// main.c
#include "core.h"

QueueHandle_t cmd_queue = NULL;
QueueHandle_t mqtt_out_queue = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;

bool s_wifi_connected = false;
bool s_mqtt_connected = false;
led_mode_t s_led_mode = LED_MODE_OFF;

char device_id[32] = {0};
char topic_cmd[128] = {0};
char topic_stat[128] = {0};
char id_torno [32] = {0};
