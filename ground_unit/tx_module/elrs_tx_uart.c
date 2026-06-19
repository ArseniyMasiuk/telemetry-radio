#include "elrs_tx_uart.h"

#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "ELRS-TX-UART";

esp_err_t elrs_tx_uart_init(void)
{
    ESP_LOGI(TAG, "UART%d TX=%d RX=%d baud=%d inverted=%d",
             ELRS_TX_UART_NUM, ELRS_TX_UART_TX_PIN, ELRS_TX_UART_RX_PIN,
             ELRS_TX_UART_BAUD, ELRS_TX_UART_INVERTED);

    uart_config_t uart_config = {
        .baud_rate = ELRS_TX_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(ELRS_TX_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ELRS_TX_UART_NUM, ELRS_TX_UART_TX_PIN, ELRS_TX_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

#if ELRS_TX_UART_INVERTED
    ESP_ERROR_CHECK(uart_set_line_inverse(ELRS_TX_UART_NUM, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV));
#endif

    ESP_ERROR_CHECK(uart_driver_install(ELRS_TX_UART_NUM, ELRS_TX_UART_BUF_SIZE * 2,
                                        ELRS_TX_UART_BUF_SIZE * 2, 0, NULL, 0));
    uart_flush_input(ELRS_TX_UART_NUM);
    return ESP_OK;
}

int elrs_tx_uart_write(const uint8_t *data, size_t len)
{
    return uart_write_bytes(ELRS_TX_UART_NUM, data, len);
}

int elrs_tx_uart_read(uint8_t *data, size_t max_len, uint32_t timeout_ms)
{
    return uart_read_bytes(ELRS_TX_UART_NUM, data, max_len, pdMS_TO_TICKS(timeout_ms));
}

void elrs_tx_uart_flush_input(void)
{
    uart_flush_input(ELRS_TX_UART_NUM);
}
