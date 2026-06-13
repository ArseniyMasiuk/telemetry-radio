
void configure_telemetry_uart(void);
void read_data_from_uart(cdc_acm_dev_hdl_t dev_handle);
void write_data_to_uart(const uint8_t *data, size_t data_len);
