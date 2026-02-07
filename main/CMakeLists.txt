idf_component_register(
    SRCS "gm861s_reader.c" "led_status.c" "commands.c" "mqtt_manager.c" "wifi_manager.c" "core.c" "config.c" "main.c" "rc522_reader.c" "ota_manager.c" "app_config.c" "gm861s_reader.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash mqtt esp_driver_gpio esp_https_ota esp_driver_uart
)
