#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"

#include "ping/ping_sock.h"

#define PORT 3333
static const char *TAG = "UDP SOCKET SERVER";

static void udp_sender_task(void *pvParameters)
{
    const char *PC_IP = "192.168.1.2"; // PC IP
    const int PC_PORT = 3333;

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(PC_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PC_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP socket created, sending to %s:%d", PC_IP, PC_PORT);

    int counter = 0;
    while (1)
    {
        char payload[64];
        snprintf(payload, sizeof(payload),
                 "Hello from ESP32, count=%d", counter++);

        int err = sendto(sock, payload, strlen(payload), 0,
                         (struct sockaddr *)&dest_addr,
                         sizeof(dest_addr));

        if (err < 0)
        {
            ESP_LOGE(TAG, "Error sending: errno %d", errno);
        }
        else
        {
            ESP_LOGI(TAG, "Message sent: %s", payload);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second
    }

    close(sock);
    vTaskDelete(NULL);
}

void setup_upd_server(void)
{
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    xTaskCreate(udp_sender_task, "udp_sender", 4096, (void *)AF_INET, 5, NULL);
}
