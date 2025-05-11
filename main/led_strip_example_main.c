/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#define RMT_LED_STRIP_RESOLUTION_HZ     10000000        // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM          GPIO_NUM_27
#define MOSFET_TOGGLE_GPIO              GPIO_NUM_26
#define MOSFET_GATE_GPIO                GPIO_NUM_12
#define PIR_GPIO                        GPIO_NUM_25

#define EXAMPLE_LED_NUMBERS             300
#define EXAMPLE_CHASE_SPEED_MS          10


static int toggle_mosfet_gate = 0;
static int toggle_button = 0;
static bool pir_timer_active = false;
static const char *TAG = "LED_STRIP";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];

/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}


void disable_timer(TimerHandle_t xTimer) {
    pir_timer_active = false;
    toggle_mosfet_gate = 0;

    vTaskDelay(2000 / portTICK_PERIOD_MS);
}


void app_main(void)
{
    gpio_config_t mosfet_gate_io_conf = {
        .pin_bit_mask = 1ULL << MOSFET_GATE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&mosfet_gate_io_conf);

    gpio_config_t mosfet_toggle_io_conf = {
        .pin_bit_mask = (1ULL << MOSFET_TOGGLE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&mosfet_toggle_io_conf);

    gpio_config_t pir_io_config = {
        .pin_bit_mask = (1ULL << PIR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&pir_io_config);


    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    // uint16_t hue = 0;
    // uint16_t start_rgb = 0;

    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Start LED rainbow chase");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    while (1) {
        if (!gpio_get_level(MOSFET_TOGGLE_GPIO)) {
            toggle_mosfet_gate ^= 1;
            toggle_button ^= 1;
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }

        if (gpio_get_level(PIR_GPIO) && !pir_timer_active) {
            toggle_mosfet_gate = 1;

            // Start a cooldown timer where the pir will not be read by the mcu gpio
            pir_timer_active = true;
            TimerHandle_t pir_off = xTimerCreate("pir_off", pdMS_TO_TICKS(2000), pdFALSE, NULL, disable_timer);  // 20 seconds cd
            xTimerStart(pir_off, 0);
        }

        if (toggle_mosfet_gate) {
            gpio_set_level(MOSFET_GATE_GPIO, 1);
            for (int i = 0; i < 3; ++i) {
                for (int j = i; j < EXAMPLE_LED_NUMBERS; j += 3) {
                    // Build RGB pixels
                    // hue = j * 360 / EXAMPLE_LED_NUMBERS + start_rgb;
                    led_strip_hsv2rgb(100, 50, 100, &red, &green, &blue);
                    led_strip_pixels[j * 3 + 0] = 0;
                    led_strip_pixels[j * 3 + 1] = 0;
                    led_strip_pixels[j * 3 + 2] = blue;
                }
            }
            // Flush RGB values to LEDs
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
            
            // start_rgb += 60;
        }
        else {
            gpio_set_level(MOSFET_GATE_GPIO, 0);
            memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        }
    }
    
}
