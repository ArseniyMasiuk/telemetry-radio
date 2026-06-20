idf_component_register(
    SRCS
        "main.c"
        "telemetry_uart.c"
        "telemetry_wifi.c"
        "telemetry_udp_support.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_uart esp_wifi nvs_flash tx_module usb_host
)