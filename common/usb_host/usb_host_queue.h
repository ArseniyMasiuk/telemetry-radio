#include <inttypes.h>

typedef struct
{
    enum
    {
        USB_HOST_QUIT,                /**< Request to exit: uninstall CDC driver and USB host lib */
        USB_HOST_DEVICE_CONNECTED,    /**< New USB CDC device connected */
        USB_HOST_DEVICE_DISCONNECTED, /**< CDC device disconnected */
    } id;
    union
    {
        struct
        {
            uint16_t vid;
            uint16_t pid;
        } new_dev;       /**< VID/PID for USB_HOST_DEVICE_CONNECTED */
        int device_slot; /**< Slot for USB_HOST_DEVICE_DISCONNECTED */
    } data;
} usb_host_message_t;

void create_usb_host_queue();
void delete_usb_host_queue();
bool queue_is_valid();

void send_message_to_queue(usb_host_message_t *message);
void get_message_from_queue(usb_host_message_t *message);
