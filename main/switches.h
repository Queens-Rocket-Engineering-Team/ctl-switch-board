#pragma once

#include <driver/gpio.h>
#include <esp_timer.h>
#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

typedef struct {
    gpio_num_t pin;
    uint8_t rising_keycode[6];
    uint8_t falling_keycode[6];
    TimerHandle_t debounce_timer;
    uint8_t last_level;
} switch_ctx_t;

esp_err_t switches_init(switch_ctx_t switch_ctx[], size_t num_switches);