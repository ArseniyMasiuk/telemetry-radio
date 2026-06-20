#pragma once

#include "esp_err.h"
#include "usb/cdc_acm_host.h"

#define MAX_CDC_DEVICES (5)

esp_err_t usb_cdc_write(int slot, const uint8_t *data, size_t len);
void start_main_USB_task(cdc_acm_data_callback_t usb_device_data_handler);
