#include <esp_err.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/gpio_filter.h>
#include <freertos/FreeRTOS.h>
#include <stddef.h>
#include <stdint.h>
#include <tinyusb.h>
#include <tinyusb_default_config.h>
#include <class/hid/hid_device.h>
#include <class/hid/hid.h>
#include <string.h>

#include "switches.h"

#define DEBOUNCE_TIME_MS 50
#define KEYSTROKE_TIME_MS 10

static const char *TAG = "SWITCHES";

typedef struct {
    switch_ctx_t *ctx;
    uint8_t level;
} switch_event_t;

static QueueHandle_t switches_queue = NULL;

// isr handler for any edge
static void IRAM_ATTR switch_isr_handler(void *arg) {
    switch_ctx_t *switch_ctx = (switch_ctx_t *) arg;

    gpio_intr_disable(switch_ctx->pin);

    esp_timer_start_once(switch_ctx->debounce_timer, DEBOUNCE_TIME_MS * 1000ULL);
}

// triggers after DEBOUNCE_TIME_MS to read gpio level
static void debounce_timer_callback(void *arg) {
    switch_ctx_t *switch_ctx = (switch_ctx_t *) arg;
    
    uint8_t current_level = gpio_get_level(switch_ctx->pin);

    // only queue event if state actually changed compared to the last state
    if (current_level != switch_ctx->last_level) {
        switch_ctx->last_level = current_level;
        
        const switch_event_t event = {
            .ctx = switch_ctx,
            .level = current_level,
        };

        if (xQueueSend(switches_queue, &event, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Queue full, keystroke dropped");
        }
    }

    // reset isr
    gpio_set_intr_type(switch_ctx->pin, GPIO_INTR_DISABLE);
    gpio_set_intr_type(switch_ctx->pin, GPIO_INTR_ANYEDGE);

    gpio_intr_enable(switch_ctx->pin);
}

// handles switch events and sends corresponding keystrokes
static void switches_handler_task(void *pvParams) {
    switch_event_t switch_event = {0};
    static const uint8_t empty_keycode[6] = {0};

    while (1) {
        memset(&switch_event, 0, sizeof(switch_event));
        xQueueReceive(switches_queue, &switch_event, portMAX_DELAY);
        if (switch_event.ctx == NULL) {
            continue;
        }

        if (tud_hid_ready()) {
            if (switch_event.level == 1) {
                // rising key press
                tud_hid_n_keyboard_report(0, 0, 0, switch_event.ctx->rising_keycode);
            } else if (switch_event.level == 0) {
                // falling key press
                tud_hid_n_keyboard_report(0, 0, 0, switch_event.ctx->falling_keycode);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(KEYSTROKE_TIME_MS));

        if (tud_hid_ready()) {
            // release kepress
            tud_hid_n_keyboard_report(0, 0, 0, empty_keycode);
        }
    }
}

// initialize switches, switch_ctx array must stay in scope
esp_err_t switches_init(switch_ctx_t switch_ctx[], size_t num_switches) {
    if (switch_ctx == NULL || num_switches == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    switches_queue = xQueueCreate(num_switches * 2, sizeof(switch_event_t));

    esp_err_t err = gpio_install_isr_service(0);
    // returns invalid state if already installed
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    // set up the HID keyboard device
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "Failed to install tinyusb driver");

    for (size_t i = 0; i < num_switches; i++) {

        // set up the gpio
        const gpio_config_t gpio_cfg = {
            .pin_bit_mask = 1ULL << switch_ctx[i].pin,
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_ANYEDGE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gpio_cfg), TAG, "GPIO config for DRDY failed");

        const gpio_pin_glitch_filter_config_t glitch_cfg = {
            .gpio_num = switch_ctx[i].pin,
            .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        };
        gpio_glitch_filter_handle_t glitch_handle = NULL;
        gpio_new_pin_glitch_filter(&glitch_cfg, &glitch_handle);
        gpio_glitch_filter_enable(glitch_handle);

        switch_ctx[i].last_level = gpio_get_level(switch_ctx[i].pin);

        // add the timer callback
        const esp_timer_create_args_t debounce_timer_args = {
            .callback = debounce_timer_callback,
            .arg = &switch_ctx[i],
            .name = "debounce timer",
        };
        esp_timer_create(&debounce_timer_args, &switch_ctx[i].debounce_timer);

        // add the isr
        ESP_RETURN_ON_ERROR(
            gpio_isr_handler_add(switch_ctx[i].pin, switch_isr_handler, &switch_ctx[i]),
            TAG,
            "Failed to add GPIO to ISR handler"
        );

    }

    xTaskCreate(switches_handler_task, "Switches Handler", 2048, NULL, 1, NULL);

    return ESP_OK;
}

// callbacks for tinyusb keyboard

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    return hid_report_descriptor; 
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, const uint8_t *buffer, uint16_t bufsize) {

}