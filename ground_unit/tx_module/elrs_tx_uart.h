#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ELRS_TX_UART_NUM UART_NUM_2
#define ELRS_TX_UART_BAUD 420000
#define ELRS_TX_UART_TX_PIN 4
#define ELRS_TX_UART_RX_PIN 5
#define ELRS_TX_UART_BUF_SIZE 512

// Set to 1 if Nomad pads require inverted UART (test during bring-up).
#define ELRS_TX_UART_INVERTED 0

esp_err_t elrs_tx_uart_init(void);
int elrs_tx_uart_write(const uint8_t *data, size_t len);
int elrs_tx_uart_read(uint8_t *data, size_t max_len, uint32_t timeout_ms);
void elrs_tx_uart_flush_input(void);
