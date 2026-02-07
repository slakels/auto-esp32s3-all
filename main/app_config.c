// app_config.c

#include "app_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "APP_CFG";
static const char *NVS_NAMESPACE = "app_cfg";
static const int   CFG_VERSION   = 2;  // Incremented for new structure

app_config_t g_app_config = {0};

// Thread safety for configuration access
static SemaphoreHandle_t s_config_mutex = NULL;

// Thread safety for configuration access
static SemaphoreHandle_t s_config_mutex = NULL;

void app_config_init_mutex(void)
{
    if (s_config_mutex == NULL) {
        s_config_mutex = xSemaphoreCreateMutex();
        configASSERT(s_config_mutex != NULL);
        ESP_LOGI(TAG, "Config mutex initialized");
    }
}

esp_err_t app_config_lock(void)
{
    if (s_config_mutex == NULL) {
        ESP_LOGW(TAG, "Config mutex not initialized");
        return ESP_FAIL;
    }
    return (xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) 
           ? ESP_OK : ESP_ERR_TIMEOUT;
}

void app_config_unlock(void)
{
    if (s_config_mutex != NULL) {
        xSemaphoreGive(s_config_mutex);
    }
}

// Helper for safe string copy with guaranteed null termination
void app_config_safe_str_copy(char *dst, const char *src, size_t dst_size)
{
    if (dst == NULL || src == NULL || dst_size == 0) {
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';  // Explicit null termination
}

void app_config_set_defaults(void)
{
    memset(&g_app_config, 0, sizeof(g_app_config));
    
    g_app_config.version = CFG_VERSION;
    
    // Device identification defaults
    app_config_safe_str_copy(g_app_config.device_id, "SFTCLUB_DEVICE", sizeof(g_app_config.device_id));
    app_config_safe_str_copy(g_app_config.device_name, "Default Device", sizeof(g_app_config.device_name));
    
    // Feature enables - default values
    g_app_config.enable_cards = false;
    g_app_config.enable_qr = true;
    g_app_config.enable_wifi = true;
    g_app_config.enable_mqtt = true;
    
    // WiFi defaults (from config.c)
    app_config_safe_str_copy(g_app_config.wifi_ssid, "DIGIFIBRA-3SDH", sizeof(g_app_config.wifi_ssid));
    app_config_safe_str_copy(g_app_config.wifi_pass, "CSFX66C2Yfyz", sizeof(g_app_config.wifi_pass));
    
    // MQTT defaults (from config.h)
    app_config_safe_str_copy(g_app_config.mqtt_host, "mqtt.pro.wiplaypadel.com", sizeof(g_app_config.mqtt_host));
    g_app_config.mqtt_port = 1883;
    app_config_safe_str_copy(g_app_config.mqtt_user, "admin", sizeof(g_app_config.mqtt_user));
    app_config_safe_str_copy(g_app_config.mqtt_pass, "Abc_0123456789", sizeof(g_app_config.mqtt_pass));
    app_config_safe_str_copy(g_app_config.mqtt_topic_root, "/var/deploys/topics", sizeof(g_app_config.mqtt_topic_root));
    
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
    esp_err_t err;
    
    // Lock for thread-safe access
    if ((err = app_config_lock()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire config lock");
        return err;
    }
    
    nvs_handle_t h;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No config found in NVS, using defaults");
        app_config_set_defaults();
        app_config_unlock();
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
            
            // TODO: Implement proper field-by-field migration
            // For now, preserve important user settings
            char old_device_id[32];
            char old_device_name[64];
            char old_wifi_ssid[64];
            char old_wifi_pass[64];
            char old_mqtt_host[128];
            int  old_mqtt_port;
            char old_mqtt_user[64];
            char old_mqtt_pass[64];
            bool old_enable_cards = g_app_config.enable_cards;
            bool old_enable_qr = g_app_config.enable_qr;
            
            memcpy(old_device_id, g_app_config.device_id, sizeof(old_device_id));
            memcpy(old_device_name, g_app_config.device_name, sizeof(old_device_name));
            memcpy(old_wifi_ssid, g_app_config.wifi_ssid, sizeof(old_wifi_ssid));
            memcpy(old_wifi_pass, g_app_config.wifi_pass, sizeof(old_wifi_pass));
            memcpy(old_mqtt_host, g_app_config.mqtt_host, sizeof(old_mqtt_host));
            old_mqtt_port = g_app_config.mqtt_port;
            memcpy(old_mqtt_user, g_app_config.mqtt_user, sizeof(old_mqtt_user));
            memcpy(old_mqtt_pass, g_app_config.mqtt_pass, sizeof(old_mqtt_pass));
            
            // Set defaults (includes new fields)
            app_config_set_defaults();
            
            // Restore old values
            memcpy(g_app_config.device_id, old_device_id, sizeof(g_app_config.device_id));
            memcpy(g_app_config.device_name, old_device_name, sizeof(g_app_config.device_name));
            memcpy(g_app_config.wifi_ssid, old_wifi_ssid, sizeof(g_app_config.wifi_ssid));
            memcpy(g_app_config.wifi_pass, old_wifi_pass, sizeof(g_app_config.wifi_pass));
            memcpy(g_app_config.mqtt_host, old_mqtt_host, sizeof(g_app_config.mqtt_host));
            g_app_config.mqtt_port = old_mqtt_port;
            memcpy(g_app_config.mqtt_user, old_mqtt_user, sizeof(g_app_config.mqtt_user));
            memcpy(g_app_config.mqtt_pass, old_mqtt_pass, sizeof(g_app_config.mqtt_pass));
            g_app_config.enable_cards = old_enable_cards;
            g_app_config.enable_qr = old_enable_qr;
            
            // Unlock temporarily to allow save
            app_config_unlock();
            
            // Save migrated config
            app_config_save();
            
            ESP_LOGI(TAG, "Config migrated to version %d", CFG_VERSION);
            return ESP_OK;
        } else {
            ESP_LOGI(TAG, "Config loaded from NVS (version %d)", g_app_config.version);
        }
        app_config_unlock();
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Config not found or corrupted, using defaults");
    app_config_set_defaults();
    
    // Unlock temporarily to allow save
    app_config_unlock();
    
    // Save defaults so next boot will have valid config
    app_config_save();
    return ESP_OK;
}

esp_err_t app_config_save(void)
{
    esp_err_t err;
    
    // Lock for thread-safe access
    if ((err = app_config_lock()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire config lock for save");
        return err;
    }
    
    nvs_handle_t h;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open error: %s", esp_err_to_name(err));
        app_config_unlock();
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
    
    app_config_unlock();
    return err;
}

esp_err_t app_config_reset_to_defaults(void)
{
    ESP_LOGI(TAG, "Resetting configuration to defaults");
    app_config_set_defaults();
    return app_config_save();
}
