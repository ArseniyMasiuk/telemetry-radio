idf_component_register(SRCS "main.c" "telemetry_uart.c" "device_queue.c" "telemetry_ubs_device.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart
                    )