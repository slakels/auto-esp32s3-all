// app_config.c

#include "app_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include <string.h>

static const char *TAG = "APP_CFG";
static const char *NVS_NAMESPACE = "app_cfg";
static const int   CFG_VERSION   = 2;  // Incremented for new structure

app_config_t g_app_config = {0};

void app_config_set_defaults(void)
{
    memset(&g_app_config, 0, sizeof(g_app_config));
    
    g_app_config.version = CFG_VERSION;
    
    // Device identification defaults
    strncpy(g_app_config.device_id, "SFTCLUB_DEVICE", sizeof(g_app_config.device_id) - 1);
    strncpy(g_app_config.device_name, "Default Device", sizeof(g_app_config.device_name) - 1);
    
    // Feature enables - default values
    g_app_config.enable_cards = false;
    g_app_config.enable_qr = true;
    g_app_config.enable_wifi = true;
    g_app_config.enable_mqtt = true;
    
    // WiFi defaults (from config.c)
    strncpy(g_app_config.wifi_ssid, "DIGIFIBRA-3SDH", sizeof(g_app_config.wifi_ssid) - 1);
    strncpy(g_app_config.wifi_pass, "CSFX66C2Yfyz", sizeof(g_app_config.wifi_pass) - 1);
    
    // MQTT defaults (from config.h)
    strncpy(g_app_config.mqtt_host, "mqtt.pro.wiplaypadel.com", sizeof(g_app_config.mqtt_host) - 1);
    g_app_config.mqtt_port = 1883;
    strncpy(g_app_config.mqtt_user, "admin", sizeof(g_app_config.mqtt_user) - 1);
    strncpy(g_app_config.mqtt_pass, "Abc_0123456789", sizeof(g_app_config.mqtt_pass) - 1);
    strncpy(g_app_config.mqtt_topic_root, "/var/deploys/topics", sizeof(g_app_config.mqtt_topic_root) - 1);
    
    // GPIO defaults - RC522 SPI (from config.h)
    g_app_config.rc522_pin_mosi = 11;
    g_app_config.rc522_pin_miso = 13;
    g_app_config.rc522_pin_sck = 12;
    g_app_config.rc522_pin_ss1 = 10;
    g_app_config.rc522_pin_rst1 = 16;
    g_app_config.rc522_pin_ss2 = 15;
    g_app_config.rc522_pin_rst2 = 17;
    
    // GPIO defaults - Relays (from config.h)
    g_app_config.torn_in_pin = 19;
    g_app_config.torn_out_pin = 20;
    
    // GPIO defaults - Buzzer (from config.h)
    g_app_config.buzzer_pin = 21;
    
    // GPIO defaults - QR Reader UART (from config.h)
    g_app_config.qr_uart_tx = 17;
    g_app_config.qr_uart_rx = 18;
    g_app_config.qr_uart_num = UART_NUM_1;
    g_app_config.qr_baud_rate = 9600;
    
    ESP_LOGI(TAG, "Default configuration set");
}

esp_err_t app_config_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No config found in NVS, using defaults");
        app_config_set_defaults();
        return ESP_OK;
    }

    size_t len = sizeof(g_app_config);
    err = nvs_get_blob(h, "cfg", &g_app_config, &len);
    nvs_close(h);

    if (err == ESP_OK) {
        // Check version and migrate if needed
        if (g_app_config.version != CFG_VERSION) {
            ESP_LOGW(TAG, "Config version mismatch (stored:%d, expected:%d), migrating...", 
                     g_app_config.version, CFG_VERSION);
            
            // Save old values that we want to preserve
            bool old_enable_cards = g_app_config.enable_cards;
            bool old_enable_qr = g_app_config.enable_qr;
            
            // Set defaults (includes new fields)
            app_config_set_defaults();
            
            // Restore old values
            g_app_config.enable_cards = old_enable_cards;
            g_app_config.enable_qr = old_enable_qr;
            
            // Save migrated config
            app_config_save();
            
            ESP_LOGI(TAG, "Config migrated to version %d", CFG_VERSION);
        } else {
            ESP_LOGI(TAG, "Config loaded from NVS (version %d)", g_app_config.version);
        }
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Config not found or corrupted, using defaults");
    app_config_set_defaults();
    // Save defaults so next boot will have valid config
    app_config_save();
    return ESP_OK;
}

esp_err_t app_config_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open error: %s", esp_err_to_name(err));
        return err;
    }

    g_app_config.version = CFG_VERSION;
    err = nvs_set_blob(h, "cfg", &g_app_config, sizeof(g_app_config));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Config saved to NVS");
    }
    return err;
}

esp_err_t app_config_reset_to_defaults(void)
{
    ESP_LOGI(TAG, "Resetting configuration to defaults");
    app_config_set_defaults();
    return app_config_save();
}
