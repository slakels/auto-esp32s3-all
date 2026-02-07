// commands.c

#include "commands.h"
#include "config.h"
#include "core.h"
#include "mqtt_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "cJSON.h"
#include "rc522_reader.h"
#include "app_config.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

static const char *TAG = "CMD";

// ================== INPUT VALIDATION ==================

#define ESP32_S3_GPIO_MAX 48
#define MIN_VALID_PORT 1
#define MAX_VALID_PORT 65535

static bool is_valid_gpio(int pin)
{
    return pin >= 0 && pin <= ESP32_S3_GPIO_MAX;
}

static bool is_valid_port(int port)
{
    return port >= MIN_VALID_PORT && port <= MAX_VALID_PORT;
}

static bool is_valid_uart_num(int uart_num)
{
    return uart_num >= 0 && uart_num <= 2;  // ESP32-S3 has UART0, UART1, UART2
}

// ================== GPIO HELPERS ==================

static void gpio_init_if_needed(int gpio_num)
{
    // quick & dirty: ajusta tamaño si usas GPIO más altos
    static bool initialized[50] = {0};

    if (gpio_num < 0 || gpio_num >= 50) return;
    if (initialized[gpio_num]) return;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio_num, 0);
    initialized[gpio_num] = true;
}

static void pulsar_gpio_blocking(int gpio, int ms, bool invertido)
{
    gpio_init_if_needed(gpio);
    gpio_set_level(gpio, invertido ? 0 : 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(gpio, invertido ? 1 : 0);
}

static void interruptor_gpio_set(int gpio, int estado, bool inverso)
{
    gpio_init_if_needed(gpio);
    int enc = inverso ? 0 : 1;
    int off = inverso ? 1 : 0;
    gpio_set_level(gpio, (estado == 0) ? enc : off);
}

// ================== RESPUESTAS MQTT ==================

static void publish_resp(const command_t *cmd,
                         const char *action_resp,
                         int estat_extra,
                         bool include_pista)
{
    char payload[256];

    if (include_pista) {
        snprintf(payload, sizeof(payload),
                 "{\"action\":\"%s\",\"pin\":%d,\"estat\":\"%d\","
                 "\"idPista\":\"%d\",\"idPeticion\":\"%s\"}",
                 action_resp,
                 cmd->pin,
                 estat_extra,
                 cmd->id_pista,
                 cmd->id_peticion);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"action\":\"%s\",\"pin\":%d,"
                 "\"idPeticion\":\"%s\"}",
                 action_resp,
                 cmd->pin,
                 cmd->id_peticion);
    }

    (void) mqtt_enqueue(TOPIC_RESP_FIXED, payload, 0, 0);
}

static void publish_status_now(const char *id_peticion)
{
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"action\":\"status\",\"online\":true,"
             "\"id\":\"%s\",\"idPeticion\":\"%s\"}",
             device_id,
             id_peticion ? id_peticion : "-");

    mqtt_enqueue(TOPIC_RESP_FIXED, payload, 0, 0);
}

// ================== LÓGICA DE COMANDOS ==================

static void handle_command(const command_t *cmd)
{
    if (strcmp(cmd->action, "pulsadorLuz") == 0) {
        // usa TEMPS_PULSADOR_MS
        pulsar_gpio_blocking(cmd->pin, TEMPS_PULSADOR_MS, false);
        publish_resp(cmd, "retornoLuz", cmd->estat, true);

    } else if (strcmp(cmd->action, "interruptorLuz") == 0) {
        int estat = cmd->estat;
        if (estat == 2) estat = 1;
        interruptor_gpio_set(cmd->pin, estat, INTERRUPTOR_INVERSO);
        publish_resp(cmd, "retornoLuz", estat, true);

    } else if (strcmp(cmd->action, "pulsador") == 0) {
        pulsar_gpio_blocking(cmd->pin, TEMPS_PULSADOR_MS, false);
        publish_resp(cmd, "retornoPulsador", 0, false);

    } else if (strcmp(cmd->action, "pulsadorInverso") == 0) {
        pulsar_gpio_blocking(cmd->pin, 500 /*0.5s*/, BOCINA_INVERSA);
        publish_resp(cmd, "retornoPulsador", 0, false);

    } else if (strcmp(cmd->action, "interruptor") == 0) {
        int estat = cmd->estat;
        if (estat == 2) estat = 1;
        interruptor_gpio_set(cmd->pin, estat, INTERRUPTOR_INVERSO);
        publish_resp(cmd, "retornoInterruptor", estat, false);

    } else if (strcmp(cmd->action, "obrirPorta") == 0) {
        pulsar_gpio_blocking(cmd->pin, 500 /*0.5s*/, ENTRADA_INVERSO);
        publish_resp(cmd, "retornoObrirPorta", 0, false);

    } else if (strcmp(cmd->action, "obrirPortaMaterial") == 0) {
        pulsar_gpio_blocking(cmd->pin, TEMPS_MATERIAL_MS, MATERIAL_INVERSO);
        publish_resp(cmd, "retornoObrirPortaMaterial", 0, false);

    } else if (strcmp(cmd->action, "obrirPortaVenta") == 0) {
        pulsar_gpio_blocking(cmd->pin, 500 /*0.5s*/, false);
        publish_resp(cmd, "retornoObrirPortaVenta", 0, false);

    } else if (strcmp(cmd->action, "getConfig") == 0) {

        // Thread-safe read of configuration
        if (app_config_lock() != ESP_OK) {
            ESP_LOGW(TAG, "getConfig: Failed to acquire config lock");
            return;
        }

        cJSON *root = cJSON_CreateObject();
        if (!root) {
            app_config_unlock();
            return;
        }

        cJSON_AddStringToObject(root, "action", "retornoConfig");
        cJSON_AddStringToObject(root, "id", device_id);
        cJSON_AddStringToObject(root, "idPeticion", cmd->id_peticion);
        cJSON_AddNumberToObject(root, "version", g_app_config.version);
        
        // Device info
        cJSON_AddStringToObject(root, "deviceId", g_app_config.device_id);
        cJSON_AddStringToObject(root, "deviceName", g_app_config.device_name);
        
        // Feature enables
        cJSON_AddBoolToObject(root, "enableCards", g_app_config.enable_cards);
        cJSON_AddBoolToObject(root, "enableQr", g_app_config.enable_qr);
        cJSON_AddBoolToObject(root, "enableWifi", g_app_config.enable_wifi);
        cJSON_AddBoolToObject(root, "enableMqtt", g_app_config.enable_mqtt);
        
        // WiFi config (don't send password for security)
        cJSON_AddStringToObject(root, "wifiSsid", g_app_config.wifi_ssid);
        
        // MQTT config (don't send password for security)
        cJSON_AddStringToObject(root, "mqttHost", g_app_config.mqtt_host);
        cJSON_AddNumberToObject(root, "mqttPort", g_app_config.mqtt_port);
        cJSON_AddStringToObject(root, "mqttUser", g_app_config.mqtt_user);
        cJSON_AddStringToObject(root, "mqttTopicRoot", g_app_config.mqtt_topic_root);
        
        // GPIO pins - RC522
        cJSON *gpioRc522 = cJSON_CreateObject();
        cJSON_AddNumberToObject(gpioRc522, "mosi", g_app_config.rc522_pin_mosi);
        cJSON_AddNumberToObject(gpioRc522, "miso", g_app_config.rc522_pin_miso);
        cJSON_AddNumberToObject(gpioRc522, "sck", g_app_config.rc522_pin_sck);
        cJSON_AddNumberToObject(gpioRc522, "ss1", g_app_config.rc522_pin_ss1);
        cJSON_AddNumberToObject(gpioRc522, "rst1", g_app_config.rc522_pin_rst1);
        cJSON_AddNumberToObject(gpioRc522, "ss2", g_app_config.rc522_pin_ss2);
        cJSON_AddNumberToObject(gpioRc522, "rst2", g_app_config.rc522_pin_rst2);
        cJSON_AddItemToObject(root, "gpioRc522", gpioRc522);
        
        // GPIO pins - other
        cJSON_AddNumberToObject(root, "tornInPin", g_app_config.torn_in_pin);
        cJSON_AddNumberToObject(root, "tornOutPin", g_app_config.torn_out_pin);
        cJSON_AddNumberToObject(root, "buzzerPin", g_app_config.buzzer_pin);
        
        // GPIO pins - QR
        cJSON *gpioQr = cJSON_CreateObject();
        cJSON_AddNumberToObject(gpioQr, "tx", g_app_config.qr_uart_tx);
        cJSON_AddNumberToObject(gpioQr, "rx", g_app_config.qr_uart_rx);
        cJSON_AddNumberToObject(gpioQr, "uartNum", g_app_config.qr_uart_num);
        cJSON_AddNumberToObject(gpioQr, "baudRate", g_app_config.qr_baud_rate);
        cJSON_AddItemToObject(root, "gpioQr", gpioQr);

        app_config_unlock();

        char *json = cJSON_PrintUnformatted(root);
        if (json) {
            mqtt_enqueue(TOPIC_RESP_FIXED, json, 1, 0);
            cJSON_free(json);
        }
        cJSON_Delete(root);
    } else if (strcmp(cmd->action, "setConfig") == 0) {

        // El payload completo del mensaje MQTT está en cmd->payload
        cJSON *root = cJSON_Parse(cmd->payload);
        if (!root) {
            ESP_LOGW(TAG, "setConfig: JSON invalido");
            return;
        }

        cJSON *cfg = cJSON_GetObjectItem(root, "config");
        cJSON *idPetItem = cJSON_GetObjectItem(root, "idPeticion");

        const char *id_pet = (cJSON_IsString(idPetItem) ? idPetItem->valuestring : "-");

        if (!cJSON_IsObject(cfg)) {
            ESP_LOGW(TAG, "setConfig: campo 'config' no valido");
            cJSON_Delete(root);
            return;
        }

        // Thread-safe configuration write
        if (app_config_lock() != ESP_OK) {
            ESP_LOGW(TAG, "setConfig: Failed to acquire config lock");
            cJSON_Delete(root);
            return;
        }

        // Track if critical fields changed (requiring restart)
        bool needs_restart = false;
        cJSON *item;
        
        // Device info
        item = cJSON_GetObjectItem(cfg, "deviceId");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.device_id, item->valuestring, 
                                     sizeof(g_app_config.device_id));
        }
        
        item = cJSON_GetObjectItem(cfg, "deviceName");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.device_name, item->valuestring, 
                                     sizeof(g_app_config.device_name));
        }
        
        // Feature enables
        item = cJSON_GetObjectItem(cfg, "enableCards");
        if (cJSON_IsBool(item)) {
            g_app_config.enable_cards = cJSON_IsTrue(item);
        }
        
        item = cJSON_GetObjectItem(cfg, "enableQr");
        if (cJSON_IsBool(item)) {
            g_app_config.enable_qr = cJSON_IsTrue(item);
        }
        
        item = cJSON_GetObjectItem(cfg, "enableWifi");
        if (cJSON_IsBool(item)) {
            g_app_config.enable_wifi = cJSON_IsTrue(item);
        }
        
        item = cJSON_GetObjectItem(cfg, "enableMqtt");
        if (cJSON_IsBool(item)) {
            g_app_config.enable_mqtt = cJSON_IsTrue(item);
        }
        
        // WiFi config
        item = cJSON_GetObjectItem(cfg, "wifiSsid");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.wifi_ssid, item->valuestring, 
                                     sizeof(g_app_config.wifi_ssid));
            needs_restart = true;  // WiFi credentials changed
        }
        
        item = cJSON_GetObjectItem(cfg, "wifiPass");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.wifi_pass, item->valuestring, 
                                     sizeof(g_app_config.wifi_pass));
            needs_restart = true;  // WiFi credentials changed
        }
        
        // MQTT config
        item = cJSON_GetObjectItem(cfg, "mqttHost");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.mqtt_host, item->valuestring, 
                                     sizeof(g_app_config.mqtt_host));
            needs_restart = true;  // MQTT broker changed
        }
        
        item = cJSON_GetObjectItem(cfg, "mqttPort");
        if (cJSON_IsNumber(item)) {
            int port = item->valueint;
            if (is_valid_port(port)) {
                g_app_config.mqtt_port = port;
                needs_restart = true;  // MQTT port changed
            } else {
                ESP_LOGW(TAG, "Invalid MQTT port: %d (ignored)", port);
            }
        }
        
        item = cJSON_GetObjectItem(cfg, "mqttUser");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.mqtt_user, item->valuestring, 
                                     sizeof(g_app_config.mqtt_user));
            needs_restart = true;  // MQTT credentials changed
        }
        
        item = cJSON_GetObjectItem(cfg, "mqttPass");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.mqtt_pass, item->valuestring, 
                                     sizeof(g_app_config.mqtt_pass));
            needs_restart = true;  // MQTT credentials changed
        }
        
        item = cJSON_GetObjectItem(cfg, "mqttTopicRoot");
        if (cJSON_IsString(item)) {
            app_config_safe_str_copy(g_app_config.mqtt_topic_root, item->valuestring, 
                                     sizeof(g_app_config.mqtt_topic_root));
            needs_restart = true;  // MQTT topic changed
        }
        
        // GPIO pins - RC522 (with validation)
        cJSON *gpioRc522 = cJSON_GetObjectItem(cfg, "gpioRc522");
        if (cJSON_IsObject(gpioRc522)) {
            item = cJSON_GetObjectItem(gpioRc522, "mosi");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.rc522_pin_mosi = item->valueint;
                needs_restart = true;  // GPIO changed
            }
            
            item = cJSON_GetObjectItem(gpioRc522, "miso");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.rc522_pin_miso = item->valueint;
                needs_restart = true;
            }
            
            item = cJSON_GetObjectItem(gpioRc522, "sck");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.rc522_pin_sck = item->valueint;
                needs_restart = true;
            }
            
            item = cJSON_GetObjectItem(gpioRc522, "ss1");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.rc522_pin_ss1 = item->valueint;
                needs_restart = true;
            }
            
            item = cJSON_GetObjectItem(gpioRc522, "rst1");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.rc522_pin_rst1 = item->valueint;
                needs_restart = true;
            }
            
            item = cJSON_GetObjectItem(gpioRc522, "ss2");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.rc522_pin_ss2 = item->valueint;
                needs_restart = true;
            }
            
            item = cJSON_GetObjectItem(gpioRc522, "rst2");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.rc522_pin_rst2 = item->valueint;
                needs_restart = true;
            }
        }
        
        // GPIO pins - other (with validation)
        item = cJSON_GetObjectItem(cfg, "tornInPin");
        if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
            g_app_config.torn_in_pin = item->valueint;
        }
        
        item = cJSON_GetObjectItem(cfg, "tornOutPin");
        if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
            g_app_config.torn_out_pin = item->valueint;
        }
        
        item = cJSON_GetObjectItem(cfg, "buzzerPin");
        if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
            g_app_config.buzzer_pin = item->valueint;
        }
        
        // GPIO pins - QR (with validation)
        cJSON *gpioQr = cJSON_GetObjectItem(cfg, "gpioQr");
        if (cJSON_IsObject(gpioQr)) {
            item = cJSON_GetObjectItem(gpioQr, "tx");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.qr_uart_tx = item->valueint;
                needs_restart = true;  // UART GPIO changed
            }
            
            item = cJSON_GetObjectItem(gpioQr, "rx");
            if (cJSON_IsNumber(item) && is_valid_gpio(item->valueint)) {
                g_app_config.qr_uart_rx = item->valueint;
                needs_restart = true;
            }
            
            item = cJSON_GetObjectItem(gpioQr, "uartNum");
            if (cJSON_IsNumber(item) && is_valid_uart_num(item->valueint)) {
                g_app_config.qr_uart_num = item->valueint;
                needs_restart = true;
            }
            
            item = cJSON_GetObjectItem(gpioQr, "baudRate");
            if (cJSON_IsNumber(item) && item->valueint > 0) {
                g_app_config.qr_baud_rate = item->valueint;
                needs_restart = true;
            }
        }

        // Unlock before save (save will re-lock)
        app_config_unlock();

        // Save configuration to NVS
        esp_err_t err = app_config_save();
        bool save_ok = (err == ESP_OK);
        
        if (needs_restart) {
            ESP_LOGW(TAG, "Critical configuration changed, restart recommended");
        } else {
            ESP_LOGI(TAG, "Configuration updated");
        }

        // Respuesta
        cJSON *resp = cJSON_CreateObject();
        if (resp) {
            cJSON_AddStringToObject(resp, "action", "retornoSetConfig");
            cJSON_AddBoolToObject(resp, "ok", save_ok);
            cJSON_AddStringToObject(resp, "message", save_ok ? "Config saved" : "Error saving config");
            cJSON_AddBoolToObject(resp, "needsRestart", needs_restart);
            cJSON_AddBoolToObject(resp, "enableCards", g_app_config.enable_cards);
            cJSON_AddBoolToObject(resp, "enableQr", g_app_config.enable_qr);
            cJSON_AddStringToObject(resp, "idPeticion", id_pet);
            cJSON_AddStringToObject(resp, "id", device_id);

            char *json = cJSON_PrintUnformatted(resp);
            if (json) {
                mqtt_enqueue(TOPIC_RESP_FIXED, json, 1, 0);
                cJSON_free(json);
            }
            cJSON_Delete(resp);
        }

        cJSON_Delete(root);
    } else if (strcmp(cmd->action, "resetConfig") == 0) {
        
        // Reset configuration to defaults
        cJSON *root = cJSON_Parse(cmd->payload);
        const char *id_pet = "-";
        
        if (root) {
            cJSON *idPetItem = cJSON_GetObjectItem(root, "idPeticion");
            if (cJSON_IsString(idPetItem)) {
                id_pet = idPetItem->valuestring;
            }
            cJSON_Delete(root);
        }
        
        esp_err_t err = app_config_reset_to_defaults();
        bool reset_ok = (err == ESP_OK);
        
        ESP_LOGI(TAG, "Configuration reset to defaults. Restart recommended.");
        
        // Respuesta
        cJSON *resp = cJSON_CreateObject();
        if (resp) {
            cJSON_AddStringToObject(resp, "action", "retornoResetConfig");
            cJSON_AddBoolToObject(resp, "ok", reset_ok);
            cJSON_AddStringToObject(resp, "message", reset_ok ? "Config reset to defaults" : "Error resetting config");
            cJSON_AddStringToObject(resp, "idPeticion", id_pet);
            cJSON_AddStringToObject(resp, "id", device_id);

            char *json = cJSON_PrintUnformatted(resp);
            if (json) {
                mqtt_enqueue(TOPIC_RESP_FIXED, json, 1, 0);
                cJSON_free(json);
            }
            cJSON_Delete(resp);
        }
        
    } else if (strcmp(cmd->action, "status_now") == 0) {
        publish_status_now(cmd->id_peticion);

    } else if (strcmp(cmd->action, "writeCard") == 0) {
        // 1) Parsear JSON del comando
        //    Cambia cmd->payload por el campo real que tenga el JSON
        cJSON *root = cJSON_Parse(cmd->payload);
        if (!root) {
            ESP_LOGW(TAG, "writeCard: JSON invalido en payload");
            return;
        }

        cJSON *idUserItem     = cJSON_GetObjectItem(root, "idUser");
        cJSON *idPetItem      = cJSON_GetObjectItem(root, "idPeticion");

        if (!cJSON_IsString(idUserItem) || !cJSON_IsString(idPetItem)) {
            ESP_LOGW(TAG, "writeCard: faltan idUser o idPeticion en JSON");
            cJSON_Delete(root);
            return;
        }

        const char *id_user     = idUserItem->valuestring;
        const char *id_peticion = idPetItem->valuestring;

        ESP_LOGI(TAG, "writeCard recibido: idUser='%s' idPeticion='%s'",
                id_user, id_peticion);

        // 2) Escribir tarjeta en lector de salida (OUT)
        char uid_hex[16] = {0};
        bool ok = rc522_write_card_out_block8(id_user,
                                            uid_hex, sizeof(uid_hex),
                                            7000);   // timeout 7s

        // 3) Construir retornoWriteCard
        cJSON *resp = cJSON_CreateObject();
        if (!resp) {
            ESP_LOGW(TAG, "writeCard: no se pudo crear JSON resp");
            cJSON_Delete(root);
            return;
        }

        cJSON_AddStringToObject(resp, "action", "retornoWriteCard");
        cJSON_AddBoolToObject  (resp, "ok",      ok);
        cJSON_AddStringToObject(resp, "lector",  "OUT");
        cJSON_AddStringToObject(resp, "uid",     uid_hex);
        cJSON_AddStringToObject(resp, "user",    id_user);
        cJSON_AddStringToObject(resp, "idPeticion", id_peticion);

        char *json_str = cJSON_PrintUnformatted(resp);
        if (json_str) {
            // Usamos el mismo topic fijo que ya usas en rc522_reader.c
            bool enq_ok = mqtt_enqueue(TOPIC_RESP_FIXED, json_str, 1, 0);
            if (!enq_ok) {
                ESP_LOGW(TAG, "writeCard: no se pudo encolar retornoWriteCard");
            }
            cJSON_free(json_str);
        } else {
            ESP_LOGW(TAG, "writeCard: cJSON_PrintUnformatted fallo");
        }

        cJSON_Delete(resp);
        cJSON_Delete(root);
        } else if (strcmp(cmd->action, "hasAccess") == 0) {

        ESP_LOGI(TAG, "hasAccess: result=%s type=%s idPeticion=%s",
                 cmd->result, cmd->type, cmd->id_peticion);

                 rc522_access_gate_release();

        // Normalizamos: consideramos acceso OK si result es "true" (o "1"/"OK" si quieres)
        bool access_ok = false;
        if (cmd->result[0] != '\0') {
            if (strcasecmp(cmd->result, "true") == 0 ||
                strcmp(cmd->result, "1") == 0 ||
                strcasecmp(cmd->result, "ok") == 0) {
                access_ok = true;
            }
        }

        if (access_ok) {
            // ✅ Acceso concedido → abrir torno
            if (strcmp(cmd->type, "IN") == 0) {
                ESP_LOGI(TAG, "Acceso OK (IN), abriendo entrada");
                pulsar_gpio_blocking(TORN_IN_PIN, 2000, ENTRADA_INVERSO);
            } else if (strcmp(cmd->type, "OUT") == 0) {
                ESP_LOGI(TAG, "Acceso OK (OUT), abriendo salida");
                pulsar_gpio_blocking(TORN_OUT_PIN, 2000, ENTRADA_INVERSO);
            } else {
                ESP_LOGW(TAG, "hasAccess con type desconocido: %s", cmd->type);
            }
        } else {
            // ❌ Acceso denegado → activar pito en GPIO 21
            ESP_LOGI(TAG, "Acceso denegado, activando pito en GPIO 21");

            // Un pitido doble cortito, por ejemplo
            pulsar_gpio_blocking(PITO_DENEGADO_PIN, 150, false);
            vTaskDelay(pdMS_TO_TICKS(100));
            pulsar_gpio_blocking(PITO_DENEGADO_PIN, 150, false);
        }

        // Enviar confirmación a la web: retornoAccessTorn (siempre)
        cJSON *resp = cJSON_CreateObject();
        if (!resp) {
            ESP_LOGW(TAG, "hasAccess: no se pudo crear JSON resp");
            return;
        }

        cJSON_AddStringToObject(resp, "action",     "retornoAccessTorn");
        cJSON_AddStringToObject(resp, "idPeticion", cmd->id_peticion);
        cJSON_AddBoolToObject  (resp, "ok",         access_ok);
        cJSON_AddStringToObject(resp, "type",       cmd->type);

        char *json_str = cJSON_PrintUnformatted(resp);
        if (json_str) {
            bool enq_ok = mqtt_enqueue(TOPIC_RESP_FIXED, json_str, 1, 0);
            if (!enq_ok) {
                ESP_LOGW(TAG, "hasAccess: no se pudo encolar retornoAccessTorn");
            }
            cJSON_free(json_str);
        } else {
            ESP_LOGW(TAG, "hasAccess: cJSON_PrintUnformatted fallo");
        }

        cJSON_Delete(resp);

    }
    else {
        ESP_LOGW(TAG, "Accion desconocida: %s", cmd->action);
    }
}

// ================== TASK DE COMANDOS ==================

static void gpio_command_task(void *pv)
{
    command_t cmd;
    while (1) {
        if (xQueueReceive(cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            handle_command(&cmd);
        }
    }
}

void commands_start_task(void)
{
    xTaskCreate(gpio_command_task, "gpio_command_task", 4096, NULL, 5, NULL);
}
