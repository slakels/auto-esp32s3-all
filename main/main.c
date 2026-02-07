// main.c
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "config.h"
#include "core.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "commands.h"
#include "led_status.h"
#include "rc522_reader.h"
#include "app_config.h"
#include "gm861s_reader.h"

static const char *TAG = "TOTPADEL";

// Definición de globals declarados en core.h
QueueHandle_t cmd_queue = NULL;
QueueHandle_t mqtt_out_queue = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;

bool s_wifi_connected = false;
bool s_mqtt_connected = false;
led_mode_t s_led_mode = LED_MODE_OFF;

char device_id[32] = {0};
char id_torno[32] = "1";
char topic_cmd[128] = {0};
char topic_stat[128] = {0};

static void make_device_id(void)
{
    // Thread-safe read of device_id from configuration
    if (app_config_lock() == ESP_OK) {
        snprintf(device_id, sizeof(device_id), "%s", g_app_config.device_id);
        app_config_unlock();
    } else {
        // Fallback if lock fails
        snprintf(device_id, sizeof(device_id), "SFTCLUB_DEVICE");
    }
}

static void make_topics(void)
{
    // Thread-safe read of MQTT topic root from configuration
    if (app_config_lock() == ESP_OK) {
        snprintf(topic_cmd, sizeof(topic_cmd),
                 "%s/%s", g_app_config.mqtt_topic_root, device_id);

        snprintf(topic_stat, sizeof(topic_stat),
                 "%s/%s/status", g_app_config.mqtt_topic_root, device_id);
        app_config_unlock();
    } else {
        // Fallback if lock fails
        snprintf(topic_cmd, sizeof(topic_cmd),
                 "/var/deploys/topics/%s", device_id);
        snprintf(topic_stat, sizeof(topic_stat),
                 "/var/deploys/topics/%s/status", device_id);
    }
}

void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Initialize config mutex BEFORE loading config
    app_config_init_mutex();
    ESP_ERROR_CHECK(app_config_load());

    make_device_id();
    make_topics();

    // Log device information (thread-safe read)
    char device_name_copy[64];
    if (app_config_lock() == ESP_OK) {
        snprintf(device_name_copy, sizeof(device_name_copy), "%s", g_app_config.device_name);
        app_config_unlock();
    } else {
        snprintf(device_name_copy, sizeof(device_name_copy), "Unknown");
    }

    ESP_LOGI(TAG, "DEVICE_ID=%s", device_id);
    ESP_LOGI(TAG, "DEVICE_NAME=%s", device_name_copy);
    ESP_LOGI(TAG, "topic_cmd=%s", topic_cmd);
    ESP_LOGI(TAG, "topic_stat=%s", topic_stat);
    ESP_LOGI(TAG, "topic_resp=%s", TOPIC_RESP_FIXED);

    // Colas
    cmd_queue      = xQueueCreate(64, sizeof(command_t));
    mqtt_out_queue = xQueueCreate(64, sizeof(mqtt_out_msg_t));
    configASSERT(cmd_queue      != NULL);
    configASSERT(mqtt_out_queue != NULL);

    // LED estado
    led_status_init();
    s_led_mode = LED_MODE_WIFI_CONNECTING;

    // WiFi
    ESP_ERROR_CHECK(wifi_init_and_start());

    // MQTT
    mqtt_start();
    mqtt_start_tasks();

    // Lector RC522 (dos lectores por SPI, pero mantenemos nombres pn532_*)
    bool enable_cards = false;
    bool enable_qr = false;
    
    if (app_config_lock() == ESP_OK) {
        enable_cards = g_app_config.enable_cards;
        enable_qr = g_app_config.enable_qr;
        app_config_unlock();
    }
    
    if (enable_cards) {
        ESP_ERROR_CHECK(pn532_reader_init());
        pn532_reader_start_task();
    } else {
        ESP_LOGW(TAG, "RC522 desactivado por config");
    }

    if (enable_qr) {
        ESP_ERROR_CHECK(gm861s_reader_init());
        gm861s_reader_start_task();
    } else {
        ESP_LOGW(TAG, "QR desactivado por config");
    }

    // Tasks de comandos
    commands_start_task();

    // Task de LED
    led_status_start_task();

    ESP_LOGI(TAG, "Sistema TOTPADEL arrancado");
}
