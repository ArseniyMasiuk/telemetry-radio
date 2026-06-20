#pragma once

#include "esp_err.h"
#include "usb/cdc_acm_host.h"

#define MAX_CDC_DEVICES (5)

void start_main_USB_task(cdc_acm_data_callback_t usb_device_data_handler);
