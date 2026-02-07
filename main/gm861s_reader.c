// gm861s_reader.c
#include "gm861s_reader.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "core.h"
#include "mqtt_manager.h"
#include "config.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "GM861S";

#define UART_BUF_SZ        1024
#define QR_DEBOUNCE_MS     1200

// ===== DEBUG =====
// 1 -> loguea RX bytes + hexdump
#define GM861S_LOG_RX_HEX  1

static TaskHandle_t s_task = NULL;

typedef struct {
    char last[256];
    int64_t last_ts_us;
} qr_debounce_t;

static qr_debounce_t s_db = {0};

#define GM861S_ZONE_SERIAL_OUT   0x0060
#define GM861S_SERIAL_CFG_VALUE  0x21 // with protocol + CRLF + tail enabled

static bool is_printable_ascii(const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

static bool looks_like_url(const char *s) {
    return (strstr(s, "http://") == s) || (strstr(s, "https://") == s);
}

static void gm861s_send_cmd(const uint8_t *cmd, size_t len)
{
    int w = uart_write_bytes(GM861S_UART_PORT, (const char *)cmd, len);
    ESP_LOGI(TAG, "TX cmd %d bytes", w);
    ESP_LOG_BUFFER_HEXDUMP(TAG, cmd, len, ESP_LOG_INFO);
}

static void gm861s_write_zone(uint16_t addr, uint8_t value)
{
    uint8_t cmd[] = {
        0x7E, 0x00, 0x08, 0x01,
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        value,
        0xAB, 0xCD
    };
    gm861s_send_cmd(cmd, sizeof(cmd));
}

static void gm861s_apply_prod_config(void)
{
    ESP_LOGI(TAG, "Aplicando config salida serie: zone 0x%04X = 0x%02X",
             GM861S_ZONE_SERIAL_OUT, GM861S_SERIAL_CFG_VALUE);

    gm861s_write_zone(GM861S_ZONE_SERIAL_OUT, GM861S_SERIAL_CFG_VALUE);

    // Dale un pelín de tiempo y limpia lo que haya quedado de respuestas binarias
    vTaskDelay(pdMS_TO_TICKS(80));
    uart_flush_input(GM861S_UART_PORT);
}

static bool should_publish_qr(const char *text)
{
    int64_t now = esp_timer_get_time();
    int64_t dt_ms = (now - s_db.last_ts_us) / 1000;

    if (s_db.last[0] != '\0' &&
        strcmp(s_db.last, text) == 0 &&
        dt_ms < QR_DEBOUNCE_MS) {
        return false;
    }

    strncpy(s_db.last, text, sizeof(s_db.last) - 1);
    s_db.last[sizeof(s_db.last) - 1] = '\0';
    s_db.last_ts_us = now;
    return true;
}

// ===== PARSER: protocolo <0x03><len><data> =====
static bool try_parse_protocol_frame(uint8_t *buf, int *len, char *out, size_t out_sz)
{
    if (*len < 2) return false;

    // Buscar 0x03
    int stx = -1;
    for (int i = 0; i < *len; i++) {
        if (buf[i] == 0x03) { stx = i; break; }
    }
    if (stx < 0) {
        // no hay STX -> no tiramos todo el buffer; podría ser binario o parcial
        // pero si se llena de basura, podríamos recortar. Por ahora, conservador:
        return false;
    }

    // Descartar basura antes de 0x03
    if (stx > 0) {
        memmove(buf, buf + stx, *len - stx);
        *len -= stx;
        if (*len < 2) return false;
    }

    uint8_t pay_len = buf[1];
    if (pay_len == 0 || pay_len > 250) {
        // frame raro, descartar 0x03 y seguir
        memmove(buf, buf + 1, *len - 1);
        *len -= 1;
        return false;
    }

    if (*len < (2 + pay_len)) return false; // esperar más

    int copy = (pay_len < (out_sz - 1)) ? pay_len : (out_sz - 1);
    memcpy(out, &buf[2], copy);
    out[copy] = '\0';

    // Consumir frame completo
    int consumed = 2 + pay_len;
    memmove(buf, buf + consumed, *len - consumed);
    *len -= consumed;

    return true;
}

static bool try_parse_line(uint8_t *buf, int *len, char *out, size_t out_sz)
{
    for (int i = 0; i < *len; i++) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') {
            int n = i;

            // Trim final
            while (n > 0 && (buf[n-1] == '\r' || buf[n-1] == '\n' || buf[n-1] == '\t' || buf[n-1] == ' '))
                n--;

            // **Nuevo**: valida que sea mayormente ASCII imprimible
            int printable = 0;
            for (int k = 0; k < n; k++) {
                if (buf[k] >= 0x20 && buf[k] <= 0x7E) printable++;
            }
            // si menos del 80% es imprimible, lo descartamos como "línea binaria"
            if (n == 0 || printable * 100 / n < 80) {
                // consumir separadores y descartar
                int j = i + 1;
                while (j < *len && (buf[j] == '\n' || buf[j] == '\r' || buf[j] == '\t')) j++;
                memmove(buf, buf + j, *len - j);
                *len -= j;
                return false;
            }

            int copy = (n < (int)out_sz - 1) ? n : (int)out_sz - 1;
            memcpy(out, buf, copy);
            out[copy] = '\0';

            int j = i + 1;
            while (j < *len && (buf[j] == '\n' || buf[j] == '\r' || buf[j] == '\t')) j++;
            memmove(buf, buf + j, *len - j);
            *len -= j;

            return copy > 0;
        }
    }
    return false;
}

// Fallback: busca la “mejor” racha ASCII imprimible (>=4 chars)
static bool extract_printable(uint8_t *buf, int *len, char *out, size_t out_sz)
{
    int best_s = -1, best_e = -1;
    int i = 0;

    while (i < *len) {
        while (i < *len && (buf[i] < 0x20 || buf[i] > 0x7E)) i++;
        int s = i;
        while (i < *len && (buf[i] >= 0x20 && buf[i] <= 0x7E)) i++;
        int e = i;

        if (e - s >= 4 && (best_s < 0 || (e - s) > (best_e - best_s))) {
            best_s = s;
            best_e = e;
        }
    }

    if (best_s < 0) return false;

    int n = best_e - best_s;
    if (n > (int)out_sz - 1) n = (int)out_sz - 1;
    memcpy(out, buf + best_s, n);
    out[n] = '\0';

    // Consume hasta best_e para no quedarnos atascados
    memmove(buf, buf + best_e, *len - best_e);
    *len -= best_e;

    return true;
}

static void publish_qr_event(const char *qr_text)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "action", "getAccessTorn");
    cJSON_AddStringToObject(root, "type",   "QR");
    cJSON_AddStringToObject(root, "cardId", qr_text);
    cJSON_AddStringToObject(root, "user",   "");
    cJSON_AddStringToObject(root, "name",   device_id);
    cJSON_AddStringToObject(root, "idTorno", id_torno);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        ESP_LOGI(TAG, "QR -> '%s'", qr_text);
        ESP_LOGI(TAG, "MQTT enqueue -> topic='%s' payload=%s", TOPIC_RESP_FIXED, json);

        mqtt_enqueue(TOPIC_RESP_FIXED, json, 1, 0);
        cJSON_free(json);
    } else {
        ESP_LOGW(TAG, "No se pudo serializar JSON");
    }

    cJSON_Delete(root);
}

static void gm861s_task(void *pv)
{
    (void)pv;

    uint8_t rx[UART_BUF_SZ];
    int rx_len = 0;

    ESP_LOGI(TAG, "GM861S task (UART=%d TX=%d RX=%d baud=%d)",
             GM861S_UART_PORT, GM861S_UART_TX, GM861S_UART_RX, GM861S_BAUD);

    while (1) {
        uint8_t tmp[128];
        int n = uart_read_bytes(GM861S_UART_PORT, tmp, sizeof(tmp), pdMS_TO_TICKS(200));

        if (n > 0) {
            #if GM861S_LOG_RX_HEX
                ESP_LOGI(TAG, "RX %d bytes", n);
                ESP_LOG_BUFFER_HEXDUMP(TAG, tmp, n, ESP_LOG_INFO);
            #endif
            if (rx_len > 0) ESP_LOGI(TAG, "rx_len=%d", rx_len);
            int space = UART_BUF_SZ - rx_len;
            int to_copy = (n < space) ? n : space;
            if (to_copy > 0) {
                memcpy(rx + rx_len, tmp, to_copy);
                rx_len += to_copy;
            } else {
                // buffer lleno -> resetea para evitar quedarse colgado
                rx_len = 0;
            }
        }

        // Procesa “todos los mensajes posibles” acumulados en rx
        while (1) {
            char qr[256] = {0};
            bool got = false;
            const char *src = NULL;

            if (try_parse_protocol_frame(rx, &rx_len, qr, sizeof(qr))) {
                got = true; src = "PROTO";
            } else if (try_parse_line(rx, &rx_len, qr, sizeof(qr))) {
                got = true; src = "LINE";
            }

            // Si quieres mantener extract_printable, ponlo SOLO cuando el buffer crezca mucho
            // y no haya forma de parsear:
            // else if (rx_len > 200 && extract_printable(rx, &rx_len, qr, sizeof(qr))) { ... }

            if (!got) break;
            if (qr[0] == '\0') continue;

            ESP_LOGI(TAG, "QR detectado (%s): '%s'", src, qr);

            if (!should_publish_qr(qr)) {
                ESP_LOGI(TAG, "QR repetido (debounce), ignorado");
                continue;
            }

            if (s_mqtt_connected) {
                publish_qr_event(qr);
            } else {
                ESP_LOGW(TAG, "MQTT no conectado -> no publico, pero QR detectado");
            }
        }


        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t gm861s_reader_init(void)
{
    uart_config_t cfg = {
        .baud_rate = GM861S_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    ESP_ERROR_CHECK(uart_driver_install(GM861S_UART_PORT, 4096, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GM861S_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GM861S_UART_PORT, GM861S_UART_TX, GM861S_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Importante: flush después de instalar/configurar
    uart_flush_input(GM861S_UART_PORT);
    gm861s_apply_prod_config();

    return ESP_OK;
}

void gm861s_reader_start_task(void)
{
    if (!s_task) {
        xTaskCreate(gm861s_task, "gm861s_task", 4096, NULL, 8, &s_task);
    }
}
