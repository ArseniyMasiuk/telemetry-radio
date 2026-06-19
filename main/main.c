// Enable exactly one module role:
// #define UART_COMMUNICATION      // Air Module
// #define UDP_WIFI_COMMUNICATION  // Ground Module
// #define ELRS_TX_HOST_MODE       // TX Host Module — CRSF handset for Nomad TX

#if defined(ELRS_TX_HOST_MODE) && (defined(UART_COMMUNICATION) || defined(UDP_WIFI_COMMUNICATION))
#error "Enable only one module role: ELRS_TX_HOST_MODE, UART_COMMUNICATION, or UDP_WIFI_COMMUNICATION"
#endif

#ifdef ELRS_TX_HOST_MODE

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "elrs_tx_host.h"
#include "elrs_tx_params.h"

static const char *TAG = "ELRS-TX-MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "ELRS TX Host Module starting");

    ESP_ERROR_CHECK(elrs_tx_host_init());
    ESP_ERROR_CHECK(elrs_tx_host_start());

    esp_err_t err = elrs_tx_params_fetch_all();
    if (err == ESP_OK)
    {
        elrs_tx_params_log_all();
    }
    else
    {
        ESP_LOGW(TAG, "Parameter fetch failed: %s (RC emulation continues)", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Running — RC emulation active, link stats logged every 1s");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#else

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.h"
#include "usb/vcp_cp210x.h"
#include "usb/vcp_ftdi.h"

#include "telemetry_uart.h"
#include "device_queue.h"
#include "telemetry_ubs_device.h"
#include "telemetry_wifi.h"
#include "telemetry_udp_support.h"

#define TX_TIMEOUT_MS (1000)
#define MAX_CDC_DEVICES (5)
#define ESPRESSIF_VID (0x303A) // 0x303A Espressif VID, used in TinyUSB devices or in USB-Serial-JTAG devices


// #define UART_COMMUNICATION
#define UDP_WIFI_COMMUNICATION
// #define ELRS_TX_HOST_MODE

#ifdef UART_COMMUNICATION
#define BUF_SIZE 1024

#elifdef UDP_WIFI_COMMUNICATION
#define BUF_SIZE 128

#endif

static const char *TAG = "USB-CDC-MAIN";

static cdc_acm_dev_hdl_t cdc_devices[MAX_CDC_DEVICES] = {0};

///===============================================================================================================
// uplink

int read_data_from_GS(uint8_t *data)
{
#ifdef UART_COMMUNICATION
    return read_data_from_uart(dev_handle);
#elifdef UDP_WIFI_COMMUNICATION
    // todo: read data from UDP socket
    // for now, return 0 as zero bytes read
    return 0;
#endif
}

void GS_to_FC_task(void *pvParameters)
{
    cdc_acm_dev_hdl_t dev_handle = (cdc_acm_dev_hdl_t)pvParameters;

    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);

    while (1)
    {
        // Read data from the RX FIFO buffer
        int data_len = read_data_from_GS(data);

        if (data_len > 0)
        {
            ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_DEBUG);

            esp_err_t err = cdc_acm_host_data_tx_blocking(dev_handle, data, data_len, TX_TIMEOUT_MS);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to write data: %s", esp_err_to_name(err));
            }
        }
        // Block briefly to pass control back to the OS scheduler
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}

// downlink
void FC_TO_GS_task(const uint8_t *data, size_t data_len)
{
#ifdef UART_COMMUNICATION
    write_data_to_uart(data, data_len);
#elifdef UDP_WIFI_COMMUNICATION
    udp_send_all(data, data_len);
#endif
}

void setup_communications()
{
#ifdef UART_COMMUNICATION
    configure_telemetry_uart();
#elifdef UDP_WIFI_COMMUNICATION
    configure_wifi();

    configure_udp_manager();
#endif
}

///===============================================================================================================
static inline int find_free_slot(void)
{
    for (int i = 0; i < MAX_CDC_DEVICES; i++)
    {
        if (cdc_devices[i] == NULL)
        {
            return i;
        }
    }
    return -1;
}

static bool handle_data_from_USB(const uint8_t *data, size_t data_len, void *arg)
{
    FC_TO_GS_task(data, data_len);

    ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_DEBUG);
    return true;
}

static void handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type)
    {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "CDC-ACM error has occurred, err_no = %i", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        if (queue_is_valid())
        {
            app_message_t msg = {
                .id = APP_DEVICE_DISCONNECTED,
                .data.device_slot = (int)(intptr_t)user_ctx,
            };
            send_message_to_queue(&msg);
        }
        else
        {
            ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
        }
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        ESP_LOGI(TAG, "Serial state notif 0x%04X", event->data.serial_state.val);
        break;
    default:
        ESP_LOGW(TAG, "Unsupported CDC event: %d (possibly suspend/resume)", event->type);
        break;
    }
}

void log_usb_cdc_device_open(char *success_string, char *failure_string, esp_err_t err)
{
    if (err == ESP_OK)
        ESP_LOGI(TAG, "%s", success_string);
    else
        ESP_LOGE(TAG, "%s %s", failure_string, esp_err_to_name(err));
}

static cdc_acm_dev_hdl_t usb_cdc_device_open(uint16_t vid, uint16_t pid,
                                             const cdc_acm_host_device_config_t *dev_config)
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;
    esp_err_t err;

    ESP_LOGI(TAG, "Opening device VID=0x%04X PID=0x%04X", vid, pid);

    switch (vid)
    {
    case FTDI_VID:
        err = ftdi_vcp_open(pid, 0, dev_config, &cdc_dev);
        log_usb_cdc_device_open("FTDI_VID device opened",
                                "Failed to open FTDI_VID device. Error:",
                                err);
        break;

    case NANJING_QINHENG_MICROE_VID:
        err = ch34x_vcp_open(pid, 0, dev_config, &cdc_dev);
        log_usb_cdc_device_open("NANJING_QINHENG_MICROE_VID device opened",
                                "Failed to open NANJING_QINHENG_MICROE_VID device. Error:",
                                err);
        break;

    case SILICON_LABS_VID:
        err = cp210x_vcp_open(pid, 0, dev_config, &cdc_dev);
        log_usb_cdc_device_open("SILICON_LABS_VID device opened",
                                "Failed to open SILICON_LABS_VID device. Error:",
                                err);
        break;

    case ESPRESSIF_VID:
    default:
        err = cdc_acm_host_open(vid, pid, 0, dev_config, &cdc_dev);
        log_usb_cdc_device_open("ESPRESSIF_VID device opened",
                                "Failed to open ESPRESSIF_VID device.Error : ",
                                err);
        break;
    }

    if (err == ESP_OK)
    {
        return cdc_dev;
    }

    ESP_LOGE(TAG, "Failed to open device VID=0x%04X PID=0x%04X", vid, pid);
    return NULL;
}

static void free_cdc_device(int slot)
{
    if (slot < 0 || slot >= MAX_CDC_DEVICES || cdc_devices[slot] == NULL)
    {
        return;
    }
    ESP_LOGI(TAG, "\t- Closing CDC device in slot %d", slot);
    cdc_acm_host_close(cdc_devices[slot]);
    cdc_devices[slot] = NULL;
}

static void free_all_cdc_devices(void)
{
    for (int i = 0; i < MAX_CDC_DEVICES; i++)
    {
        if (cdc_devices[i] != NULL)
        {
            free_cdc_device(i);
        }
    }
}

void app_main(void)
{
    create_app_queue();

    configure_USB();

    setup_communications();

    cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 0,
        .out_buffer_size = 2048,
        .in_buffer_size = 512,
        .user_arg = NULL,
        .event_cb = handle_event,
        .data_cb = handle_data_from_USB};

    ESP_LOGI("app_main", "Waiting for CDC devices.");

    bool running = true;
    while (running)
    {
        app_message_t msg;
        get_message_from_queue(&msg);

        switch (msg.id)
        {
        case APP_DEVICE_CONNECTED:
        {
            int slot = find_free_slot();
            if (slot < 0)
            {
                ESP_LOGW("app_main", "No free slots for new CDC device (max %d)", MAX_CDC_DEVICES);
                continue;
            }

            cdc_acm_dev_hdl_t cdc_dev = usb_cdc_device_open(msg.data.new_dev.vid, msg.data.new_dev.pid, &dev_config);
            if (cdc_dev == NULL)
            {
                continue;
            }

            cdc_devices[slot] = cdc_dev;
            // creating task to send data back to FC
            xTaskCreate(GS_to_FC_task, "GS to FC task", 4096, (void *)cdc_dev, 5, NULL);
            break;
        }

        case APP_DEVICE_DISCONNECTED:
        {
            ESP_LOGI("app_main", "Device disconnected from slot %d", msg.data.device_slot);
            free_cdc_device(msg.data.device_slot);
            break;
        }

        case APP_QUIT:
        {
            ESP_LOGI("app_main", "Exiting example");
            free_all_cdc_devices();
            ESP_ERROR_CHECK(cdc_acm_host_uninstall());
            running = false;
            break;
        }
        default:
            ESP_LOGW("app_main", "Unknown message ID: %d", msg.id);
            break;
        }
    }

    ESP_LOGI("app_main", "\t- Exit completed");
    delete_app_queue();
}

#endif /* ELRS_TX_HOST_MODE */