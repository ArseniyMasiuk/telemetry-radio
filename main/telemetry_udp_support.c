#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "telemetry_udp_support.h"

#define MAX_CLIENTS 10
#define RX_BUFFER_SIZE 128

static const char *TAG = "UDP_SERVER";

// Structure for a single destination endpoint
typedef struct
{
    struct sockaddr_in dest_addr;
    int sock;
    bool is_active;
} udp_client_t;

// Global array and mutex for safety
static udp_client_t clients[MAX_CLIENTS];
static SemaphoreHandle_t client_mutex = NULL;

// Initialize the array
void configure_udp_manager(void)
{
    client_mutex = xSemaphoreCreateMutex();
    memset(clients, 0, sizeof(clients));
}

bool udp_client_add(const char *ip_str, uint16_t port)
{
    if (!client_mutex)
        return false;

    xSemaphoreTake(client_mutex, portMAX_DELAY);

    // Find an empty slot
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clients[i].is_active)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        ESP_LOGE(TAG, "Cannot add client. List full!");
        xSemaphoreGive(client_mutex);
        return false;
    }

    // Create the UDP socket immediately
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Socket creation failed for %s: errno %d", ip_str, errno);
        xSemaphoreGive(client_mutex);
        return false;
    }

    // Configure the client endpoint struct
    clients[slot].dest_addr.sin_addr.s_addr = inet_addr(ip_str);
    clients[slot].dest_addr.sin_family = AF_INET;
    clients[slot].dest_addr.sin_port = htons(port);
    clients[slot].sock = sock;
    clients[slot].is_active = true;

    ESP_LOGI(TAG, "Client added to slot %d -> %s:%d (Sock: %d)", slot, ip_str, port, sock);

    xSemaphoreGive(client_mutex);
    return true;
}

void udp_client_remove(const char *ip_str)
{
    if (!client_mutex)
        return;

    xSemaphoreTake(client_mutex, portMAX_DELAY);

    uint32_t target_ip = inet_addr(ip_str);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_active && clients[i].dest_addr.sin_addr.s_addr == target_ip)
        {
            // Close the active socket cleanly
            if (clients[i].sock >= 0)
            {
                close(clients[i].sock);
            }
            clients[i].is_active = false;
            clients[i].sock = -1;
            ESP_LOGI(TAG, "Removed client %s from slot %d", ip_str, i);
            break;
        }
    }

    xSemaphoreGive(client_mutex);
}

void udp_send_all(const uint8_t *data, size_t len)
{
    if (!client_mutex || !data)
        return;

    xSemaphoreTake(client_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_active && clients[i].sock >= 0)
        {
            int err = sendto(clients[i].sock, data, len, 0,
                             (struct sockaddr *)&clients[i].dest_addr,
                             sizeof(clients[i].dest_addr));
            if (err < 0)
            {
                ESP_LOGE(TAG, "Error sending to slot %d: errno %d", i, errno);
            }
            else
            {
                ESP_LOGD(TAG, "Sent to slot %d", i);
            }
        }
    }

    xSemaphoreGive(client_mutex);
}

void udp_read_all(void)
{
    if (!client_mutex)
        return;

    char rx_buffer[RX_BUFFER_SIZE];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    xSemaphoreTake(client_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_active && clients[i].sock >= 0)
        {
            // Try to pull a message from this socket
            int len = recvfrom(clients[i].sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                               (struct sockaddr *)&source_addr, &socklen);

            if (len > 0)
            {
                // do someting with data

                char ip_str[16];
                inet_ntoa_r(source_addr.sin_addr, ip_str, sizeof(ip_str));

                ESP_LOGD(TAG, "Received %d bytes from %s (Slot %d)", len, ip_str, i);
            }
            // If len < 0 and errno is EAGAIN/EWOULDBLOCK, it just means no data is ready yet
            else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                ESP_LOGE(TAG, "Read error on slot %d: errno %d", i, errno);
            }
        }
    }

    xSemaphoreGive(client_mutex);
}