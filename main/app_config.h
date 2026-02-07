// app_config.h
#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Multi-device configuration structure
// All settings are stored in NVS and persist across firmware updates
typedef struct {
    int  version;           // Config version for migrations
    
    // Device identification
    char device_id[32];     // Unique device identifier
    char device_name[64];   // Human-readable device name
    
    // Feature enables
    bool enable_cards;      // Enable RC522 card readers
    bool enable_qr;         // Enable QR code reader
    bool enable_wifi;       // Enable WiFi (can disable for testing)
    bool enable_mqtt;       // Enable MQTT
    
    // WiFi configuration
    char wifi_ssid[64];     // WiFi SSID
    char wifi_pass[64];     // WiFi password
    
    // MQTT configuration
    char mqtt_host[128];    // MQTT broker hostname/IP
    int  mqtt_port;         // MQTT broker port
    char mqtt_user[64];     // MQTT username
    char mqtt_pass[64];     // MQTT password
    char mqtt_topic_root[128]; // MQTT topic root path
    
    // GPIO pin assignments - RC522 SPI
    int  rc522_pin_mosi;
    int  rc522_pin_miso;
    int  rc522_pin_sck;
    int  rc522_pin_ss1;     // Chip select reader 1
    int  rc522_pin_rst1;    // Reset reader 1
    int  rc522_pin_ss2;     // Chip select reader 2
    int  rc522_pin_rst2;    // Reset reader 2
    
    // GPIO pins - Relays
    int  torn_in_pin;       // Entrada relay
    int  torn_out_pin;      // Salida relay
    
    // GPIO pins - Buzzer
    int  buzzer_pin;
    
    // GPIO pins - QR Reader UART
    int  qr_uart_tx;
    int  qr_uart_rx;
    int  qr_uart_num;       // UART port number (0, 1, 2)
    int  qr_baud_rate;
} app_config_t;

extern app_config_t g_app_config;

// Thread-safe configuration access
void      app_config_init_mutex(void);
esp_err_t app_config_lock(void);
void      app_config_unlock(void);

esp_err_t app_config_load(void);
esp_err_t app_config_save(void);
void      app_config_set_defaults(void);
esp_err_t app_config_reset_to_defaults(void);

// Helper for safe string copy with guaranteed null termination
void app_config_safe_str_copy(char *dst, const char *src, size_t dst_size);
