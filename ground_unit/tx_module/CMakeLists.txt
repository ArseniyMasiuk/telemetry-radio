idf_component_register(
    SRCS
        "crsf_protocol.c"
        "elrs_tx_uart.c"
        "elrs_tx_host.c"
        "elrs_tx_params.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_uart
)
