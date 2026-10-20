#include "magpie_ble.h"
#include "magpie_ui.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include <stdio.h>

static QueueHandle_t input_queue;
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } key_event_t;
static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void on_key(bsp_btn_t key, bsp_btn_ev_t event, void *user)
{
    (void)user;
    key_event_t input = {key, event}; xQueueSend(input_queue, &input, 0);
}
void app_main(void)
{
    // Do not erase unrelated NVS on an init failure.
    ESP_ERROR_CHECK(nvs_flash_init());
    input_queue = xQueueCreate(16, sizeof(key_event_t));
    QueueHandle_t ble_events = xQueueCreate(12, sizeof(magpie_ble_event_t));
    if (!input_queue || !ble_events) { ESP_LOGE("magpie", "Queue allocation failed"); return; }
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) { ESP_LOGE("magpie", "LVGL initialization failed"); return; }
    magpie_model_t model; magpie_model_init(&model, now_ms());
    if (!bsp_lvgl_lock(1000)) { ESP_LOGE("magpie", "LVGL lock failed"); return; }
    magpie_ui_create(); magpie_ui_update(&model, now_ms(), -1, 0, ""); bsp_lvgl_unlock();
    bsp_display_backlight(70);
    char toast[64] = ""; uint64_t toast_until = 0;
    bool battery_ready = bsp_battery_init() == ESP_OK;
    int battery = -1;
    if (bsp_button_init(on_key, NULL) != ESP_OK) { snprintf(toast, sizeof(toast), "BUTTON INIT FAILED"); toast_until = now_ms()+10000; }
    if (magpie_ble_start(ble_events) != ESP_OK) { snprintf(toast, sizeof(toast), "BLE INIT FAILED"); toast_until = now_ms()+10000; }
    uint32_t passkey = 0; uint64_t rendered = 0, battery_read = 0;
    // First press when asleep is consumed through release, including a following click.
    bool consume_gesture = false, wake_release = false;
    bool shown_awake = true;
    for (;;) {
        uint64_t now = now_ms();
        magpie_ble_event_t event;
        while (xQueueReceive(ble_events, &event, 0) == pdTRUE) {
            switch (event.type) {
            case BLE_LINK: magpie_connection(&model, event.connected); break;
            case BLE_SNAPSHOT:
                model.data = event.snapshot; model.valid = true; model.received_ms = now; model.pairing = false; break;
            case BLE_PASSKEY:
                model.pairing = true; model.awake = true; model.activity_ms = now; passkey = event.passkey; break;
            case BLE_PAIRED:
                model.pairing = false; model.activity_ms = now;
                snprintf(toast, sizeof(toast), "%s", event.connected ? "PAIRED" : "PAIRING FAILED"); toast_until = now+4000; break;
            case BLE_MESSAGE: snprintf(toast, sizeof(toast), "%s", event.message); toast_until = now+4000; break;
            }
            rendered = 0;
        }
        key_event_t input;
        while (xQueueReceive(input_queue, &input, 0) == pdTRUE) {
            if (input.event == BSP_BTN_PRESS && !model.awake) {
                model.awake = true; model.activity_ms = now; consume_gesture = true; wake_release = false; rendered = 0; continue;
            }
            if (consume_gesture) {
                if (input.event == BSP_BTN_RELEASE) wake_release = true;
                if (input.event == BSP_BTN_CLICK || input.event == BSP_BTN_DOUBLE) { consume_gesture = false; continue; }
                if (!(input.event == BSP_BTN_PRESS && wake_release)) continue;
                consume_gesture = false;
            }
            if (input.event != BSP_BTN_CLICK && input.event != BSP_BTN_LONG) continue;
            magpie_effect_t effect = magpie_key(&model, (magpie_key_t)input.key, input.event == BSP_BTN_LONG, now);
            if (effect != EFFECT_NONE && !magpie_ble_action(effect)) { snprintf(toast, sizeof(toast), "BLE BUSY / OFFLINE"); toast_until = now+4000; }
            rendered = 0;
        }
        magpie_tick(&model, now);
        if (model.awake != shown_awake) { shown_awake = model.awake; bsp_display_backlight(shown_awake ? 70 : 0); rendered = 0; }
        if (battery_ready && (!battery_read || now - battery_read >= 30000)) { battery = bsp_battery_soc(); battery_read = now; }
        if (model.awake && (!rendered || now - rendered >= 1000) && bsp_lvgl_lock(100)) {
            magpie_ui_update(&model, now, battery, passkey, now < toast_until ? toast : "");
            bsp_lvgl_unlock(); rendered = now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
