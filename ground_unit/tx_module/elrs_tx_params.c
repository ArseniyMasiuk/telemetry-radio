#include "elrs_tx_params.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "crsf_protocol.h"
#include "elrs_tx_host.h"

#define ELRS_TX_PARAM_FETCH_TIMEOUT_MS 2000
#define ELRS_TX_PARAM_READ_TIMEOUT_MS 1500
#define ELRS_TX_BOOT_SETTLE_MS 500

static const char *TAG = "ELRS-TX-PARAMS";

static elrs_tx_params_t s_params;

static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static const char *param_type_name(uint8_t type)
{
    switch (type)
    {
    case CRSF_PARAM_TYPE_UINT8:
        return "uint8";
    case CRSF_PARAM_TYPE_INT8:
        return "int8";
    case CRSF_PARAM_TYPE_UINT16:
        return "uint16";
    case CRSF_PARAM_TYPE_INT16:
        return "int16";
    case CRSF_PARAM_TYPE_FLOAT:
        return "float";
    case CRSF_PARAM_TYPE_TEXT_SELECTION:
        return "text_sel";
    case CRSF_PARAM_TYPE_STRING:
        return "string";
    case CRSF_PARAM_TYPE_FOLDER:
        return "folder";
    case CRSF_PARAM_TYPE_INFO:
        return "info";
    case CRSF_PARAM_TYPE_COMMAND:
        return "command";
    default:
        return "unknown";
    }
}

static void format_text_selection(char *out, size_t out_size, const char *options, uint8_t index)
{
    // Options are semicolon-separated, e.g. "25Hz;50Hz;100Hz".
    // We walk char by char; hitting ';' while on the target option means we are done.
    uint8_t opt = 0;
    size_t pos = 0;

    for (const char *p = options; *p != '\0'; p++)
    {
        if (*p == ';')
        {
            if (opt == index)
                break;
            opt++;
            continue;
        }
        if (opt == index && pos + 1 < out_size)
        {
            out[pos++] = *p;
        }
    }
    out[pos] = '\0';
    if (pos == 0)
    {
        snprintf(out, out_size, "index %u", index);
    }
}

static bool parse_device_info(const crsf_frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->payload_len;

    if (frame->payload_len < 12)
    {
        return false;
    }

    size_t name_len = strnlen((const char *)p, (size_t)(end - p));
    if (name_len >= sizeof(s_params.device_name))
    {
        return false;
    }
    memcpy(s_params.device_name, p, name_len);
    s_params.device_name[name_len] = '\0';
    p += name_len + 1;

    // Need: serial(4) + hw_version(4) + sw_version(4) + param_count(1) = 13 bytes minimum.
    if ((size_t)(end - p) < 13)
    {
        return false;
    }

    s_params.serial = read_u32_be(p);
    p += 4;
    s_params.hw_version = read_u32_be(p);
    p += 4;
    s_params.sw_version = read_u32_be(p);
    p += 4;
    s_params.param_count = *p++;
    if (p < end)
    {
        s_params.param_version = *p++;
    }
    s_params.device_info_valid = true;

    ESP_LOGI(TAG, "Device: %s serial=0x%08" PRIX32 " hw=0x%08" PRIX32 " sw=0x%08" PRIX32
             " params=%u param_ver=%u",
             s_params.device_name, s_params.serial, s_params.hw_version, s_params.sw_version,
             s_params.param_count, s_params.param_version);
    return true;
}

static bool parse_param_payload(const uint8_t *payload, size_t payload_len, elrs_tx_param_entry_t *entry)
{
    const uint8_t *p = payload;
    const uint8_t *end = payload + payload_len;

    if (payload_len < 4)
    {
        return false;
    }

    entry->index = *p++;
    uint8_t chunks_remaining = *p++;
    if (chunks_remaining != 0)
    {
        ESP_LOGW(TAG, "Param %u has %u chunks remaining (partial parse)", entry->index, chunks_remaining);
    }

    if (p >= end)
    {
        return false;
    }

    uint8_t parent_folder = *p++;
    (void)parent_folder;
    if (p >= end)
    {
        return false;
    }

    uint8_t data_type = *p++ & 0x7F;
    entry->type = data_type;

    if (data_type == 127)
    {
        snprintf(entry->name, sizeof(entry->name), "OUT_OF_RANGE");
        snprintf(entry->value, sizeof(entry->value), "end");
        return true;
    }

    const char *name = (const char *)p;
    size_t name_len = strnlen(name, (size_t)(end - p));
    if (name_len >= sizeof(entry->name))
    {
        return false;
    }
    memcpy(entry->name, name, name_len);
    entry->name[name_len] = '\0';
    p += name_len + 1;

    switch (data_type)
    {
    case CRSF_PARAM_TYPE_FLOAT:
        if ((size_t)(end - p) >= 4)
        {
            int32_t value = (int32_t)read_u32_be(p);
            p += 4;
            uint8_t decimal_point = 0;
            if ((size_t)(end - p) >= 13)
            {
                p += 12;
                decimal_point = *p;
            }
            if (decimal_point == 0)
            {
                snprintf(entry->value, sizeof(entry->value), "%ld", (long)value);
            }
            else
            {
                int32_t divisor = 1;
                for (uint8_t i = 0; i < decimal_point; i++)
                {
                    divisor *= 10;
                }
                int32_t frac = value % divisor;
                if (frac < 0)
                {
                    frac = -frac;
                }
                snprintf(entry->value, sizeof(entry->value), "%ld.%0*d",
                         (long)(value / divisor), (int)decimal_point, (int)frac);
            }
        }
        break;

    case CRSF_PARAM_TYPE_TEXT_SELECTION:
    {
        const char *options = (const char *)p;
        size_t options_len = strnlen(options, (size_t)(end - p));
        p += options_len + 1;
        if (p < end)
        {
            format_text_selection(entry->value, sizeof(entry->value), options, *p);
        }
        break;
    }

    case CRSF_PARAM_TYPE_STRING:
    case CRSF_PARAM_TYPE_INFO:
        if (p < end)
        {
            strncpy(entry->value, (const char *)p, sizeof(entry->value) - 1);
            entry->value[sizeof(entry->value) - 1] = '\0';
        }
        break;

    case CRSF_PARAM_TYPE_FOLDER:
        snprintf(entry->value, sizeof(entry->value), "folder");
        break;

    case CRSF_PARAM_TYPE_COMMAND:
        if (p < end)
        {
            snprintf(entry->value, sizeof(entry->value), "status=%u", *p);
        }
        break;

    default:
        snprintf(entry->value, sizeof(entry->value), "raw");
        break;
    }

    return true;
}

static esp_err_t send_parameter_read(uint8_t field_index, uint8_t chunk_index)
{
    uint8_t frame[CRSF_MAX_FRAME_SIZE];
    size_t frame_len = crsf_build_parameter_read(frame, sizeof(frame),
                                                 CRSF_ADDRESS_CRSF_TRANSMITTER,
                                                 CRSF_ADDRESS_RADIO_TRANSMITTER,
                                                 field_index, chunk_index);
    if (frame_len == 0)
    {
        return ESP_FAIL;
    }
    return elrs_tx_host_send_frame(frame, frame_len);
}

static esp_err_t request_device_info(void)
{
    uint8_t frame[CRSF_MAX_FRAME_SIZE];
    size_t frame_len = crsf_build_device_ping(frame, sizeof(frame),
                                              CRSF_ADDRESS_CRSF_TRANSMITTER,
                                              CRSF_ADDRESS_RADIO_TRANSMITTER);
    if (frame_len == 0)
    {
        return ESP_FAIL;
    }

    elrs_tx_host_arm_wait(CRSF_FRAMETYPE_DEVICE_INFO);

    esp_err_t err = elrs_tx_host_send_frame(frame, frame_len);
    if (err != ESP_OK)
    {
        return err;
    }

    crsf_frame_t response;
    err = elrs_tx_host_collect_frame(ELRS_TX_PARAM_FETCH_TIMEOUT_MS, &response);
    if (err != ESP_OK)
    {
        return err;
    }

    if (!parse_device_info(&response))
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t fetch_parameter(uint8_t field_index)
{
    // Reassembly buffer: spec allows up to 8 chunks of ~56 payload bytes each.
    // Keep generous headroom; stack usage is bounded by the 8-chunk guard below.
    uint8_t payload[CRSF_MAX_FRAME_SIZE * 8];
    size_t payload_len = 0;

    for (uint8_t chunk = 0; chunk < 8; chunk++)
    {
        elrs_tx_host_arm_wait(CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY);

        esp_err_t err = send_parameter_read(field_index, chunk);
        if (err != ESP_OK)
        {
            return err;
        }

        crsf_frame_t response;
        err = elrs_tx_host_collect_frame(ELRS_TX_PARAM_READ_TIMEOUT_MS, &response);
        if (err != ESP_OK)
        {
            return err;
        }

        if (response.payload_len < 2)
        {
            return ESP_FAIL;
        }

        uint8_t chunks_remaining = response.payload[1];

        if (chunk == 0)
        {
            // First chunk: store the full payload (param_number + chunks_remaining
            // + first portion of Data_type_payload_chunk).
            size_t copy_len = response.payload_len;
            if (copy_len > sizeof(payload))
            {
                copy_len = sizeof(payload);
            }
            memcpy(payload, response.payload, copy_len);
            payload_len = copy_len;
        }
        else
        {
            // Subsequent chunks: append only the data portion (skip the 2-byte
            // param_number + chunks_remaining header that repeats in each chunk).
            const size_t header = 2;
            if (response.payload_len > header)
            {
                size_t append_len = response.payload_len - header;
                if (payload_len + append_len > sizeof(payload))
                {
                    append_len = sizeof(payload) - payload_len;
                }
                memcpy(payload + payload_len, response.payload + header, append_len);
                payload_len += append_len;
            }
        }

        if (chunks_remaining == 0)
        {
            break;
        }
    }

    if (s_params.num_loaded >= ELRS_TX_MAX_PARAMS)
    {
        return ESP_ERR_NO_MEM;
    }

    elrs_tx_param_entry_t *entry = &s_params.params[s_params.num_loaded];
    if (!parse_param_payload(payload, payload_len, entry))
    {
        return ESP_FAIL;
    }

    if (entry->type == 127)
    {
        return ESP_ERR_NOT_FOUND;
    }

    s_params.num_loaded++;
    return ESP_OK;
}

esp_err_t elrs_tx_params_fetch_all(void)
{
    memset(&s_params, 0, sizeof(s_params));

    vTaskDelay(pdMS_TO_TICKS(ELRS_TX_BOOT_SETTLE_MS));

    esp_err_t err = request_device_info();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "DEVICE_INFO failed: %s", esp_err_to_name(err));
        return err;
    }

    for (uint8_t i = 0; i < s_params.param_count && i < ELRS_TX_MAX_PARAMS; i++)
    {
        err = fetch_parameter(i);
        if (err == ESP_ERR_NOT_FOUND)
        {
            break;
        }
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Param %u fetch failed: %s", i, esp_err_to_name(err));
            continue;
        }
    }

    ESP_LOGI(TAG, "Loaded %u parameters", s_params.num_loaded);
    return s_params.num_loaded > 0 ? ESP_OK : ESP_FAIL;
}

const elrs_tx_params_t *elrs_tx_params_get(void)
{
    return &s_params;
}

void elrs_tx_params_log_all(void)
{
    if (!s_params.device_info_valid)
    {
        ESP_LOGW(TAG, "No device info available");
        return;
    }

    ESP_LOGI(TAG, "========== ELRS TX Parameters ==========");
    ESP_LOGI(TAG, "Device: %s", s_params.device_name);
    ESP_LOGI(TAG, "Serial: 0x%08" PRIX32, s_params.serial);
    ESP_LOGI(TAG, "HW: 0x%08" PRIX32 " SW: 0x%08" PRIX32, s_params.hw_version, s_params.sw_version);
    ESP_LOGI(TAG, "Param version: %u | Reported count: %u | Loaded: %u",
             s_params.param_version, s_params.param_count, s_params.num_loaded);

    for (uint8_t i = 0; i < s_params.num_loaded; i++)
    {
        const elrs_tx_param_entry_t *entry = &s_params.params[i];
        ESP_LOGI(TAG, "[%02u] %-24s (%s) = %s",
                 entry->index, entry->name, param_type_name(entry->type), entry->value);
    }
    ESP_LOGI(TAG, "========================================");
}
