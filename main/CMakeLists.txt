idf_component_register(
    SRCS "switches.c" "main.c" "switches.c"
    INCLUDE_DIRS "."
    REQUIRES
    esp_common
    esp_driver_gpio
    freertos
    esp_tinyusb
    esp_timer
)