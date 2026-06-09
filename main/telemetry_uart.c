#include "esp_log.h"
#include "esp_err.h"
#include "esp_log_buffer.h"

#include "driver/uart.h"
#include "telemetry_uart.h"
#include "usb/cdc_host_types.h"

#include "usb/cdc_acm_host.h"

#define EXAMPLE_TX_TIMEOUT_MS (1000)

#define TELEM_UART_NUM UART_NUM_1 // Use UART1 to avoid interrupting your USB boot/flash console
#define TELEM_TX_IO_NUM (17)      // Replace with your physical TX solder pad GPIO
#define TELEM_RX_IO_NUM (18)      // Replace with your physical RX solder pad GPIO
#define TELEM_BAUD_RATE 460800    // Your exact telemetry radio speed
#define BUF_SIZE (1024)

static const char *FROM_UART_TAG = "FROM UART";

void write_data_to_uart(const uint8_t *data, size_t data_len)
{
    uart_write_bytes(TELEM_UART_NUM, (const char *)data, data_len);
}

void elrs_read_task(void *pvParameters)
{
    cdc_acm_dev_hdl_t dev_handle = (cdc_acm_dev_hdl_t)pvParameters;

    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);

    while (1)
    {
        // Read data from the RX FIFO buffer
        int data_len = uart_read_bytes(TELEM_UART_NUM, data, BUF_SIZE, pdMS_TO_TICKS(5));

        if (data_len > 0)
        {
            // ESP_LOG_BUFFER_HEXDUMP(FROM_UART_TAG, data, data_len, ESP_LOG_DEBUG);

            esp_err_t err = cdc_acm_host_data_tx_blocking(dev_handle, data, data_len, EXAMPLE_TX_TIMEOUT_MS);

            if (err != ESP_OK)
            {
                ESP_LOGE(FROM_UART_TAG, "Failed to write data: %s", esp_err_to_name(err));
            }
        }
        // Block briefly to pass control back to the OS scheduler
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}

void configure_telemetry_uart(void)
{
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
