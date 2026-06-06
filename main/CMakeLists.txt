idf_component_register(SRCS "main.c" "telemetry_uart.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart
                    )