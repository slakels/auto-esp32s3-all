// app_config.c

#include "app_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "APP_CFG";
static const char *NVS_NAMESPACE = "app_cfg";
static const int   CFG_VERSION   = 1;

app_config_t g_app_config = {0};

void app_config_set_defaults(void)
{
    g_app_config.version      = CFG_VERSION;
    g_app_config.enable_cards = false;  // por defecto: tarjetas activas
    g_app_config.enable_qr = true;
    // otros defaults...
}

esp_err_t app_config_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No hay config en NVS, usando defaults");
        app_config_set_defaults();
        return ESP_OK;
    }

    size_t len = sizeof(g_app_config);
    err = nvs_get_blob(h, "cfg", &g_app_config, &len);
    nvs_close(h);

    if (err == ESP_OK && g_app_config.version == CFG_VERSION) {
        ESP_LOGI(TAG, "Config cargada de NVS (version %d)", g_app_config.version);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Config inexistente o version distinta, usando defaults");
    app_config_set_defaults();
    // guardamos defaults para que la próxima vez ya exista
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
        ESP_LOGE(TAG, "Error guardando config: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Config guardada en NVS");
    }
    return err;
}
