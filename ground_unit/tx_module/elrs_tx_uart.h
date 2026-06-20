#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ELRS_TX_UART_NUM UART_NUM_1
#define ELRS_TX_UART_BAUD 400000
#define ELRS_TX_UART_TX_PIN 17
#define ELRS_TX_UART_RX_PIN 18
#define ELRS_TX_UART_BUF_SIZE 512


// Nomad S port (GPIO4) is half-duplex CRSF — always inverted.
#define ELRS_TX_UART_INVERTED 1

esp_err_t elrs_tx_uart_init(void);
int elrs_tx_uart_write(const uint8_t *data, size_t len);
int elrs_tx_uart_read(uint8_t *data, size_t max_len, uint32_t timeout_ms);
void elrs_tx_uart_flush_input(void);
