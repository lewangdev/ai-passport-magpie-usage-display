#include "magpie_ble.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

// UUID bytes are least significant first, as required by NimBLE.
#define UUID(n) BLE_UUID128_INIT(n,0xde,0xc0,0x70,0x65,0x1b,0xf8,0xa0,0x44,0x4b,0x6d,0x8f,0x20,0x6c,0x8b,0x7d)
static const ble_uuid128_t service_uuid = UUID(1), status_uuid = UUID(2), command_uuid = UUID(3),
    result_uuid = UUID(4), info_uuid = UUID(5);
static QueueHandle_t event_queue, action_queue;
static struct ble_npl_event action_event;
static struct ble_npl_callout watchdog;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE, command_handle;
static uint8_t address_type;
static uint32_t sequence, session, request_id;
static uint64_t last_status_ms;
static bool subscribed;
static const char *TAG = "magpie_ble";
void ble_store_config_init(void);
static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static int gap_event(struct ble_gap_event *event, void *arg);

static bool publish(magpie_ble_event_t *event)
{
    // Application events are consumed in a dedicated loop. Never block the host.
    return xQueueSend(event_queue, event, 0) == pdTRUE;
}
static void message(const char *text)
{
    magpie_ble_event_t event = {.type = BLE_MESSAGE};
    snprintf(event.message, sizeof(event.message), "%s", text); publish(&event);
}
static int access_characteristic(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)attr;
    if (arg == (void *)5) {
        static const char info[] = "{\"v\":4,\"fw\":\"0.4.0-passport\",\"screen\":\"240x320\"}";
        return os_mbuf_append(ctx->om, info, sizeof(info)-1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) || !desc.sec_state.encrypted || !desc.sec_state.authenticated)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    uint16_t length = OS_MBUF_PKTLEN(ctx->om);
    if (length == 0 || length > MAGPIE_PAYLOAD_MAX) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    char payload[MAGPIE_PAYLOAD_MAX + 1];
    if (os_mbuf_copydata(ctx->om, 0, length, payload)) return BLE_ATT_ERR_UNLIKELY;
    payload[length] = 0;
    magpie_ble_event_t event = {0};
    if (arg == (void *)2) {
        event.type = BLE_SNAPSHOT;
        if (!magpie_parse(payload, length, sequence, &event.snapshot)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        if (!publish(&event)) return BLE_ATT_ERR_INSUFFICIENT_RES;
        sequence = event.snapshot.sequence; last_status_ms = now_ms();
    } else {
        if (!magpie_json_frame(payload, length)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        const char *end = NULL;
        cJSON *root = cJSON_ParseWithLengthOpts(payload, length + 1, &end, true);
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "v");
        cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "m");
        if (!root || !cJSON_IsNumber(v) || v->valueint != 4 || !cJSON_IsString(m)) {
            cJSON_Delete(root); return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        event.type = BLE_MESSAGE;
        size_t i = 0;
        for (; m->valuestring[i] && i + 1 < sizeof(event.message); ++i) {
            unsigned char ch = m->valuestring[i];
            event.message[i] = ch >= 32 && ch < 127 ? ch : '?';
        }
        event.message[i] = 0;
        cJSON_Delete(root);
        if (!publish(&event)) return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}
static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid=&status_uuid.u, .access_cb=access_characteristic, .arg=(void *)2,
         .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN, .min_key_size=16},
        {.uuid=&command_uuid.u, .access_cb=access_characteristic, .arg=(void *)3, .val_handle=&command_handle,
         .flags=BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC | BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN, .min_key_size=16},
        {.uuid=&result_uuid.u, .access_cb=access_characteristic, .arg=(void *)4,
         .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN, .min_key_size=16},
        {.uuid=&info_uuid.u, .access_cb=access_characteristic, .arg=(void *)5, .flags=BLE_GATT_CHR_F_READ},
        {0}
    }
}, {0}};
static int advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid; fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) return rc;
    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)"Magpie Display"; response.name_len = 14; response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc) return rc;
    struct ble_gap_adv_params params = {.conn_mode=BLE_GAP_CONN_MODE_UND, .disc_mode=BLE_GAP_DISC_MODE_GEN};
    return ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    magpie_ble_event_t update = {.type=BLE_LINK};
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) { advertise(); break; }
        connection = event->connect.conn_handle; sequence = 0; subscribed = false; last_status_ms = now_ms();
        update.connected = true;
        if (!publish(&update)) ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection = BLE_HS_CONN_HANDLE_NONE; subscribed = false;
        publish(&update); advertise(); break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == command_handle) subscribed = event->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) return BLE_HS_ENOTSUP;
        struct ble_sm_io io = {.action=BLE_SM_IOACT_DISP, .passkey=esp_random() % 1000000};
        update.type = BLE_PASSKEY; update.passkey = io.passkey;
        if (!publish(&update)) return BLE_HS_ENOMEM;
        return ble_sm_inject_io(event->passkey.conn_handle, &io);
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;
        bool ok = !event->enc_change.status && !ble_gap_conn_find(connection, &desc) &&
                  desc.sec_state.encrypted && desc.sec_state.authenticated;
        update.type = BLE_PAIRED; update.connected = ok; publish(&update);
        if (!ok) ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
        break;
    }
    // A lost Mac bond requires explicit reset on the device; never silently remove a peer.
    case BLE_GAP_EVENT_REPEAT_PAIRING: return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: break;
    }
    return 0;
}
static void action_callback(struct ble_npl_event *event)
{
    (void)event;
    magpie_effect_t action;
    while (xQueueReceive(action_queue, &action, 0) == pdTRUE) {
        if (action == EFFECT_CLEAR_BONDS) {
            // Disconnect first; erase only NimBLE peer keys, preserving all other NVS.
            if (connection != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            int rc = ble_store_clear();
            int peers = -1;
            if (rc == 0) rc = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peers);
            if (rc == 0 && peers == 0) { message("PAIRING CLEARED"); esp_restart(); }
            else message("CLEAR FAILED");
            continue;
        }
        struct ble_gap_conn_desc desc;
        if (connection == BLE_HS_CONN_HANDLE_NONE || !subscribed || ble_gap_conn_find(connection, &desc) ||
            !desc.sec_state.encrypted || !desc.sec_state.authenticated) { message("COMPANION OFFLINE"); continue; }
        char payload[96];
        int length = snprintf(payload, sizeof(payload), "{\"v\":4,\"sid\":%lu,\"id\":%lu,\"a\":\"%s\"}",
            (unsigned long)session, (unsigned long)++request_id, action == EFFECT_FOCUS ? "focus_magpie" : "refresh");
        if (length > ble_att_mtu(connection) - 3) { message("BLE MTU TOO SMALL"); continue; }
        struct os_mbuf *buffer = ble_hs_mbuf_from_flat(payload, length);
        if (!buffer) { message("OUT OF MEMORY"); continue; }
        int rc = ble_gatts_notify_custom(connection, command_handle, buffer);
        message(rc == 0 ? "REQUEST SENT" : "SEND FAILED");
    }
}
static void watchdog_callback(struct ble_npl_event *event)
{
    (void)event;
    // Permit STALE to remain visible before reconnecting an unresponsive companion.
    if (connection != BLE_HS_CONN_HANDLE_NONE && now_ms() - last_status_ms >= 90000)
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    ble_npl_callout_reset(&watchdog, ble_npl_time_ms_to_ticks32(5000));
}
static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &address_type);
    if (!rc) rc = advertise();
    if (rc) { ESP_LOGE(TAG, "advertising failed: %d", rc); message("BLE START FAILED"); }
}
static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset: %d", reason);
    connection = BLE_HS_CONN_HANDLE_NONE; subscribed = false;
    magpie_ble_event_t event = {.type=BLE_LINK}; publish(&event);
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); vTaskDelete(NULL); }
esp_err_t magpie_ble_start(QueueHandle_t events)
{
    event_queue = events;
    action_queue = xQueueCreate(4, sizeof(magpie_effect_t));
    if (!action_queue) return ESP_ERR_NO_MEM;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) { vQueueDelete(action_queue); action_queue = NULL; return err; }
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Magpie Display");
    ble_hs_cfg.sync_cb = on_sync; ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_mitm = 1; ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1; ble_hs_cfg.sm_sec_lvl = 4;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    int rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (!rc) rc = ble_att_set_preferred_mtu(256);
    session = esp_random(); if (!session) session = 1;
    ble_npl_event_init(&action_event, action_callback, NULL);
    ble_npl_callout_init(&watchdog, nimble_port_get_dflt_eventq(), watchdog_callback, NULL);
    if (rc || xTaskCreate(host_task, "magpie_ble", 4096, NULL, 5, NULL) != pdPASS) {
        nimble_port_deinit(); vQueueDelete(action_queue); action_queue = NULL; return ESP_FAIL;
    }
    ble_npl_callout_reset(&watchdog, ble_npl_time_ms_to_ticks32(5000));
    return ESP_OK;
}
bool magpie_ble_action(magpie_effect_t action)
{
    if (action != EFFECT_REFRESH && action != EFFECT_FOCUS && action != EFFECT_CLEAR_BONDS) return false;
    if (!action_queue || xQueueSend(action_queue, &action, 0) != pdTRUE) return false;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &action_event); return true;
}
