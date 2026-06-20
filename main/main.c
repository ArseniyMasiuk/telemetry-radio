// Enable exactly one communication mode:
// #define UART_COMMUNICATION   // Air Module
#define UDP_WIFI_COMMUNICATION  // Ground Module (always includes TX module)

#if defined(UART_COMMUNICATION) && defined(UDP_WIFI_COMMUNICATION)
#error "Enable only one communication mode: UART_COMMUNICATION or UDP_WIFI_COMMUNICATION"
#endif

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

#include "telemetry_uart.h"
#include "usb_host_queue.h"
#include "telemetry_ubs_device.h"
#include "telemetry_wifi.h"
#include "telemetry_udp_support.h"
#include "elrs_tx_host.h"
#include "usb_cdc_manager.h"

#ifdef UART_COMMUNICATION
#define BUF_SIZE 1024

#elifdef UDP_WIFI_COMMUNICATION
#define BUF_SIZE 128

#endif

static const char *TAG = "USB-CDC-MAIN";

void setup_communications(void)
{
#ifdef UART_COMMUNICATION
    configure_telemetry_uart();
#elifdef UDP_WIFI_COMMUNICATION
    configure_wifi();
    configure_udp_manager();
    elrs_tx_host_setup();
#endif
}

// ======================================================================================================

int read_data_from_ground(uint8_t *data)
{
#ifdef UART_COMMUNICATION
    return read_data_from_uart(dev_handle);
#elifdef UDP_WIFI_COMMUNICATION
    // todo: read data from UDP socket
    // for now, return 0 as zero bytes read
    return 0;
#endif
}

void ground_to_air(void *pvParameters)
{
    int slot = (int)(intptr_t)pvParameters;

    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);

    while (1)
    {
        int data_len = read_data_from_ground(data);

        if (data_len > 0)
        {
            ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_DEBUG);

            esp_err_t err = usb_cdc_write(slot, data, data_len);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to write data: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}

void air_to_ground(const uint8_t *data, size_t data_len)
{
#ifdef UART_COMMUNICATION
    write_data_to_uart(data, data_len);
#elifdef UDP_WIFI_COMMUNICATION
    udp_send_all(data, data_len);
#endif
}

static bool handle_data_from_USB(const uint8_t *data, size_t data_len, void *arg)
{
    air_to_ground(data, data_len);

    ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_DEBUG);
    return true;
}

void app_main(void)
{
    setup_communications();

    start_main_USB_task(handle_data_from_USB);
}
