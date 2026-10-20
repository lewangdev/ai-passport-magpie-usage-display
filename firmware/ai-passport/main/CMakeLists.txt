idf_component_register(SRCS "main.c" "magpie_model.c" "magpie_ble.c" "magpie_ui.c" "magpie_logo.c"
    INCLUDE_DIRS "." REQUIRES bsp bt esp_timer esp_hw_support json nvs_flash)
