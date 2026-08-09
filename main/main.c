#include <stddef.h>
#include <stdint.h>

#include "switches.h"

static const char *TAG = "MAIN";

#define SWITCH_PIN 20

void app_main(void) {

    // test
    const switch_config_t switch_cfg = {
        .pin = SWITCH_PIN,
        .rising_keycode = {0},
        .falling_keycode = {0},
    };

    switch_ctx_t switch_ctx = {0};

    ESP_ERROR_CHECK(switches_init(&switch_ctx, &switch_cfg, 1));
}