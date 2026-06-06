idf_component_register(SRCS "main.c" "uart.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart
                    )