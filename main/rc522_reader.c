// rc522_reader.c

#include "esp_timer.h"
#include "rc522_reader.h"
#include "config.h"
#include "core.h"
#include "mqtt_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include "freertos/semphr.h"

#define ACCESS_IN_FLIGHT_TIMEOUT_MS 3000

static bool s_access_in_flight = false;
static int64_t s_access_in_flight_ts_us = 0;

static portMUX_TYPE s_gate_mux = portMUX_INITIALIZER_UNLOCKED;

static bool access_gate_try_acquire(void)
{
    bool ok = false;
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_gate_mux);

    int64_t dt_ms = (now - s_access_in_flight_ts_us) / 1000;
    if (s_access_in_flight && dt_ms > ACCESS_IN_FLIGHT_TIMEOUT_MS) {
        s_access_in_flight = false;
    }

    if (!s_access_in_flight) {
        s_access_in_flight = true;
        s_access_in_flight_ts_us = now;
        ok = true;
    }

    portEXIT_CRITICAL(&s_gate_mux);
    return ok;
}

void rc522_access_gate_release(void)
{
    portENTER_CRITICAL(&s_gate_mux);
    s_access_in_flight = false;
    portEXIT_CRITICAL(&s_gate_mux);
}


#define CARD_DEBOUNCE_MS 900   // ajusta: 500–1500 suele ir bien

typedef struct {
    char last_uid[16];
    int64_t last_ts_us;
    bool card_present;
} reader_debounce_t;

static reader_debounce_t s_db_in  = {0};
static reader_debounce_t s_db_out = {0};

static bool should_publish(reader_debounce_t *db, const char *uid_hex)
{
    int64_t now = esp_timer_get_time();
    int64_t dt_ms = (now - db->last_ts_us) / 1000;

    if (db->last_uid[0] != '\0' &&
        strcmp(db->last_uid, uid_hex) == 0 &&
        dt_ms < CARD_DEBOUNCE_MS) {
        return false;
    }

    // aceptar y actualizar
    strncpy(db->last_uid, uid_hex, sizeof(db->last_uid) - 1);
    db->last_uid[sizeof(db->last_uid)-1] = '\0';
    db->last_ts_us = now;
    db->card_present = true;
    return true;
}

static void mark_no_card(reader_debounce_t *db)
{
    db->card_present = false;
    // opcional: no borres last_uid, así sigue sirviendo para debounce temporal
}

static bool s_last_in_ok = true;
static bool s_last_out_ok = true;

bool rc522_last_in_ok()  { return s_last_in_ok; }
bool rc522_last_out_ok() { return s_last_out_ok; }

static SemaphoreHandle_t s_rc522_mutex = NULL;

static inline void rc522_lock(void)
{
    if (s_rc522_mutex) {
        xSemaphoreTake(s_rc522_mutex, portMAX_DELAY);
    }
}

static inline void rc522_unlock(void)
{
    if (s_rc522_mutex) {
        xSemaphoreGive(s_rc522_mutex);
    }
}


static const char *TAG = "RC522_READER";

static spi_device_handle_t s_rc522_1 = NULL;  // entrada
static spi_device_handle_t s_rc522_2 = NULL;  // salida

// ====== Registros MFRC522 (RC522) ======
#define RC522_REG_COMMAND       0x01
#define RC522_REG_COMM_IEN      0x02
#define RC522_REG_COMM_IRQ      0x04
#define RC522_REG_DIV_IRQ       0x05
#define RC522_REG_ERROR         0x06
#define RC522_REG_STATUS2       0x08
#define RC522_REG_FIFO_DATA     0x09
#define RC522_REG_FIFO_LEVEL    0x0A
#define RC522_REG_CONTROL       0x0C
#define RC522_REG_BIT_FRAMING   0x0D
#define RC522_REG_MODE          0x11
#define RC522_REG_TX_CONTROL    0x14
#define RC522_REG_TX_ASK        0x15
#define RC522_REG_CRC_RESULT_H  0x21
#define RC522_REG_CRC_RESULT_L  0x22
#define RC522_REG_T_MODE        0x2A
#define RC522_REG_T_PRESCALER   0x2B
#define RC522_REG_T_RELOAD_H    0x2C
#define RC522_REG_T_RELOAD_L    0x2D
#define RC522_REG_VERSION       0x37

// Comandos PCD (lector)
#define PCD_IDLE        0x00
#define PCD_AUTHENT     0x0E
#define PCD_RECEIVE     0x08
#define PCD_TRANSMIT    0x04
#define PCD_TRANSCEIVE  0x0C
#define PCD_SOFTRESET   0x0F
#define PCD_CALCCRC     0x03

// Comandos PICC (tarjeta)
#define PICC_REQIDL     0x26
#define PICC_ANTICOLL   0x93
#define PICC_HALT       0x50
#define PICC_READ       0x30
#define PICC_WRITE      0xA0

#define MI_OK        0
#define MI_NOTAGERR  1
#define MI_ERR       2

static const uint8_t KEY_DEFAULT[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ================== Bajo nivel SPI RC522 ==================

static esp_err_t rc522_spi_transmit(spi_device_handle_t dev, spi_transaction_t *t);

// Escribir registro
static esp_err_t rc522_write_reg(spi_device_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];
    buf[0] = (reg << 1) & 0x7E;   // addr + write
    buf[1] = val;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length   = 16;
    t.tx_buffer = buf;
    t.rx_buffer = NULL;

    return rc522_spi_transmit(dev, &t);
}


static esp_err_t rc522_spi_transmit(spi_device_handle_t dev, spi_transaction_t *t)
{
    rc522_lock();
    esp_err_t ret = spi_device_transmit(dev, t);
    rc522_unlock();
    return ret;
}

// Leer registro
static uint8_t rc522_read_reg(spi_device_handle_t dev, uint8_t reg)
{
    uint8_t tx[2];
    uint8_t rx[2];

    tx[0] = 0x80 | ((reg << 1) & 0x7E);  // addr + read
    tx[1] = 0x00;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length   = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    if (rc522_spi_transmit(dev, &t) != ESP_OK) {
        return 0;
    }
    return rx[1];
}


static void rc522_set_bit_mask(spi_device_handle_t dev, uint8_t reg, uint8_t mask)
{
    uint8_t tmp = rc522_read_reg(dev, reg);
    rc522_write_reg(dev, reg, tmp | mask);
}

static void rc522_clear_bit_mask(spi_device_handle_t dev, uint8_t reg, uint8_t mask)
{
    uint8_t tmp = rc522_read_reg(dev, reg);
    rc522_write_reg(dev, reg, tmp & (~mask));
}

// Encender antena
static void rc522_antenna_on(spi_device_handle_t dev)
{
    uint8_t v = rc522_read_reg(dev, RC522_REG_TX_CONTROL);
    if (!(v & 0x03)) {
        rc522_write_reg(dev, RC522_REG_TX_CONTROL, v | 0x03);
    }
}

// ================== Inicialización RC522 ==================

static void rc522_init_chip(spi_device_handle_t dev, const char *name)
{
    // Reset suave
    rc522_write_reg(dev, RC522_REG_COMMAND, PCD_SOFTRESET);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Timer config típica (como en tu Python)
    rc522_write_reg(dev, RC522_REG_T_MODE, 0x8D);
    rc522_write_reg(dev, RC522_REG_T_PRESCALER, 0x3E);
    rc522_write_reg(dev, RC522_REG_T_RELOAD_L, 30);
    rc522_write_reg(dev, RC522_REG_T_RELOAD_H, 0);

    // 100% ASK
    rc522_write_reg(dev, RC522_REG_TX_ASK, 0x40);

    // CRC preset 0x6363
    rc522_write_reg(dev, RC522_REG_MODE, 0x3D);

    rc522_antenna_on(dev);

    uint8_t ver = rc522_read_reg(dev, RC522_REG_VERSION);
    ESP_LOGI(TAG, "[%s] RC522 VersionReg=0x%02X", name, ver);
}

// ================== Transceive helper (para request/anticoll) ==================

static esp_err_t rc522_transceive(spi_device_handle_t dev,
                                  const uint8_t *send_data, uint8_t send_len,
                                  uint8_t *back_data, uint8_t *back_len)
{
    uint8_t irqEn  = 0x77;
    uint8_t waitIRq = 0x30; // RxIRq | IdleIRq

    rc522_write_reg(dev, RC522_REG_COMM_IEN, irqEn | 0x80);
    rc522_clear_bit_mask(dev, RC522_REG_COMM_IRQ, 0x80);
    rc522_set_bit_mask(dev, RC522_REG_FIFO_LEVEL, 0x80); // flush FIFO

    for (uint8_t i = 0; i < send_len; i++)
        rc522_write_reg(dev, RC522_REG_FIFO_DATA, send_data[i]);

    rc522_write_reg(dev, RC522_REG_COMMAND, PCD_TRANSCEIVE);
    rc522_set_bit_mask(dev, RC522_REG_BIT_FRAMING, 0x80); // StartSend

    uint16_t i = 2000;
    uint8_t n;
    do {
        n = rc522_read_reg(dev, RC522_REG_COMM_IRQ);
        i--;
    } while (i && !(n & (waitIRq | 0x01)));

    rc522_clear_bit_mask(dev, RC522_REG_BIT_FRAMING, 0x80); // StopSend

    if (i == 0) {
        ESP_LOGW(TAG, "Timeout transceive");
        return ESP_ERR_TIMEOUT;
    }

    uint8_t error = rc522_read_reg(dev, RC522_REG_ERROR);
    if (error & 0x1B) {
        ESP_LOGW(TAG, "ErrorReg=0x%02X en transceive", error);
        return ESP_FAIL;
    }

    uint8_t length = rc522_read_reg(dev, RC522_REG_FIFO_LEVEL);
    if (length > *back_len) length = *back_len;

    for (uint8_t j = 0; j < length; j++)
        back_data[j] = rc522_read_reg(dev, RC522_REG_FIFO_DATA);

    *back_len = length;
    return ESP_OK;
}

// ================== Alto nivel: Request + Anticollision ==================

static bool rc522_request(spi_device_handle_t dev,
                          uint8_t req_mode, uint8_t *atqa, uint8_t *atqa_len)
{
    rc522_write_reg(dev, RC522_REG_BIT_FRAMING, 0x07); // solo 7 bits
    uint8_t buf[1] = { req_mode };

    esp_err_t ret = rc522_transceive(dev, buf, 1, atqa, atqa_len);
    return (ret == ESP_OK && *atqa_len == 2);
}

static bool rc522_anticoll(spi_device_handle_t dev,
                           uint8_t *uid, uint8_t *uid_len)
{
    rc522_write_reg(dev, RC522_REG_BIT_FRAMING, 0x00);

    uint8_t cmd[2] = { PICC_ANTICOLL, 0x20 };
    uint8_t back[10] = {0};
    uint8_t back_len = sizeof(back);

    esp_err_t ret = rc522_transceive(dev, cmd, 2, back, &back_len);
    if (ret != ESP_OK || back_len < 5) {
        return false;
    }

    memcpy(uid, back, 4);   // UID 4 bytes
    *uid_len = 4;
    return true;
}

// ================== Helpers extra: CRC, AUTH, lectura bloque ==================

// Calcula CRC_A al estilo del driver MicroPython
static void rc522_calc_crc(spi_device_handle_t dev,
                           const uint8_t *data, uint8_t len,
                           uint8_t *out_crcL, uint8_t *out_crcH)
{
    // Clear CRCIRq
    rc522_clear_bit_mask(dev, RC522_REG_DIV_IRQ, 0x04);
    // Flush FIFO
    rc522_set_bit_mask(dev, RC522_REG_FIFO_LEVEL, 0x80);

    // Mete datos en FIFO
    for (uint8_t i = 0; i < len; i++) {
        rc522_write_reg(dev, RC522_REG_FIFO_DATA, data[i]);
    }

    // Lanza cálculo CRC
    rc522_write_reg(dev, RC522_REG_COMMAND, PCD_CALCCRC);

    // Espera a que termine CRC
    uint16_t i = 0xFF;
    uint8_t n;
    do {
        n = rc522_read_reg(dev, RC522_REG_DIV_IRQ);
        i--;
    } while (i && !(n & 0x04));  // bit CRCIRq

    *out_crcL = rc522_read_reg(dev, RC522_REG_CRC_RESULT_L);
    *out_crcH = rc522_read_reg(dev, RC522_REG_CRC_RESULT_H);
}


static bool rc522_to_card(spi_device_handle_t dev,
                          uint8_t command,
                          const uint8_t *send_data, uint8_t send_len,
                          uint8_t *back_data, uint8_t *back_len,
                          uint8_t *back_bits)
{
    uint8_t irq_en = 0x00;
    uint8_t wait_irq = 0x00;

    if (command == PCD_AUTHENT) {
        irq_en  = 0x12;  // como en MicroPython: ErrIrq + IdleIrq
        wait_irq = 0x10; // IdleIrq
    } else if (command == PCD_TRANSCEIVE) {
        irq_en  = 0x77;  // TxI, RxI, IdleI, ErrI, TimerI
        wait_irq = 0x30; // RxIRq | IdleIrq
    }

    // Habilita interrupciones
    rc522_write_reg(dev, RC522_REG_COMM_IEN, irq_en | 0x80);
    // Clear flags de IRQ
    rc522_clear_bit_mask(dev, RC522_REG_COMM_IRQ, 0x80);
    // Flush FIFO
    rc522_set_bit_mask(dev, RC522_REG_FIFO_LEVEL, 0x80);

    // IDLE
    rc522_write_reg(dev, RC522_REG_COMMAND, PCD_IDLE);

    // Carga datos en FIFO
    for (uint8_t i = 0; i < send_len; i++) {
        rc522_write_reg(dev, RC522_REG_FIFO_DATA, send_data[i]);
    }

    // Lanza comando
    rc522_write_reg(dev, RC522_REG_COMMAND, command);
    if (command == PCD_TRANSCEIVE) {
        rc522_set_bit_mask(dev, RC522_REG_BIT_FRAMING, 0x80); // StartSend
    }

    // Espera fin o timeout
    uint16_t i = 2000;
    uint8_t n;
    do {
        n = rc522_read_reg(dev, RC522_REG_COMM_IRQ);
        i--;
    } while (i && !(n & 0x01) && !(n & wait_irq));  // TimerIrq o wait_irq

    // StopSend
    rc522_clear_bit_mask(dev, RC522_REG_BIT_FRAMING, 0x80);

    if (i == 0) {
        ESP_LOGW(TAG, "rc522_to_card timeout (cmd=0x%02X)", command);
        return false;
    }

    uint8_t error = rc522_read_reg(dev, RC522_REG_ERROR);
    if (error & 0x1B) {
        ESP_LOGW(TAG, "rc522_to_card ErrorReg=0x%02X (cmd=0x%02X)", error, command);
        return false;
    }

    if (command == PCD_TRANSCEIVE && back_data && back_len && back_bits) {
        uint8_t length = rc522_read_reg(dev, RC522_REG_FIFO_LEVEL);
        if (length > *back_len) length = *back_len;

        for (uint8_t j = 0; j < length; j++) {
            back_data[j] = rc522_read_reg(dev, RC522_REG_FIFO_DATA);
        }

        *back_len = length;
        uint8_t last_bits = rc522_read_reg(dev, RC522_REG_CONTROL) & 0x07;
        if (last_bits)
            *back_bits = (length - 1) * 8 + last_bits;
        else
            *back_bits = length * 8;
    }

    return true;
}

// SELECT TAG: 0x93 0x70 + UID[4] + BCC + CRC_A
// Equivalente a _select_with_crc() de tu código MicroPython
static bool rc522_select(spi_device_handle_t dev, const uint8_t uid4[4])
{
    // Calculamos BCC como en Python: uid0^uid1^uid2^uid3
    uint8_t bcc = uid4[0] ^ uid4[1] ^ uid4[2] ^ uid4[3];

    uint8_t frame[9];
    frame[0] = 0x93;
    frame[1] = 0x70;
    frame[2] = uid4[0];
    frame[3] = uid4[1];
    frame[4] = uid4[2];
    frame[5] = uid4[3];
    frame[6] = bcc;

    // CRC_A sobre los 7 primeros bytes
    uint8_t crcL, crcH;
    rc522_calc_crc(dev, frame, 7, &crcL, &crcH);
    frame[7] = crcL;
    frame[8] = crcH;

    uint8_t back[4] = {0};
    uint8_t back_len  = sizeof(back);
    uint8_t back_bits = 0;

    bool ok = rc522_to_card(dev,
                            PCD_TRANSCEIVE,
                            frame, sizeof(frame),
                            back, &back_len, &back_bits);
    if (!ok) {
        ESP_LOGW(TAG, "SELECT fallo (rc522_to_card)");
        return false;
    }
    if (back_len < 1) {
        ESP_LOGW(TAG, "SELECT sin respuesta (len=%d)", back_len);
        return false;
    }

    uint8_t sak = back[0];
    ESP_LOGI(TAG, "SELECT OK, SAK=0x%02X", sak);
    return true;
}



static bool rc522_auth(spi_device_handle_t dev,
                       uint8_t key_mode,
                       uint8_t block_addr,
                       const uint8_t key[6],
                       const uint8_t uid4[4])
{
    uint8_t buf[12];
    buf[0] = key_mode;       // 0x60 = KeyA, 0x61 = KeyB
    buf[1] = block_addr;
    memcpy(&buf[2], key, 6); // 6 bytes de clave
    memcpy(&buf[8], uid4, 4);// 4 bytes de UID

    uint8_t dummy[2] = {0};
    uint8_t dummy_len = sizeof(dummy);
    uint8_t dummy_bits = 0;

    if (!rc522_to_card(dev, PCD_AUTHENT,
                       buf, sizeof(buf),
                       dummy, &dummy_len, &dummy_bits)) {
        ESP_LOGW(TAG, "rc522_to_card AUTH fallo (cmd MFAuthent)");
        return false;
    }

    uint8_t status2 = rc522_read_reg(dev, RC522_REG_STATUS2);
    if (status2 & 0x08) {
        // Crypto1 activo -> authenticated
        return true;
    } else {
        ESP_LOGW(TAG, "Status2Reg=0x%02X, no authenticated", status2);
        return false;
    }
}

static void rc522_stop_crypto(spi_device_handle_t dev)
{
    rc522_clear_bit_mask(dev, RC522_REG_STATUS2, 0x08);
}

static bool rc522_read_block(spi_device_handle_t dev,
                             uint8_t block_addr,
                             const uint8_t uid4[4],
                             uint8_t out_data[16])
{
    uint8_t keyA[6];
    memcpy(keyA, KEY_DEFAULT, 6);

    ESP_LOGD(TAG, "Intentando AUTH con KeyA en bloque %d", block_addr);
    bool authed = rc522_auth(dev, 0x60 /* Key A */, block_addr, keyA, uid4);
    if (!authed) {
        uint8_t keyB[6];
        memcpy(keyB, KEY_DEFAULT, 6);
        ESP_LOGD(TAG, "AUTH A fallo, probando KeyB en bloque %d", block_addr);
        authed = rc522_auth(dev, 0x61 /* Key B */, block_addr, keyB, uid4);
        if (!authed) {
            ESP_LOGW(TAG, "AUTH fallo en bloque %d (ni A ni B)", block_addr);
            rc522_stop_crypto(dev);
            return false;
        }
    }

    uint8_t cmd[2] = { 0x30 /* READ */, block_addr };
    uint8_t crcL, crcH;
    rc522_calc_crc(dev, cmd, 2, &crcL, &crcH);

    uint8_t frame[4] = { 0x30, block_addr, crcL, crcH };
    uint8_t back[32] = {0};
    uint8_t back_bytes = sizeof(back);
    uint8_t back_bits = 0;

    bool ok = rc522_to_card(dev, PCD_TRANSCEIVE,
                            frame, sizeof(frame),
                            back, &back_bytes, &back_bits);

    rc522_stop_crypto(dev);

    if (!ok) {
        ESP_LOGW(TAG, "rc522_to_card fallo leyendo bloque %d", block_addr);
        return false;
    }

    if (back_bits != 0x90 || back_bytes < 16) {
        ESP_LOGW(TAG, "Lectura bloque invalida: bits=%d bytes=%d (bloque %d)",
                 back_bits, back_bytes, block_addr);
        return false;
    }

    memcpy(out_data, back, 16);
    ESP_LOGD(TAG, "Bloque %d leido OK", block_addr);
    return true;
}

static bool rc522_write_block(spi_device_handle_t dev,
                              uint8_t block_addr,
                              const uint8_t uid4[4],
                              const uint8_t data16[16])
{
    uint8_t keyA[6];
    memcpy(keyA, KEY_DEFAULT, 6);

    ESP_LOGD(TAG, "Intentando AUTH (WRITE) con KeyA en bloque %d", block_addr);
    bool authed = rc522_auth(dev, 0x60 /* Key A */, block_addr, keyA, uid4);
    if (!authed) {
        uint8_t keyB[6];
        memcpy(keyB, KEY_DEFAULT, 6);
        ESP_LOGD(TAG, "AUTH A fallo (WRITE), probando KeyB en bloque %d", block_addr);
        authed = rc522_auth(dev, 0x61 /* Key B */, block_addr, keyB, uid4);
        if (!authed) {
            ESP_LOGW(TAG, "AUTH WRITE fallo en bloque %d (ni A ni B)", block_addr);
            rc522_stop_crypto(dev);
            return false;
        }
    }

    // 1) Comando WRITE
    uint8_t cmd[2] = { PICC_WRITE, block_addr };
    uint8_t crcL, crcH;
    rc522_calc_crc(dev, cmd, 2, &crcL, &crcH);

    uint8_t frame[4] = { PICC_WRITE, block_addr, crcL, crcH };
    uint8_t ack[4] = {0};
    uint8_t ack_len  = sizeof(ack);
    uint8_t ack_bits = 0;

    bool ok = rc522_to_card(dev, PCD_TRANSCEIVE,
                            frame, sizeof(frame),
                            ack, &ack_len, &ack_bits);
    if (!ok) {
        ESP_LOGW(TAG, "WRITE cmd fallo (rc522_to_card) bloque %d", block_addr);
        rc522_stop_crypto(dev);
        return false;
    }

    // Esperamos ACK: 4 bits 0x0A -> en la práctica data[0] & 0x0F == 0x0A
    if (ack_len < 1 || (ack[0] & 0x0F) != 0x0A) {
        ESP_LOGW(TAG, "WRITE cmd sin ACK valido (len=%d, ack0=0x%02X) bloque %d",
                 ack_len, ack[0], block_addr);
        rc522_stop_crypto(dev);
        return false;
    }

    // 2) Enviar los 16 bytes + CRC
    uint8_t data_frame[18];
    memcpy(data_frame, data16, 16);
    rc522_calc_crc(dev, data16, 16, &crcL, &crcH);
    data_frame[16] = crcL;
    data_frame[17] = crcH;

    uint8_t ack2[4] = {0};
    uint8_t ack2_len  = sizeof(ack2);
    uint8_t ack2_bits = 0;

    ok = rc522_to_card(dev, PCD_TRANSCEIVE,
                       data_frame, sizeof(data_frame),
                       ack2, &ack2_len, &ack2_bits);

    rc522_stop_crypto(dev);

    if (!ok) {
        ESP_LOGW(TAG, "WRITE data fallo (rc522_to_card) bloque %d", block_addr);
        return false;
    }
    if (ack2_len < 1 || (ack2[0] & 0x0F) != 0x0A) {
        ESP_LOGW(TAG, "WRITE data sin ACK valido (len=%d, ack0=0x%02X) bloque %d",
                 ack2_len, ack2[0], block_addr);
        return false;
    }

    ESP_LOGI(TAG, "Bloque %d escrito OK", block_addr);
    return true;
}

// ================== Helpers: texto limpio + lectora bloque8 ==================

static void clean_text_block(const uint8_t *in, size_t len,
                             char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }

    size_t w = 0;

    // Copiamos hasta 16 bytes o hasta 0x00, sin pasarnos del buffer
    for (size_t i = 0; i < len && w < out_size - 1; i++) {
        uint8_t c = in[i];
        if (c == 0x00) {
            break;  // fin de cadena
        }
        out[w++] = (char)c;
    }
    out[w] = '\0';

    // Trim right (espacios, tabs, saltos de línea al final)
    while (w > 0 && (out[w - 1] == ' ' ||
                     out[w - 1] == '\t' ||
                     out[w - 1] == '\r' ||
                     out[w - 1] == '\n')) {
        w--;
        out[w] = '\0';
    }

    // Trim left opcional: desplazamos si empieza con espacios
    size_t start = 0;
    while (out[start] == ' ' ||
           out[start] == '\t' ||
           out[start] == '\r' ||
           out[start] == '\n') {
        start++;
    }
    if (start > 0) {
        size_t i = 0;
        while (out[start + i] != '\0') {
            out[i] = out[start + i];
            i++;
        }
        out[i] = '\0';
    }
}


static bool rc522_read_card_block8(spi_device_handle_t dev,
                                   char *uid_str, size_t uid_str_size,
                                   char *user_buf, size_t user_buf_size)
{
    uint8_t atqa[2];
    uint8_t atqa_len = sizeof(atqa);

    if (!rc522_request(dev, PICC_REQIDL, atqa, &atqa_len)) {
        return false; // No hay tarjeta
    }

    uint8_t uid4[4];
    uint8_t uid_len = 0;
    if (!rc522_anticoll(dev, uid4, &uid_len)) {
        return false;
    }

    // --- LOG UID ---
    if (uid_len != 4) return false;
    char *p = uid_str;
    for (int i = 0; i < 4 && (size_t)((p - uid_str) + 2) < uid_str_size; i++) {
        sprintf(p, "%02X", uid4[i]);
        p += 2;
    }
    *p = '\0';

    ESP_LOGI(TAG, "Tarjeta detectada UID=%s, intentando leer bloque 8", uid_str);

    // AQUÍ VIENE LO IMPORTANTE: SELECT antes de AUTH
    if (!rc522_select(dev, uid4)) {
        ESP_LOGW(TAG, "SELECT fallo para UID=%s, no se puede autenticar", uid_str);
        return false;
    }

    // Leer bloque 8 usando AUTH + READ
    uint8_t block_data[16] = {0};
    if (!rc522_read_block(dev, 8, uid4, block_data)) {
        ESP_LOGW(TAG, "No se pudo leer bloque 8 para UID=%s (se enviara user=\"\")", uid_str);
        user_buf[0] = '\0';
        return true; // UID ok, pero sin user
    }

    // Limpiar texto del bloque 8 (trim)
    clean_text_block(block_data, 16, user_buf, user_buf_size);

    ESP_LOGI(TAG, "UID=%s  block8='%s'", uid_str, user_buf);
    return true;
}


// ================== MQTT + task ==================

static void publish_access_event(const char *type, const char *uid_hex, const char *user_text)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "action", "getAccessTorn");
    cJSON_AddStringToObject(root, "type",   type);        // "IN" o "OUT"
    cJSON_AddStringToObject(root, "cardId", uid_hex);
    cJSON_AddStringToObject(root, "user",   user_text);
    cJSON_AddStringToObject(root, "name",   device_id);
    cJSON_AddStringToObject(root, "idTorno",   id_torno);

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        cJSON_Delete(root);
        return;
    }

    // Usamos el topic de respuesta fijo (similar a lo que hacías en MicroPython con LOG/RESP)
    const char *topic = TOPIC_RESP_FIXED;

    bool ok = mqtt_enqueue(topic, json_str, 1, 0);
    if (!ok) {
        ESP_LOGW(TAG, "No se pudo encolar mensaje MQTT getAccessTorn");
    }

    cJSON_free(json_str);
    cJSON_Delete(root);
}

static void rc522_task(void *pv)
{
    (void)pv;
    ESP_LOGI(TAG, "Task RC522 x2 (bloque 8) arrancada");

    char uid_hex[16];
    char user_text[32];

    while (1) {
        if (!s_mqtt_connected) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // ===== IN =====
        memset(uid_hex, 0, sizeof(uid_hex));
        memset(user_text, 0, sizeof(user_text));
        if (rc522_read_card_block8(s_rc522_1, uid_hex, sizeof(uid_hex),
                                   user_text, sizeof(user_text))) {

            s_last_in_ok = true;

            if (should_publish(&s_db_in, uid_hex)) {
                if (access_gate_try_acquire()) {
                    ESP_LOGI(TAG, "IN -> UID=%s user='%s' (PUBLICANDO)", uid_hex, user_text);
                    publish_access_event("IN", uid_hex, user_text);
                } else {
                    ESP_LOGW(TAG, "IN -> ignorada, esperando respuesta hasAccess");
                }
            }

        } else {
            mark_no_card(&s_db_in);
        }

        // ===== OUT =====
        memset(uid_hex, 0, sizeof(uid_hex));
        memset(user_text, 0, sizeof(user_text));
        if (rc522_read_card_block8(s_rc522_2, uid_hex, sizeof(uid_hex),
                                   user_text, sizeof(user_text))) {

            s_last_out_ok = true;

            if (should_publish(&s_db_out, uid_hex)) {
                if (access_gate_try_acquire()) {
                    ESP_LOGI(TAG, "OUT -> UID=%s user='%s' (PUBLICANDO)", uid_hex, user_text);
                    publish_access_event("OUT", uid_hex, user_text);
                } else {
                    ESP_LOGW(TAG, "OUT -> ignorada, esperando respuesta hasAccess");
                }
            }


        } else {
            mark_no_card(&s_db_out);
        }

        vTaskDelay(pdMS_TO_TICKS(60));
    }
}


// ================== Escritura bloque 8 (lector OUT) ==================

// Escribe user_text en el bloque 8 de la tarjeta detectada por "dev".
// Devuelve true si ha podido escribir y obtener UID.
static bool rc522_write_card_block8(spi_device_handle_t dev,
                                    const char *user_text,
                                    char *uid_str, size_t uid_str_size)
{
    if (!dev || !user_text || !uid_str || uid_str_size < 2) {
        return false;
    }

    uint8_t atqa[2];
    uint8_t atqa_len = sizeof(atqa);

    // Detectar tarjeta
    if (!rc522_request(dev, PICC_REQIDL, atqa, &atqa_len)) {
        return false; // No hay tarjeta
    }

    // Anticolisión -> UID de 4 bytes
    uint8_t uid4[4];
    uint8_t uid_len = 0;
    if (!rc522_anticoll(dev, uid4, &uid_len)) {
        return false;
    }
    if (uid_len != 4) {
        return false;
    }

    // UID en hex
    char *p = uid_str;
    for (int i = 0; i < 4 && (size_t)((p - uid_str) + 2) < uid_str_size; i++) {
        sprintf(p, "%02X", uid4[i]);
        p += 2;
    }
    *p = '\0';

    ESP_LOGI(TAG, "WRITE: Tarjeta detectada UID=%s, intentando escribir bloque 8", uid_str);

    // SELECT de la tarjeta (habilita Crypto1 correctamente)
    if (!rc522_select(dev, uid4)) {
        ESP_LOGW(TAG, "WRITE: SELECT fallo para UID=%s, no se puede autenticar", uid_str);
        return false;
    }

    // === AUTH EN BLOQUE 8 (igual que en lectura) ===
    uint8_t keyA[6];
    memcpy(keyA, KEY_DEFAULT, 6);

    ESP_LOGD(TAG, "WRITE: Intentando AUTH con KeyA en bloque 8");
    bool authed = rc522_auth(dev, 0x60 /* Key A */, 8, keyA, uid4);
    if (!authed) {
        uint8_t keyB[6];
        memcpy(keyB, KEY_DEFAULT, 6);
        ESP_LOGD(TAG, "WRITE: AUTH A fallo, probando KeyB en bloque 8");
        authed = rc522_auth(dev, 0x61 /* Key B */, 8, keyB, uid4);
        if (!authed) {
            ESP_LOGW(TAG, "WRITE: AUTH fallo en bloque 8 (ni A ni B)");
            rc522_stop_crypto(dev);
            return false;
        }
    }

    // === PREPARAR DATA PARA BLOQUE 8 ===
    uint8_t block_data[16];
    size_t len = strlen(user_text);
    if (len > 16) len = 16;

    // Copiamos user_text y rellenamos con espacios (0x20)
    memset(block_data, 0x20, sizeof(block_data));
    memcpy(block_data, user_text, len);

    // === COMANDO WRITE (0xA0) + CRC ===
    uint8_t cmd[2] = { PICC_WRITE /* 0xA0 */, 8 };
    uint8_t crcL, crcH;
    rc522_calc_crc(dev, cmd, 2, &crcL, &crcH);

    uint8_t frame[4] = { PICC_WRITE, 8, crcL, crcH };
    uint8_t back[4] = {0};
    uint8_t back_bytes = sizeof(back);
    uint8_t back_bits  = 0;

    bool ok = rc522_to_card(dev, PCD_TRANSCEIVE,
                            frame, sizeof(frame),
                            back, &back_bytes, &back_bits);
    if (!ok) {
        ESP_LOGW(TAG, "WRITE: rc522_to_card fallo enviando comando WRITE");
        rc522_stop_crypto(dev);
        return false;
    }

    // Debemos recibir ACK (4 bits, valor 0x0A)
    if (back_bits != 4 || (back[0] & 0x0F) != 0x0A) {
        ESP_LOGW(TAG, "WRITE: no ACK tras comando WRITE (bits=%d, val=0x%02X)",
                 back_bits, back[0]);
        rc522_stop_crypto(dev);
        return false;
    }

    // === Enviar 16 bytes de datos + CRC ===
    uint8_t data_frame[18];
    memcpy(data_frame, block_data, 16);
    rc522_calc_crc(dev, block_data, 16, &crcL, &crcH);
    data_frame[16] = crcL;
    data_frame[17] = crcH;

    memset(back, 0, sizeof(back));
    back_bytes = sizeof(back);
    back_bits  = 0;

    ok = rc522_to_card(dev, PCD_TRANSCEIVE,
                       data_frame, sizeof(data_frame),
                       back, &back_bytes, &back_bits);

    rc522_stop_crypto(dev);

    if (!ok) {
        ESP_LOGW(TAG, "WRITE: rc522_to_card fallo al enviar datos bloque 8");
        return false;
    }

    if (back_bits != 4 || (back[0] & 0x0F) != 0x0A) {
        ESP_LOGW(TAG, "WRITE: no ACK tras escribir datos (bits=%d, val=0x%02X)",
                 back_bits, back[0]);
        return false;
    }

    ESP_LOGI(TAG, "WRITE: Bloque 8 escrito OK para UID=%s", uid_str);
    return true;
}

// Función pública: usa el lector de salida (s_rc522_2) y
// espera hasta timeout_ms a que acerques una tarjeta para escribir.
bool rc522_write_card_out_block8(const char *user_text,
                                 char *uid_hex_out,
                                 size_t uid_hex_out_size,
                                 uint32_t timeout_ms)
{
    if (!s_rc522_2) {
        ESP_LOGW(TAG, "WRITE OUT: lector OUT no inicializado");
        return false;
    }

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    char uid_tmp[16] = {0};

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if (rc522_write_card_block8(s_rc522_2,
                                    user_text,
                                    uid_tmp, sizeof(uid_tmp))) {

            // Copiar UID a salida si el caller lo pide
            if (uid_hex_out && uid_hex_out_size > 0) {
                strncpy(uid_hex_out, uid_tmp, uid_hex_out_size - 1);
                uid_hex_out[uid_hex_out_size - 1] = '\0';
            }

            return true;
        }

        // Si no hay tarjeta o ha fallado, esperamos un poco y reintentamos
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "WRITE OUT: timeout esperando tarjeta para escribir");
    return false;
}


// ================== Inicialización pública ==================

esp_err_t pn532_reader_init(void)   // reutilizamos nombre
{
    esp_err_t ret;

    // Bus SPI compartido
    spi_bus_config_t buscfg = {
        .mosi_io_num = RC522_PIN_MOSI,
        .miso_io_num = RC522_PIN_MISO,
        .sclk_io_num = RC522_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };

    ret = spi_bus_initialize(RC522_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Error spi_bus_initialize: %s", esp_err_to_name(ret));
        return ret;
    }

    // Lector 1
    spi_device_interface_config_t devcfg1 = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = RC5221_PIN_SS,
        .queue_size = 1,
        .flags = 0,
    };
    ret = spi_bus_add_device(RC522_SPI_HOST, &devcfg1, &s_rc522_1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error add_device lector1: %s", esp_err_to_name(ret));
        return ret;
    }

    // Lector 2
    spi_device_interface_config_t devcfg2 = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = RC5222_PIN_SS,
        .queue_size = 1,
        .flags = 0,
    };
    ret = spi_bus_add_device(RC522_SPI_HOST, &devcfg2, &s_rc522_2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error add_device lector2: %s", esp_err_to_name(ret));
        return ret;
    }

    // Pines RST
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RC5221_PIN_RST) | (1ULL << RC5222_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(RC5221_PIN_RST, 1);
    gpio_set_level(RC5222_PIN_RST, 1);

    ESP_LOGI(TAG, "RC522 x2 inicializados en SPI");

    rc522_init_chip(s_rc522_1, "lector1");
    rc522_init_chip(s_rc522_2, "lector2");

    if (s_rc522_mutex == NULL) {
        s_rc522_mutex = xSemaphoreCreateMutex();
        if (s_rc522_mutex == NULL) {
            ESP_LOGE(TAG, "No se pudo crear el mutex RC522");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

void pn532_reader_start_task(void)
{
    xTaskCreate(rc522_task, "rc522_task", 4096, NULL, 5, NULL);
}
