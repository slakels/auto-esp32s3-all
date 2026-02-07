// config.h
#pragma once

#include "driver/spi_master.h"
#include "driver/uart.h"
#include "hal/gpio_types.h"

#define FW_VERSION "1.0.0"

//#define DEVICE_ID_FIXED   "TRESPINS_TORN_RECEPCIO"
#define DEVICE_ID_FIXED   "SFTCLUB_DEVICE"

// WiFi
typedef struct {
    const char *ssid;
    const char *pass;
} wifi_net_t;

extern const wifi_net_t WIFI_NETS[];
extern const int WIFI_NETS_COUNT;

#define MAX_RETRY_PER_AP   5

// MQTT
//#define MQTT_HOST "77.37.125.73"
#define MQTT_HOST "mqtt.pro.wiplaypadel.com"
//#define MQTT_HOST "54.38.243.157"
#define MQTT_PORT 1883
#define MQTT_USER "admin"
#define MQTT_PASS "Abc_0123456789"

// Tempos
#define TEMPS_PULSADOR_MS   500
#define TEMPS_MATERIAL_MS   3000

// Flags inversos
#define INTERRUPTOR_INVERSO   false
#define BOCINA_INVERSA        false
#define MATERIAL_INVERSO      false
#define ENTRADA_INVERSO       false

// Topics
#define TOPIC_ROOT_BASE   "/var/deploys/topics"
//#define TOPIC_RESP_FIXED  "/var/deploys/topics/TRESPINS"
#define TOPIC_RESP_FIXED  "/var/deploys/topics/SFTCLUB"

#define RC522_SPI_HOST      SPI2_HOST   // mismo que antes

// Pines comunes del bus
#define RC522_PIN_MOSI      11
#define RC522_PIN_MISO      13
#define RC522_PIN_SCK       12

// Lector 1 (entrada)
#define RC5221_PIN_SS       10
#define RC5221_PIN_RST      16

// Lector 2 (salida)
#define RC5222_PIN_SS       15
#define RC5222_PIN_RST      17

#define TORN_IN_PIN    19   // GPIO real para relé entrada
#define TORN_OUT_PIN   20   // GPIO real para relé salida

#define PITO_DENEGADO_PIN   21   // GPIO del zumbador

// ===== GM861S (QR) UART =====
#define GM861S_UART_PORT UART_NUM_1
#define GM861S_BAUD      9600

// Elige pines libres (ejemplo)
#define GM861S_UART_TX   GPIO_NUM_17   // ESP32 -> RXD del GM861S
#define GM861S_UART_RX   GPIO_NUM_18   // ESP32 <- TXD del GM861S
