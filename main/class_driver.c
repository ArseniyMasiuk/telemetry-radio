/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "class_driver.h"

#define CLIENT_NUM_EVENT_MSG        5

typedef enum {
    ACTION_OPEN_DEV         = (1 << 0),
    ACTION_CLOSE_DEV        = (1 << 5),
} action_t;

#define DEV_MAX_COUNT           128

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    action_t actions;
} USB_Device;

typedef struct
{
    usb_host_client_handle_t client_hdl;
    SemaphoreHandle_t mux_lock; /**< Mutex for protected members */
} USB_Host_constants;

typedef union
{
    struct
    {
        uint8_t unhandled_devices : 1; /**< Device has unhandled devices */
        uint8_t shutdown : 1;          /**<  */
        uint8_t reserved6 : 6;         /**< Reserved */
    };
    uint8_t val; /**< Class drivers' flags value */
} USB_Device_Flags;

typedef struct
{
    struct {
        USB_Device_Flags flags;                         /**< Class drivers' flags */
        USB_Device connected_device[DEV_MAX_COUNT];     /**< Class drivers' static array of devices */
    } mux_protected;                            /**< Mutex protected members. Must be protected by the Class mux_lock when accessed */
    
    USB_Host_constants constant; /**< Constant members. Do not change after installation thus do not require a critical section or mutex */
} USB_Driver;

static const char *TAG = "CLASS";
static USB_Driver *s_driver_obj;

static void action_open_dev(USB_Device *device_obj);
static void action_close_dev(USB_Device *device_obj);

static void action_get_str_desc(USB_Device *device_obj);
static void action_get_config_desc(USB_Device *device_obj);
static void action_get_dev_desc(USB_Device *device_obj);

//============================================================================================================

static const char *TAG_GETTING_INTERFACE = "USB_CONFIG_READER";

void enumerate_all_interfaces(usb_device_handle_t dev_hdl)
{
    // 1. Fetch the active configuration descriptor map from the ESP32 host stack
    const usb_config_desc_t *config_desc = NULL;
    esp_err_t err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_GETTING_INTERFACE, "Failed to get active configuration descriptor! Error: 0x%X", err);
        return;
    }
    ESP_LOGI(TAG_GETTING_INTERFACE, "Parsing total configuration tree (%d bytes)...", config_desc->wTotalLength);

    // 2. Step linearly through the descriptor byte buffer
    int offset = 0;
    while (offset < config_desc->wTotalLength)
    {
        // Cast the current offset memory location to a standard base descriptor header
        const usb_standard_desc_t *desc = (const usb_standard_desc_t *)((uint8_t *)config_desc + offset);

        // Safety catch: prevent infinite loops if a malformed descriptor reports zero length
        if (desc->bLength == 0)
        {
            ESP_LOGE(TAG_GETTING_INTERFACE, "Error: Descriptor length is zero. Aborting loop to prevent hang.");
            break;
        }

        // 3. Filter specifically for Interface Descriptors (Type 0x04)
        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE)
        {
            const usb_intf_desc_t *interface = (const usb_intf_desc_t *)desc;

            ESP_LOGW(TAG_GETTING_INTERFACE, "--------------------------------------------------");
            ESP_LOGI(TAG_GETTING_INTERFACE, "Found Interface Index: %d", interface->bInterfaceNumber);
            ESP_LOGI(TAG_GETTING_INTERFACE, "  Alternate Setting:    %d", interface->bAlternateSetting);
            ESP_LOGI(TAG_GETTING_INTERFACE, "  Number of Endpoints:  %d", interface->bNumEndpoints);
            ESP_LOGI(TAG_GETTING_INTERFACE, "  Interface Class:      0x%02X", interface->bInterfaceClass);
            ESP_LOGI(TAG_GETTING_INTERFACE, "  Interface SubClass:   0x%02X", interface->bInterfaceSubClass);
            ESP_LOGI(TAG_GETTING_INTERFACE, "  Interface Protocol:   0x%02X", interface->bInterfaceProtocol);
            ESP_LOGI(TAG_GETTING_INTERFACE, "  String Index (iIntf): %d", interface->iInterface);
        }

        // Move the byte offset forward by the exact size of the current descriptor block
        offset += desc->bLength;
    }

    // 4. CRITICAL: Free the descriptor block to prevent memory leaks in the RTOS heap
    usb_host_free_config_desc(config_desc);
    ESP_LOGI(TAG_GETTING_INTERFACE, "Finished enumeration. Memory released cleanly.");
}

//============================================================================================================

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    USB_Driver *driver_obj = (USB_Driver *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ESP_LOGW(TAG, "New device CONNECTED at address [%d]", event_msg->new_dev.address);

        // Save the connected_device address
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        driver_obj->mux_protected.connected_device[event_msg->new_dev.address].dev_addr = event_msg->new_dev.address;
        driver_obj->mux_protected.connected_device[event_msg->new_dev.address].dev_hdl = NULL;
        // Open the connected_device next
        driver_obj->mux_protected.connected_device[event_msg->new_dev.address].actions |= ACTION_OPEN_DEV;
        // Set flag
        driver_obj->mux_protected.flags.unhandled_devices = 1;
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        // Cancel any other actions and close the connected_device next
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);

        

        for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
            if (driver_obj->mux_protected.connected_device[i].dev_hdl == event_msg->dev_gone.dev_hdl) {
                ESP_LOGW(TAG, "Device DISCONNECTED at address [%d]", i);

                driver_obj->mux_protected.connected_device[i].actions = ACTION_CLOSE_DEV;
                // Set flag
                driver_obj->mux_protected.flags.unhandled_devices = 1;
            }
        }
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    default:
        ESP_LOGW(TAG, "Unsupported client event: %d (possibly suspend/resume)", event_msg->event);
        break;
    }
}

static void class_driver_device_handle(USB_Device *device_obj)
{
    uint8_t actions = device_obj->actions;
    device_obj->actions = 0;

    while (actions) {
        if (actions & ACTION_OPEN_DEV) {
            ESP_LOGI(TAG, "===================Opening Device ====================");
            action_open_dev(device_obj);

            const usb_device_desc_t *dev_desc;
            ESP_ERROR_CHECK(usb_host_get_device_descriptor(device_obj->dev_hdl, &dev_desc));
            printf("idVendor 0x%x\n", dev_desc->idVendor);
            printf("idProduct 0x%x\n", dev_desc->idProduct);
            printf("bDeviceClass 0x%x\n", dev_desc->bDeviceClass);

            ESP_LOGI(TAG, "===================Getting data about interfaces====================");
            enumerate_all_interfaces(device_obj->dev_hdl);
        }
        if (actions & ACTION_CLOSE_DEV) {
            ESP_LOGI(TAG, "===================Closing Device ====================");

            action_close_dev(device_obj);
        }

        actions = device_obj->actions;
        device_obj->actions = 0;
    }
}

void class_driver_task(void *arg)
{
    USB_Driver driver_obj = {0};
    usb_host_client_handle_t class_driver_client_hdl = NULL;

    ESP_LOGI(TAG, "Registering Client");

    SemaphoreHandle_t mux_lock = xSemaphoreCreateMutex();
    if (mux_lock == NULL) {
        ESP_LOGE(TAG, "Unable to create class driver mutex");
        vTaskSuspend(NULL);
        return;
    }

    usb_host_client_config_t client_config = {
        .is_synchronous = false,    //Synchronous clients currently not supported. Set this to false
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &class_driver_client_hdl));

    driver_obj.constant.mux_lock = mux_lock;
    driver_obj.constant.client_hdl = class_driver_client_hdl;

    for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
        driver_obj.mux_protected.connected_device[i].client_hdl = class_driver_client_hdl;
    }

    s_driver_obj = &driver_obj;

    while (1) {
        // Driver has unhandled devices, handle all devices first
        if (driver_obj.mux_protected.flags.unhandled_devices) {
            xSemaphoreTake(driver_obj.constant.mux_lock, portMAX_DELAY);
            for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
                if (driver_obj.mux_protected.connected_device[i].actions) {
                    class_driver_device_handle(&driver_obj.mux_protected.connected_device[i]);
                }
            }
            driver_obj.mux_protected.flags.unhandled_devices = 0;
            xSemaphoreGive(driver_obj.constant.mux_lock);
        } else {
            // Driver is active, handle client events
            if (driver_obj.mux_protected.flags.shutdown == 0) {
                usb_host_client_handle_events(class_driver_client_hdl, portMAX_DELAY);
            } else {
                // Shutdown the driver
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Deregistering Class Client");
    ESP_ERROR_CHECK(usb_host_client_deregister(class_driver_client_hdl));
    if (mux_lock != NULL) {
        vSemaphoreDelete(mux_lock);
    }
    vTaskSuspend(NULL);
}

void class_driver_client_deregister(void)
{
    // Mark all opened devices
    xSemaphoreTake(s_driver_obj->constant.mux_lock, portMAX_DELAY);
    for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
        if (s_driver_obj->mux_protected.connected_device[i].dev_hdl != NULL) {
            // Mark device to close
            s_driver_obj->mux_protected.connected_device[i].actions |= ACTION_CLOSE_DEV;
            // Set flag
            s_driver_obj->mux_protected.flags.unhandled_devices = 1;
        }
    }
    s_driver_obj->mux_protected.flags.shutdown = 1;
    xSemaphoreGive(s_driver_obj->constant.mux_lock);

    // Unblock, exit the loop and proceed to deregister client
    ESP_ERROR_CHECK(usb_host_client_unblock(s_driver_obj->constant.client_hdl));
}

// ==============================================================================================================================

static void action_open_dev(USB_Device *device_obj)
{
    assert(device_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", device_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(device_obj->client_hdl, device_obj->dev_addr, &device_obj->dev_hdl));
}

static void action_close_dev(USB_Device *device_obj)
{
    ESP_LOGI(TAG, "Closing device at address %d", device_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_close(device_obj->client_hdl, device_obj->dev_hdl));
    device_obj->dev_hdl = NULL;
    device_obj->dev_addr = 0;
}

// ==============================================================================================================================

static void action_get_info(USB_Device *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    ESP_LOGI(TAG, "\t%s speed", (char *[]){"Low", "Full", "High"}[dev_info.speed]);
    ESP_LOGI(TAG, "\tParent info:");
    if (dev_info.parent.dev_hdl)
    {
        usb_device_info_t parent_dev_info;
        ESP_ERROR_CHECK(usb_host_device_info(dev_info.parent.dev_hdl, &parent_dev_info));
        ESP_LOGI(TAG, "\t\tBus addr: %d", parent_dev_info.dev_addr);
        ESP_LOGI(TAG, "\t\tPort: %d", dev_info.parent.port_num);
    }
    else
    {
        ESP_LOGI(TAG, "\t\tPort: ROOT");
    }
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    // Get the device descriptor next
}

static void action_get_dev_desc(USB_Device *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(device_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    // Get the device's config descriptor next
}

static void action_get_config_desc(USB_Device *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(device_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    // Get the device's string descriptors next
}

static void action_get_str_desc(USB_Device *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer)
    {
        ESP_LOGI(TAG, "Getting Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product)
    {
        ESP_LOGI(TAG, "Getting Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num)
    {
        ESP_LOGI(TAG, "Getting Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
}
