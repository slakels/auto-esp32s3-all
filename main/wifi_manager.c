// wifi_manager.c

#include "wifi_manager.h"
#include "config.h"
#include "core.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "WIFI";

static esp_netif_t *s_sta_netif = NULL;
static int s_current_wifi_index = 0;
static int s_retry_count = 0;

// === MISMA FUNCIÓN QUE TENÍAS EN EL MAIN ORIGINAL ===
static esp_err_t wifi_apply_current_config(void)
{
    if (s_current_wifi_index < 0 || s_current_wifi_index >= WIFI_NETS_COUNT) {
        ESP_LOGE(TAG, "wifi_apply_current_config: índice fuera de rango");
        return ESP_FAIL;
    }

    wifi_config_t wifi_config = (wifi_config_t){ 0 };
    const wifi_net_t *net = &WIFI_NETS[s_current_wifi_index];

    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", net->ssid);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", net->pass);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Usando SSID=%s (idx=%d)", net->ssid, s_current_wifi_index);
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

// === MISMO HANDLER QUE TENÍAS EN EL MAIN ORIGINAL ===
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_START -> esp_wifi_connect()");
                s_led_mode = LED_MODE_WIFI_CONNECTING;
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                s_wifi_connected = false;
                s_mqtt_connected = false;          // si no hay wifi, tampoco mqtt
                s_led_mode = LED_MODE_WIFI_CONNECTING;

                wifi_event_sta_disconnected_t *disc =
                    (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "DISCONNECTED, reason=%d (retry=%d)", disc->reason, s_retry_count);

                s_retry_count++;

                if (s_retry_count >= MAX_RETRY_PER_AP) {
                    s_retry_count = 0;
                    s_current_wifi_index = (s_current_wifi_index + 1) % WIFI_NETS_COUNT;
                    ESP_LOGW(TAG, "Cambiando a siguiente SSID (idx=%d)", s_current_wifi_index);
                    if (wifi_apply_current_config() != ESP_OK) {
                        ESP_LOGE(TAG, "Error aplicando config WiFi para idx=%d", s_current_wifi_index);
                        s_led_mode = LED_MODE_ERROR;
                    }
                }

                esp_wifi_connect();
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                s_wifi_connected = true;
                s_retry_count = 0;
                ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: " IPSTR, IP2STR(&e->ip_info.ip));

                // Si aún no tenemos MQTT, indicamos "WiFi OK pero sin MQTT"
                if (!s_mqtt_connected) {
                    s_led_mode = LED_MODE_WIFI_OK_NO_MQTT;
                }
                break;
            }
            default:
                break;
        }
    }
}

// === INIT Y ARRANQUE DEL WIFI ===
esp_err_t wifi_init_and_start(void)
{
    // Init stack de red y loop de eventos
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Crear interfaz STA por defecto
    s_sta_netif = esp_netif_create_default_wifi_sta();
    configASSERT(s_sta_netif != NULL);

    // Init driver WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Registrar handlers de WIFI_EVENT e IP_EVENT
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            NULL));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            NULL));

    // Configuramos el primer SSID y arrancamos WiFi
    s_current_wifi_index = 0;
    s_retry_count = 0;
    s_wifi_connected = false;
    ESP_ERROR_CHECK(wifi_apply_current_config());
    ESP_ERROR_CHECK(esp_wifi_start());   // WIFI_EVENT_STA_START hará esp_wifi_connect()

    return ESP_OK;
}
