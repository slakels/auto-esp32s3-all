// ota_manager.c

#include "ota_manager.h"
#include "mqtt_manager.h"
#include "config.h"
#include "core.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "OTA";

typedef struct {
    char url[256];
    char id_peticion[64];
} ota_request_t;

static void ota_task(void *pv)
{
    ota_request_t *req = (ota_request_t *)pv;
    ESP_LOGI(TAG, "Iniciando OTA desde URL: %s", req->url);

    esp_http_client_config_t http_cfg = {
        .url = req->url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    bool ok = (ret == ESP_OK);

    ESP_LOGI(TAG, "OTA finalizada: %s", ok ? "OK" : "KO");

    // Construir retorno OTA por MQTT
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "action", "retornoOta");
        cJSON_AddBoolToObject  (root, "ok",     ok);
        cJSON_AddStringToObject(root, "id",     device_id);
        cJSON_AddStringToObject(root, "idPeticion", req->id_peticion[0] ? req->id_peticion : "-");
        cJSON_AddStringToObject(root, "url",    req->url);

        char *json = cJSON_PrintUnformatted(root);
        if (json) {
            mqtt_enqueue(TOPIC_RESP_FIXED, json, 1, 0);
            cJSON_free(json);
        }
        cJSON_Delete(root);
    }

    if (ok) {
        ESP_LOGI(TAG, "Reiniciando tras OTA OK...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    free(req);
    vTaskDelete(NULL);
}

bool ota_start_async(const char *url_firmware, const char *id_peticion)
{
    if (!url_firmware || url_firmware[0] == '\0') {
        ESP_LOGW(TAG, "ota_start_async: URL vacia");
        return false;
    }

    ota_request_t *req = calloc(1, sizeof(ota_request_t));
    if (!req) {
        ESP_LOGE(TAG, "ota_start_async: sin memoria");
        return false;
    }

    strncpy(req->url, url_firmware, sizeof(req->url) - 1);
    if (id_peticion) {
        strncpy(req->id_peticion, id_peticion, sizeof(req->id_peticion) - 1);
    }

    BaseType_t r = xTaskCreate(ota_task, "ota_task", 8192, req, 5, NULL);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "ota_start_async: no se pudo crear ota_task");
        free(req);
        return false;
    }

    return true;
}
