// led_status.c

#include "led_status.h"
#include "core.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"

#define RGB_LED_GPIO        48
#define RGB_LED_MAX_BRIGHT  64

static const char *TAG = "LED_STATUS";

static led_strip_handle_t s_led_strip = NULL;
static uint8_t last_r = 255, last_g = 255, last_b = 255;

// === Era tu set_led_color original ===
static void set_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_strip == NULL) return;

    if (r > RGB_LED_MAX_BRIGHT) r = RGB_LED_MAX_BRIGHT;
    if (g > RGB_LED_MAX_BRIGHT) g = RGB_LED_MAX_BRIGHT;
    if (b > RGB_LED_MAX_BRIGHT) b = RGB_LED_MAX_BRIGHT;

    if (r == last_r && g == last_g && b == last_b) {
        return; // nada que hacer
    }
    last_r = r; last_g = g; last_b = b;

    led_strip_set_pixel(s_led_strip, 0, r, g, b);
    led_strip_refresh(s_led_strip);
}

// === Era tu rgb_led_init original, ahora interno ===
static void rgb_led_init_internal(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds       = 1,
        .led_model      = LED_MODEL_WS2812,
        .flags.invert_out = 0,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 5 * 1000 * 1000,   // 5 MHz
        .mem_block_symbols = 48,
        .flags.with_dma    = 0,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_led_strip));
}

void led_status_init(void)
{
    rgb_led_init_internal();
}

// === Era tu led_status_task original ===
static void led_status_task(void *pv)
{
    while (1) {
        switch (s_led_mode) {
            case LED_MODE_OFF:
                set_led_color(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case LED_MODE_WIFI_CONNECTING:
                // Azul parpadeando
                set_led_color(0, 0, 50);
                vTaskDelay(pdMS_TO_TICKS(200));
                set_led_color(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case LED_MODE_WIFI_OK_NO_MQTT:
                set_led_color(50, 50, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            case LED_MODE_MQTT_OK:
                set_led_color(0, 50, 0);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case LED_MODE_ERROR:
                // Rojo parpadeando rápido
                set_led_color(50, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(150));
                set_led_color(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(150));
                break;

            default:
                // Por si acaso
                set_led_color(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
        }
    }
}

void led_status_start_task(void)
{
    xTaskCreate(led_status_task, "led_status_task", 2048, NULL, 2, NULL);
}
