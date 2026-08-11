#include <esp_err.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/gpio_filter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <tinyusb.h>
#include <tinyusb_default_config.h>
#include <class/hid/hid_device.h>
#include <class/hid/hid.h>

#include "switches.h"

#define POLLING_TIME_MS 1
#define DEBOUNCE_TIME_MS 25
#define DEBOUNCE_COUNT (DEBOUNCE_TIME_MS / POLLING_TIME_MS)

#define KEYSTROKE_TIME_MS 10

static const char *TAG = "SWITCHES";

typedef struct {
    switch_config_t *switch_cfg;
    uint8_t level;
} switch_event_t;

typedef struct {
    switch_ctx_t *switches;
    size_t num_switches;
    TaskHandle_t handler_task;
    QueueHandle_t switch_event_queue;
} polling_timer_ctx_t;

static void polling_timer_callback(void *arg) {
    polling_timer_ctx_t *ctx = (polling_timer_ctx_t *) arg;

    for (size_t i = 0; i < ctx->num_switches; i++) {
        uint8_t raw_level = gpio_get_level(ctx->switches[i].cfg.pin);
        
        if (raw_level != ctx->switches[i].verified_level) {
            // add to the counter if the level is different from the last level
            ctx->switches[i].stability_counter++;
            if (ctx->switches[i].stability_counter >= DEBOUNCE_COUNT) {
                // when the level has changed for long enough switch the stored level
                ctx->switches[i].verified_level = raw_level;
                ctx->switches[i].stability_counter = 0;

                switch_event_t event = {
                    .switch_cfg = &ctx->switches[i].cfg,
                    .level = raw_level
                };
                xQueueSendToBack(ctx->switch_event_queue, &event, 0);
            }
        } else {
            // if the level is not different from the current level for
            // long enough, restart the counter
            ctx->switches[i].stability_counter = 0;
        }
    }
}

// handles switch events and sends corresponding keystrokes
static void switches_handler_task(void *pvParams) {
    QueueHandle_t switch_event_queue = (QueueHandle_t) pvParams;

    static const uint8_t empty_keycode[6] = {0};
    switch_event_t switch_event = {0};

    while (1) {
        if (xQueueReceive(switch_event_queue, &switch_event, portMAX_DELAY) == pdFALSE) {
            continue;
        }

        if (switch_event.switch_cfg == NULL) {
            continue;
        }

        if (tud_hid_ready()) {
            if (switch_event.level == 1) {
                // rising key press
                tud_hid_n_keyboard_report(0, 0, switch_event.switch_cfg->rising_modifiers, switch_event.switch_cfg->rising_keycode);
            } else if (switch_event.level == 0) {
                // falling key press
                tud_hid_n_keyboard_report(0, 0, switch_event.switch_cfg->falling_modifiers, switch_event.switch_cfg->falling_keycode);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(KEYSTROKE_TIME_MS));

        if (tud_hid_ready()) {
            // release kepress
            tud_hid_n_keyboard_report(0, 0, 0, empty_keycode);
        }
    }
}

static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

// initialize switches, switch_ctx array must stay in scope
esp_err_t switches_init(switch_ctx_t switch_ctx[], const switch_config_t switch_cfg[], size_t num_switches) {
    if (switch_ctx == NULL || num_switches == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // static variables are only set on boot
    static bool is_initialized = false;
    if (is_initialized == true) {
        return ESP_ERR_INVALID_STATE;
    }
    is_initialized = true;

    // set up the HID keyboard device
    static const uint8_t hid_configuration_descriptor[] = {
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
        // config number, interface count, string index, total length, attribute, power in mA
        TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

        // interface number, string index, protocol, report descriptor length, EP in address, size, polling interval
        TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(hid_report_descriptor), 0x81, 16, 1)
    };

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "Failed to install tinyusb driver");

    for (size_t i = 0; i < num_switches; i++) {
        // set up switch_ctx fields
        switch_ctx[i].cfg = switch_cfg[i];
        switch_ctx[i].stability_counter = 0;

        // set up the gpio
        const gpio_config_t gpio_cfg = {
            .pin_bit_mask = 1ULL << switch_ctx[i].cfg.pin,
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gpio_cfg), TAG, "GPIO config for DRDY failed");

        const gpio_pin_glitch_filter_config_t glitch_cfg = {
            .gpio_num = switch_ctx[i].cfg.pin,
            .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        };
        gpio_glitch_filter_handle_t glitch_handle = NULL;
        gpio_new_pin_glitch_filter(&glitch_cfg, &glitch_handle);
        if (glitch_handle == NULL) {
            return ESP_FAIL;
        }
        gpio_glitch_filter_enable(glitch_handle);

        // set the current level
        switch_ctx[i].verified_level = gpio_get_level(switch_ctx[i].cfg.pin);
    }

    static QueueHandle_t switch_event_queue = NULL;
    switch_event_queue = xQueueCreate(num_switches * 2, sizeof(switch_event_t));
    if (switch_event_queue == NULL) {
        return ESP_FAIL;
    }

    TaskHandle_t switches_handler = NULL;
    xTaskCreate(switches_handler_task, "Switches Handler", 2048, (void *) switch_event_queue, 1, &switches_handler);
    if (switches_handler == NULL) {
        return ESP_FAIL;
    }

    // add the timer callback
    static polling_timer_ctx_t poll_ctx = {0};
    poll_ctx.switches = switch_ctx;
    poll_ctx.num_switches = num_switches;
    poll_ctx.handler_task = switches_handler;
    poll_ctx.switch_event_queue = switch_event_queue;

    const esp_timer_create_args_t debounce_timer_args = {
        .callback = polling_timer_callback,
        .arg = (void *) &poll_ctx,
        .name = "debounce timer",
    };
    esp_timer_handle_t polling_timer = NULL;
    esp_timer_create(&debounce_timer_args, &polling_timer);
    if (polling_timer == NULL) {
        return ESP_FAIL;
    }
    esp_timer_start_periodic(polling_timer, POLLING_TIME_MS * 1000);

    return ESP_OK;
}

// callbacks for tinyusb keyboard

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    return hid_report_descriptor; 
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, const uint8_t *buffer, uint16_t bufsize) {

}