#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRSF_MAX_FRAME_SIZE 64
#define CRSF_SYNC_BYTE 0xC8
#define CRSF_RC_CHANNELS_PAYLOAD_SIZE 22
#define CRSF_NUM_CHANNELS 16
#define CRSF_CHANNEL_CENTER 992

#define CRSF_ADDRESS_BROADCAST          0x00
#define CRSF_ADDRESS_FLIGHT_CONTROLLER  0xC8
#define CRSF_ADDRESS_RC_RECEIVER        0xEC
#define CRSF_ADDRESS_RADIO_TRANSMITTER  0xEA
#define CRSF_ADDRESS_CRSF_TRANSMITTER   0xEE

#define CRSF_FRAMETYPE_LINK_STATISTICS 0x14
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAMETYPE_DEVICE_PING 0x28
#define CRSF_FRAMETYPE_DEVICE_INFO 0x29
#define CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY 0x2B
#define CRSF_FRAMETYPE_PARAMETER_READ 0x2C

#define CRSF_PARAM_TYPE_UINT8 0x00
#define CRSF_PARAM_TYPE_INT8 0x01
#define CRSF_PARAM_TYPE_UINT16 0x02
#define CRSF_PARAM_TYPE_INT16 0x03
#define CRSF_PARAM_TYPE_FLOAT 0x08
#define CRSF_PARAM_TYPE_TEXT_SELECTION 0x09
#define CRSF_PARAM_TYPE_STRING 0x0A
#define CRSF_PARAM_TYPE_FOLDER 0x0B
#define CRSF_PARAM_TYPE_INFO 0x0C
#define CRSF_PARAM_TYPE_COMMAND 0x0D

typedef struct
{
    uint8_t data[CRSF_MAX_FRAME_SIZE];
    size_t len;
    uint8_t type;
    bool extended;
    uint8_t ext_dest;
    uint8_t ext_src;
    const uint8_t *payload;
    size_t payload_len;
} crsf_frame_t;

typedef struct
{
    uint8_t buffer[CRSF_MAX_FRAME_SIZE * 2];
    size_t write_pos;
} crsf_parser_t;

typedef void (*crsf_frame_cb_t)(const crsf_frame_t *frame, void *user_data);

uint8_t crsf_crc8(const uint8_t *data, size_t len);
size_t crsf_build_rc_channels(uint8_t *out, size_t out_size, const uint16_t *channels);
size_t crsf_build_device_ping(uint8_t *out, size_t out_size, uint8_t dest, uint8_t src);
size_t crsf_build_parameter_read(uint8_t *out, size_t out_size, uint8_t dest, uint8_t src,
                                 uint8_t field_index, uint8_t chunk_index);
bool crsf_parse_frame(const uint8_t *data, size_t len, crsf_frame_t *frame);
void crsf_parser_init(crsf_parser_t *parser);
size_t crsf_parser_feed(crsf_parser_t *parser, const uint8_t *data, size_t len,
                        crsf_frame_cb_t cb, void *user_data);
