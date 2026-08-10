idf_component_register(
    SRCS "switches.c"
    INCLUDE_DIRS . 
    REQUIRES
    esp_common
    esp_driver_gpio
    PRIV_REQUIRES
    freertos
    esp_timer
    esp_tinyusb
)