#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "device_queue.h"

/**
 * @brief Application queue and its message IDs
 */
static QueueHandle_t app_queue = NULL;

void create_app_queue()
{
    app_queue = xQueueCreate(10, sizeof(app_message_t));
    assert(app_queue);
}

bool queue_is_valid()
{
    return app_queue;
}

void send_message_to_queue(app_message_t *message)
{
    xQueueSend(app_queue, message, 0);
}

void get_message_from_queue(app_message_t *message)
{
    xQueueReceive(app_queue, message, portMAX_DELAY);
}

void delete_app_queue()
{
    vQueueDelete(app_queue);
}