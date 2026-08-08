#include <stddef.h>
#include <stdint.h>

#include "switches.h"

static const char *TAG = "MAIN";

#define ISR_PIN 20

void app_main(void) {

    // test
    switch_ctx_t switch_ctx = {
        .pin = ISR_PIN,
    };

    ESP_ERROR_CHECK(switches_init(&switch_ctx, 1));
}