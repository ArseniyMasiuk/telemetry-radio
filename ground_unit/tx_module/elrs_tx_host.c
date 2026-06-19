#include "elrs_tx_host.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "elrs_tx_uart.h"

#define ELRS_TX_RC_INTERVAL_MS 10
#define ELRS_TX_RX_BUF_SIZE 128

static const char *TAG = "ELRS-TX-HOST";

static crsf_parser_t s_parser;
static SemaphoreHandle_t s_uart_mutex;
static SemaphoreHandle_t s_response_sem;
static crsf_frame_t s_last_frame;
static uint8_t s_wait_type;
static bool s_wait_pending;
static volatile bool s_host_running;
static uint32_t s_last_link_log_ms;

static void on_frame_received(const crsf_frame_t *frame, void *user_data)
{
    (void)user_data;

    if (frame->type == CRSF_FRAMETYPE_LINK_STATISTICS && frame->payload_len >= 10)
    {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if ((now_ms - s_last_link_log_ms) >= 1000)
        {
            s_last_link_log_ms = now_ms;
            // Spec 0x14: up_rssi_ant1/2 are uint8 stored as dBm * -1
            uint8_t uplink_rssi_1 = frame->payload[0];
            uint8_t uplink_rssi_2 = frame->payload[1];
            uint8_t uplink_lq    = frame->payload[2];
            int8_t  uplink_snr   = (int8_t)frame->payload[3];
            ESP_LOGI(TAG, "Link stats: RSSI1=-%u RSSI2=-%u LQ=%u SNR=%d dBm",
                     uplink_rssi_1, uplink_rssi_2, uplink_lq, uplink_snr);
        }
    }

    if (s_wait_pending && frame->type == s_wait_type)
    {
        memcpy(&s_last_frame, frame, sizeof(s_last_frame));
        s_wait_pending = false;
        xSemaphoreGive(s_response_sem);
    }
}

static void crsf_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[ELRS_TX_RX_BUF_SIZE];

    while (s_host_running)
    {
        int n = elrs_tx_uart_read(buf, sizeof(buf), 20);
        if (n > 0)
        {
            crsf_parser_feed(&s_parser, buf, (size_t)n, on_frame_received, NULL);
        }
    }
    vTaskDelete(NULL);
}

static void crsf_tx_task(void *arg)
{
    (void)arg;
    uint16_t channels[CRSF_NUM_CHANNELS];
    uint8_t frame[CRSF_MAX_FRAME_SIZE];

    for (int i = 0; i < CRSF_NUM_CHANNELS; i++)
    {
        channels[i] = CRSF_CHANNEL_CENTER;
    }

    while (s_host_running)
    {
        size_t frame_len = crsf_build_rc_channels(frame, sizeof(frame), channels);
        if (frame_len > 0)
        {
            if (xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                elrs_tx_uart_write(frame, frame_len);
                xSemaphoreGive(s_uart_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ELRS_TX_RC_INTERVAL_MS));
    }
    vTaskDelete(NULL);
}

esp_err_t elrs_tx_host_init(void)
{
    crsf_parser_init(&s_parser);
    s_uart_mutex = xSemaphoreCreateMutex();
    s_response_sem = xSemaphoreCreateBinary();
    if (!s_uart_mutex || !s_response_sem)
    {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(elrs_tx_uart_init());
    ESP_LOGI(TAG, "ELRS TX host initialized");
    return ESP_OK;
}

esp_err_t elrs_tx_host_start(void)
{
    s_host_running = true;
    BaseType_t rx_ok = xTaskCreate(crsf_rx_task, "elrs_crsf_rx", 4096, NULL, 10, NULL);
    BaseType_t tx_ok = xTaskCreate(crsf_tx_task, "elrs_crsf_tx", 4096, NULL, 9, NULL);
    if (rx_ok != pdPASS || tx_ok != pdPASS)
    {
        s_host_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Handset emulation started (RC stream %d ms)", ELRS_TX_RC_INTERVAL_MS);
    return ESP_OK;
}

esp_err_t elrs_tx_host_send_frame(const uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    int written = elrs_tx_uart_write(data, len);
    xSemaphoreGive(s_uart_mutex);
    return written == (int)len ? ESP_OK : ESP_FAIL;
}

void elrs_tx_host_arm_wait(uint8_t type)
{
    s_wait_type = type;
    s_wait_pending = true;
    xSemaphoreTake(s_response_sem, 0); // drain any stale signal
}

esp_err_t elrs_tx_host_collect_frame(uint32_t timeout_ms, crsf_frame_t *out)
{
    if (!out)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_response_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        s_wait_pending = false;
        return ESP_ERR_TIMEOUT;
    }

    memcpy(out, &s_last_frame, sizeof(*out));
    return ESP_OK;
}

esp_err_t elrs_tx_host_wait_frame(uint8_t type, uint32_t timeout_ms, crsf_frame_t *out)
{
    elrs_tx_host_arm_wait(type);
    return elrs_tx_host_collect_frame(timeout_ms, out);
}
