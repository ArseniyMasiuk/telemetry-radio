#include <string.h>

#include "esp_log.h"
#include "esp_err.h"

#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.h"
#include "usb/vcp_cp210x.h"
#include "usb/vcp_ftdi.h"

#include "usb_host_queue.h"
#include "telemetry_ubs_device.h"
#include "usb_cdc_manager.h"

#define ESPRESSIF_VID (0x303A)

static const char *TAG = "USB-CDC-MAIN";

static cdc_acm_dev_hdl_t cdc_devices[MAX_CDC_DEVICES] = {0};

/* Defined in main.c; will be decoupled in a later refactor */
extern void ground_to_air_task(void *pvParameters);

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
            usb_host_message_t msg = {
                .id = USB_HOST_DEVICE_DISCONNECTED,
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

static void log_usb_cdc_device_open(char *success_string, char *failure_string, esp_err_t err)
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

void start_main_USB_task(cdc_acm_data_callback_t usb_device_data_handler)
{
    create_usb_host_queue();
    configure_USB();

    cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 0,
        .out_buffer_size = 2048,
        .in_buffer_size = 512,
        .user_arg = NULL,
        .event_cb = handle_event,
        .data_cb = usb_device_data_handler};

    ESP_LOGI("app_main", "Waiting for CDC devices.");

    bool running = true;
    while (running)
    {
        usb_host_message_t msg;
        get_message_from_queue(&msg);

        switch (msg.id)
        {
        case USB_HOST_DEVICE_CONNECTED:
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
            xTaskCreate(ground_to_air_task, "ground to air task", 4096, (void *)cdc_dev, 5, NULL);
            break;
        }

        case USB_HOST_DEVICE_DISCONNECTED:
        {
            ESP_LOGI("app_main", "Device disconnected from slot %d", msg.data.device_slot);
            free_cdc_device(msg.data.device_slot);
            break;
        }

        case USB_HOST_QUIT:
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
    delete_usb_host_queue();
}
