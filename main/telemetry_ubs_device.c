#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"

#include "usb/cdc_acm_host.h"
#include "usb/cdc_host_types.h"
#include "usb/usb_host.h"

#include "telemetry_ubs_device.h"
#include "device_queue.h"

#define USB_HOST_PRIORITY (20)

static const char *TAG = "USB-CDC";

void init_USB_host();
static void usb_lib_task(void *arg);
static void new_dev_cb(usb_device_handle_t usb_dev);


void configure_USB()
{
    init_USB_host();
    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, USB_HOST_PRIORITY, NULL);
    assert(task_created == pdTRUE);
}

void init_USB_host()
{
    ESP_LOGI("usb_task", "Running USB task");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    ESP_LOGI("usb_task", "\t- Installing USB Host");
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    const cdc_acm_host_driver_config_t driver_config = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = USB_HOST_PRIORITY + 1,
        .xCoreID = 0,
        .new_dev_cb = new_dev_cb,
    };
    ESP_LOGI("usb_task", "\t- Installing CDC-ACM driver");
    ESP_ERROR_CHECK(cdc_acm_host_install(&driver_config));
}

/**
 * @brief USB Host library handling task.
 */
static void usb_lib_task(void *arg)
{
    bool has_clients = true;
    while (1)
    {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            has_clients = false;
            if (ESP_OK == usb_host_device_free_all())
            {
                break;
            }
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            if (!has_clients)
            {
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_ERROR_CHECK(usb_host_uninstall());
    ESP_LOGI("usb_task", "USB Host task completed");
    vTaskDelete(NULL);
}


/**
 * @brief New USB device callback
 *
 * Gets VID/PID and posts APP_DEVICE_CONNECTED to app queue.
 * Called from USB Host context; device is closed after this callback returns.
 *
 * @param[in] usb_dev    USB device handle
 */
static void new_dev_cb(usb_device_handle_t usb_dev)
{
    const usb_device_desc_t *device_desc;
    esp_err_t err = usb_host_get_device_descriptor(usb_dev, &device_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_get_device_descriptor failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t vid = device_desc->idVendor;
    uint16_t pid = device_desc->idProduct;
    ESP_LOGI(TAG, "New CDC device connected VID=0x%04X PID=0x%04X", vid, pid);

    if (queue_is_valid())
    {
        app_message_t msg = {
            .id = APP_DEVICE_CONNECTED,
            .data.new_dev.vid = vid,
            .data.new_dev.pid = pid,
        };
        send_message_to_queue(&msg);
    }
}
