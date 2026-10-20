#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "magpie_model.h"

typedef enum { BLE_LINK, BLE_SNAPSHOT, BLE_PASSKEY, BLE_PAIRED, BLE_MESSAGE } magpie_event_type_t;
typedef struct {
    magpie_event_type_t type;
    bool connected;
    uint32_t passkey;
    magpie_snapshot_t snapshot;
    char message[64];
} magpie_ble_event_t;

esp_err_t magpie_ble_start(QueueHandle_t events);
bool magpie_ble_action(magpie_effect_t action);
