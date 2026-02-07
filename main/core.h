// core.h
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mqtt_client.h"

// Tipos compartidos
typedef struct {
    char  action[32];
    int   pin;
    int   estat;
    int   id_pista;
    char  id_peticion[32];

    char payload[1024];  // Increased from 256 to 1024 for larger config payloads
    char result[8];      // "OK" / "KO"
    char type[8];        // "IN" / "OUT"
} command_t;

typedef struct {
    char topic[128];
    char payload[512];   // Increased from 256 to 512 for larger messages
    int  qos;
    int  retain;
} mqtt_out_msg_t;

// LED
typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_WIFI_CONNECTING,
    LED_MODE_WIFI_OK_NO_MQTT,
    LED_MODE_MQTT_OK,
    LED_MODE_ERROR
} led_mode_t;

// Globals accesibles desde varios módulos
extern QueueHandle_t cmd_queue;
extern QueueHandle_t mqtt_out_queue;

extern esp_mqtt_client_handle_t mqtt_client;

extern bool s_wifi_connected;
extern bool s_mqtt_connected;
extern led_mode_t s_led_mode;

extern char device_id[32];
extern char topic_cmd[128];
extern char topic_stat[128];
extern char id_torno[32];
