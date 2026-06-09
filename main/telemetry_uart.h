
void configure_telemetry_uart(void);
void elrs_read_task(void *pvParameters);
void write_data_to_uart(const uint8_t *data, size_t data_len);
