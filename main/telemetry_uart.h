#include "driver/uart.h" // Native ESP-IDF UART drivers


#define TELEM_UART_NUM UART_NUM_1 // Use UART1 to avoid interrupting your USB boot/flash console
#define TELEM_TX_IO_NUM (17)      // Replace with your physical TX solder pad GPIO
#define TELEM_RX_IO_NUM (18)      // Replace with your physical RX solder pad GPIO
#define TELEM_BAUD_RATE 460800    // Your exact telemetry radio speed
#define BUF_SIZE (1024)

void configure_telemetry_uart(void);
void elrs_read_task(void *pvParameters);
void write_data_to_uart(const uint8_t *data, size_t data_len);
