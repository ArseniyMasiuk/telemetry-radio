idf_component_register(
    SRCS
        "telemetry_ubs_device.c"
        "usb_host_queue.c"
        "usb_cdc_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__usb_host_cdc_acm
        espressif__usb_host_ch34x_vcp
        espressif__usb_host_cp210x_vcp
        espressif__usb_host_ftdi_vcp
)
