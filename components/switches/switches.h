#pragma once

#include <esp_err.h>
#include <driver/gpio.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    gpio_num_t pin;
    uint8_t rising_keycode[6];
    uint8_t rising_modifiers;
    uint8_t falling_keycode[6];
    uint8_t falling_modifiers;
} switch_config_t;

typedef struct {
    switch_config_t cfg;
    uint8_t verified_level;
    size_t stability_counter;
} switch_ctx_t;

esp_err_t switches_init(switch_ctx_t switch_ctx[], const switch_config_t switch_cfg[], size_t num_switches);