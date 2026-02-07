// mqtt_manager.c

#include "mqtt_manager.h"
#include "config.h"
#include "core.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "esp_wifi.h"     // wifi_ap_record_t, esp_wifi_sta_get_ap_info
#include "esp_timer.h"    // esp_timer_get_time
#include "esp_system.h"   // esp_get_free_heap_size

#include "ota_manager.h"
#include "app_config.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "MQTT";

// ================== COLA DE SALIDA ==================

bool mqtt_enqueue(const char *topic,
                  const char *payload,
                  int qos,
                  int retain)
{
    if (mqtt_out_queue == NULL) {
        ESP_LOGW(TAG, "mqtt_out_queue no inicializada, no se publica");
        return false;
    }

    mqtt_out_msg_t out = {0};
    strncpy(out.topic,   topic,   sizeof(out.topic)   - 1);
    strncpy(out.payload, payload, sizeof(out.payload) - 1);
    out.qos    = qos;
    out.retain = retain;

    if (xQueueSend(mqtt_out_queue, &out, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "mqtt_out_queue llena, se descarta mensaje para '%s'", topic);
        return false;
    }

    return true;
}

// ================== TASK DE PUBLICACIÓN ==================

static void mqtt_out_task(void *pv)
{
    mqtt_out_msg_t msg;

    while (1) {
        if (xQueueReceive(mqtt_out_queue, &msg, portMAX_DELAY) == pdTRUE) {

            // Esperar a que MQTT esté conectado antes de publicar
            while (!s_mqtt_connected) {
                ESP_LOGW(TAG,
                         "MQTT no conectado, esperando para publicar '%s'",
                         msg.topic);
                vTaskDelay(pdMS_TO_TICKS(200));   // 200 ms para no bloquear el WDT
            }

            int msg_id = esp_mqtt_client_publish(
                             mqtt_client,
                             msg.topic,
                             msg.payload,
                             0,              // null-terminated
                             msg.qos,
                             msg.retain);

            if (msg_id < 0) {
                ESP_LOGW(TAG, "Error publicando en '%s' (msg_id=%d)",
                         msg.topic, msg_id);
                // Opcional: podrías re-encolar aquí si quisieras reintentar
            }
        }
    }
}


// ================== TASK STATUS PERIÓDICO ==================

static void status_task(void *pv)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));  // cada 30s

        if (!s_mqtt_connected) {
            continue;
        }

        // Obtener datos del sistema
        wifi_ap_record_t wifi_info;
        int rssi = -999;
        if (esp_wifi_sta_get_ap_info(&wifi_info) == ESP_OK) {
            rssi = wifi_info.rssi;
        }

        uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t free_heap = esp_get_free_heap_size();

        // Estado RC522 — funciones que añadiremos después
        const char *rc522_in_status  = "DISABLED";
        const char *rc522_out_status = "DISABLED";

        if (g_app_config.enable_cards) {
            rc522_in_status  = rc522_last_in_ok()  ? "OK"   : "FAIL";
            rc522_out_status = rc522_last_out_ok() ? "OK"   : "FAIL";
        }

        // Crear JSON
        cJSON *root = cJSON_CreateObject();
        if (!root) continue;

        cJSON_AddStringToObject(root, "action", "status");
        cJSON_AddBoolToObject  (root, "online", true);
        cJSON_AddStringToObject(root, "id", device_id);

        cJSON_AddNumberToObject(root, "rssi", rssi);
        cJSON_AddNumberToObject(root, "uptime", uptime);
        cJSON_AddNumberToObject(root, "freeHeap", free_heap);
        cJSON_AddStringToObject(root, "fw", FW_VERSION);

        cJSON *rc = cJSON_CreateObject();
        cJSON_AddStringToObject(rc, "in", rc522_in_status);
        cJSON_AddStringToObject(rc, "out", rc522_out_status);
        cJSON_AddItemToObject(root, "rc522", rc);

        char *json = cJSON_PrintUnformatted(root);

        if (json) {
            mqtt_enqueue(topic_stat, json, 1, 1);  // retain=1
            cJSON_free(json);
        }

        cJSON_Delete(root);
    }
}


// ================== EVENTOS MQTT ==================

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_mqtt_connected = true;
            if (s_wifi_connected) {
                s_led_mode = LED_MODE_MQTT_OK;   // WiFi + MQTT OK
            }
            esp_mqtt_client_subscribe(mqtt_client, topic_cmd, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_mqtt_connected = false;
            if (s_wifi_connected) {
                s_led_mode = LED_MODE_WIFI_OK_NO_MQTT;
            } else {
                s_led_mode = LED_MODE_WIFI_CONNECTING;
            }
            break;

        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT DATA: topic=%.*s data=%.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            // Parse JSON
            cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
            if (!root) {
                ESP_LOGW(TAG, "JSON parse error");
                break;
            }

            command_t cmd = (command_t){0};

            cJSON *action    = cJSON_GetObjectItem(root, "action");
            cJSON *pin       = cJSON_GetObjectItem(root, "pin");
            cJSON *estat     = cJSON_GetObjectItem(root, "estat");
            cJSON *idPista   = cJSON_GetObjectItem(root, "idPista");
            cJSON *idPet     = cJSON_GetObjectItem(root, "idPeticion");
            cJSON *result    = cJSON_GetObjectItem(root, "result");
            cJSON *type      = cJSON_GetObjectItem(root, "type");
            cJSON *url       = cJSON_GetObjectItem(root, "url");

            if (cJSON_IsString(action) && action->valuestring) {
                strncpy(cmd.action, action->valuestring, sizeof(cmd.action)-1);
            }

            if (cJSON_IsString(pin) && pin->valuestring) {
                cmd.pin = atoi(pin->valuestring);
            } else if (cJSON_IsNumber(pin)) {
                cmd.pin = pin->valueint;
            }

            if (cJSON_IsString(estat) && estat->valuestring) {
                cmd.estat = atoi(estat->valuestring);
            } else if (cJSON_IsNumber(estat)) {
                cmd.estat = estat->valueint;
            }

            if (cJSON_IsString(idPista) && idPista->valuestring) {
                cmd.id_pista = atoi(idPista->valuestring);
            } else if (cJSON_IsNumber(idPista)) {
                cmd.id_pista = idPista->valueint;
            }

            if (cJSON_IsString(idPet) && idPet->valuestring) {
                strncpy(cmd.id_peticion, idPet->valuestring,
                        sizeof(cmd.id_peticion)-1);
            } else {
                strcpy(cmd.id_peticion, "-");
            }

            if (cJSON_IsString(result) && result->valuestring) {
                strncpy(cmd.result, result->valuestring,
                        sizeof(cmd.result)-1);
            }

            if (cJSON_IsString(type) && type->valuestring) {
                strncpy(cmd.type, type->valuestring,
                        sizeof(cmd.type)-1);
            }

            // Guardar payload bruto (para writeCard)
            size_t len = event->data_len;
            if (len >= sizeof(cmd.payload)) len = sizeof(cmd.payload) - 1;
            memcpy(cmd.payload, event->data, len);
            cmd.payload[len] = '\0';

            // 👇 CASO ESPECIAL: otaUpdate (no va a cmd_queue)
            if (cJSON_IsString(action) && action->valuestring &&
                strcmp(action->valuestring, "otaUpdate") == 0) {

                if (!cJSON_IsString(url) || !url->valuestring) {
                    ESP_LOGW(TAG, "otaUpdate sin campo 'url'");
                    cJSON_Delete(root);
                    break;
                }

                const char *url_fw = url->valuestring;
                const char *id_pet = cmd.id_peticion[0] ? cmd.id_peticion : "-";

                ESP_LOGI(TAG, "Recibido otaUpdate: url=%s idPeticion=%s",
                         url_fw, id_pet);

                if (!ota_start_async(url_fw, id_pet)) {
                    ESP_LOGW(TAG, "Fallo al lanzar ota_start_async");

                    // Respuesta inmediata KO
                    cJSON *resp = cJSON_CreateObject();
                    if (resp) {
                        cJSON_AddStringToObject(resp, "action", "retornoOta");
                        cJSON_AddBoolToObject  (resp, "ok",     false);
                        cJSON_AddStringToObject(resp, "id",     device_id);
                        cJSON_AddStringToObject(resp, "idPeticion", id_pet);
                        cJSON_AddStringToObject(resp, "url",    url_fw);

                        char *json_resp = cJSON_PrintUnformatted(resp);
                        if (json_resp) {
                            mqtt_enqueue(TOPIC_RESP_FIXED, json_resp, 1, 0);
                            cJSON_free(json_resp);
                        }
                        cJSON_Delete(resp);
                    }
                }

                cJSON_Delete(root);
                break;   // importante: NO encolamos en cmd_queue
            }

            // Resto de acciones normales → cmd_queue
            cJSON_Delete(root);

            if (xQueueSend(cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
                ESP_LOGW(TAG, "cmd_queue: error inesperado al encolar comando");
            }

            break;
        }

        default:
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void) handler_args;
    (void) base;
    (void) event_id;
    mqtt_event_handler_cb((esp_mqtt_event_handle_t) event_data);
}

// ================== INICIALIZACIÓN MQTT ==================

void mqtt_start(void)
{
    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", MQTT_HOST, MQTT_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.broker.address.uri = uri;
    mqtt_cfg.credentials.username                = MQTT_USER;
    mqtt_cfg.credentials.client_id               = device_id;
    mqtt_cfg.credentials.authentication.password = MQTT_PASS;
    mqtt_cfg.session.disable_clean_session       = true;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
                        mqtt_client,
                        ESP_EVENT_ANY_ID,
                        mqtt_event_handler,
                        NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

// ================== CREACIÓN DE TASKS ==================

void mqtt_start_tasks(void)
{
    // Task de salida MQTT
    xTaskCreate(mqtt_out_task, "mqtt_out_task", 6144, NULL, 4, NULL);

    // Task status periódico
    xTaskCreate(status_task, "status_task", 4096, NULL, 3, NULL);
}
