#include <inttypes.h>

typedef struct
{
    enum
    {
        APP_QUIT,                /**< Request to exit: uninstall CDC driver and USB host lib */
        APP_DEVICE_CONNECTED,    /**< New USB CDC device connected */
        APP_DEVICE_DISCONNECTED, /**< CDC device disconnected */
    } id;
    union
    {
        struct
        {
            uint16_t vid;
            uint16_t pid;
        } new_dev;       /**< VID/PID for APP_DEVICE_CONNECTED */
        int device_slot; /**< Slot for APP_DEVICE_DISCONNECTED */
    } data;
} app_message_t;

void create_app_queue();
void delete_app_queue();
bool queue_is_valid();

void send_message_to_queue(app_message_t *message);
void get_message_from_queue(app_message_t *message);
