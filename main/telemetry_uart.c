#include "driver/uart.h"
#include "esp_log_buffer.h"
#include "esp_log.h"
#include "esp_err.h"

#include "telemetry_uart.h"

#define TELEM_UART_NUM UART_NUM_1
#define TELEM_TX_IO_NUM (17)
#define TELEM_RX_IO_NUM (18)
#define TELEM_BAUD_RATE 460800
#define BUF_SIZE (1024)

static const char *UART_MODULE = "UART-MODULE";

void write_data_to_uart(const uint8_t *data, size_t data_len)
{
    uart_write_bytes(TELEM_UART_NUM, (const char *)data, data_len);
}

int read_data_from_uart(uint8_t *data)
{
    return uart_read_bytes(TELEM_UART_NUM, data, BUF_SIZE, pdMS_TO_TICKS(5));
}

void configure_telemetry_uart(void)
{
    ESP_LOGI(UART_MODULE, "Setting up UART port");

    uart_config_t uart_config = {
        .baud_rate = TELEM_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(TELEM_UART_NUM, &uart_config));

    // Set pins (TX, RX, RTS, CTS)
    ESP_ERROR_CHECK(uart_set_pin(TELEM_UART_NUM, TELEM_TX_IO_NUM, TELEM_RX_IO_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Install the driver without an interrupt queue (simple TX stream)
    ESP_ERROR_CHECK(uart_driver_install(TELEM_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
}
