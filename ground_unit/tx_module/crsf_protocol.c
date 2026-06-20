#include "crsf_protocol.h"

#include <string.h>

// Spec: frames with type >= 0x28 have an extended header (dest + origin bytes)
// with the following exceptions which keep the simple broadcast header.
static bool is_extended_frame(uint8_t type)
{
    if (type < CRSF_FRAMETYPE_DEVICE_PING)
    {
        return false;
    }
    switch (type)
    {
    case 0x34: // Logging
    case 0x80: // ArduPilot passthrough
    case 0x81: // mLRS RC→TX
    case 0x82: // mLRS TX→RC
    case 0x88: // Rotorflight telemetry
    case 0xAA: // CRSF MAVLink envelope
    case 0xAC: // CRSF MAVLink system status
        return false;
    default:
        return true;
    }
}

// Spec: sync byte may be 0xC8 (serial sync), 0x00 (broadcast), or any device
// address. Accept the addresses relevant for handset ↔ TX module traffic.
static bool is_valid_sync(uint8_t b)
{
    switch (b)
    {
    case CRSF_SYNC_BYTE:                   // 0xC8 — also the FC address
    case CRSF_ADDRESS_BROADCAST:           // 0x00
    case CRSF_ADDRESS_RADIO_TRANSMITTER:   // 0xEA
    case CRSF_ADDRESS_CRSF_TRANSMITTER:    // 0xEE
    case CRSF_ADDRESS_RC_RECEIVER:         // 0xEC
        return true;
    default:
        return false;
    }
}

static uint8_t crc8_update(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (int i = 0; i < 8; i++)
    {
        if (crc & 0x80)
        {
            crc = (uint8_t)((crc << 1) ^ 0xD5);
        }
        else
        {
            crc <<= 1;
        }
    }
    return crc;
}

uint8_t crsf_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++)
    {
        crc = crc8_update(crc, data[i]);
    }
    return crc;
}

static void pack_channels(uint8_t *payload, const uint16_t *channels)
{
    // 16 channels × 11 bits = 176 bits = 22 bytes exactly, no trailing bits.
    uint32_t bits = 0;
    uint8_t bits_avail = 0;
    uint8_t *dest = payload;

    for (int i = 0; i < CRSF_NUM_CHANNELS; i++)
    {
        bits |= ((uint32_t)(channels[i] & 0x7FF)) << bits_avail;
        bits_avail += 11;
        while (bits_avail >= 8)
        {
            *dest++ = (uint8_t)(bits & 0xFF);
            bits >>= 8;
            bits_avail -= 8;
        }
    }
}

static size_t build_frame(uint8_t *out, size_t out_size, uint8_t addr, uint8_t type,
                          const uint8_t *payload, size_t payload_len,
                          bool extended, uint8_t ext_dest, uint8_t ext_src)
{
    size_t frame_len = 2 + payload_len + 2;
    if (extended)
    {
        frame_len += 2;
    }
    if (frame_len > CRSF_MAX_FRAME_SIZE || frame_len > out_size)
    {
        return 0;
    }

    out[0] = addr;
    out[1] = (uint8_t)(frame_len - 2);
    out[2] = type;

    size_t idx = 3;
    if (extended)
    {
        out[idx++] = ext_dest;
        out[idx++] = ext_src;
    }
    if (payload_len > 0)
    {
        memcpy(&out[idx], payload, payload_len);
        idx += payload_len;
    }

    out[idx] = crsf_crc8(&out[2], idx - 2);
    return idx + 1;
}

size_t crsf_build_rc_channels(uint8_t *out, size_t out_size, const uint16_t *channels)
{
    uint8_t payload[CRSF_RC_CHANNELS_PAYLOAD_SIZE];
    pack_channels(payload, channels);
    return build_frame(out, out_size, CRSF_SYNC_BYTE,
                       CRSF_FRAMETYPE_RC_CHANNELS_PACKED, payload, sizeof(payload),
                       false, 0, 0);
}

size_t crsf_build_heartbeat(uint8_t *out, size_t out_size, uint8_t origin)
{
    // First byte must be the handset address (0xEE) so the TX module recognises
    // the sender. Payload: origin device address as uint16_t little-endian.
    uint8_t payload[2] = {origin, 0x00};
    return build_frame(out, out_size, CRSF_ADDRESS_CRSF_TRANSMITTER,
                       CRSF_FRAMETYPE_HEARTBEAT, payload, sizeof(payload),
                       false, 0, 0);
}

size_t crsf_build_device_ping(uint8_t *out, size_t out_size, uint8_t dest, uint8_t src)
{
    return build_frame(out, out_size, CRSF_SYNC_BYTE,
                       CRSF_FRAMETYPE_DEVICE_PING, NULL, 0, true, dest, src);
}

size_t crsf_build_parameter_read(uint8_t *out, size_t out_size, uint8_t dest, uint8_t src,
                                 uint8_t field_index, uint8_t chunk_index)
{
    uint8_t payload[2] = {field_index, chunk_index};
    return build_frame(out, out_size, CRSF_SYNC_BYTE,
                       CRSF_FRAMETYPE_PARAMETER_READ, payload, sizeof(payload),
                       true, dest, src);
}

bool crsf_parse_frame(const uint8_t *data, size_t len, crsf_frame_t *frame)
{
    if (len < 4 || !is_valid_sync(data[0]))
    {
        return false;
    }

    uint8_t declared_len = data[1];
    if (declared_len < 2 || (size_t)(declared_len + 2) > len)
    {
        return false;
    }

    size_t frame_len = declared_len + 2;
    uint8_t expected_crc = crsf_crc8(&data[2], declared_len - 1);
    if (expected_crc != data[frame_len - 1])
    {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    memcpy(frame->data, data, frame_len);
    frame->len = frame_len;
    frame->type = data[2];
    frame->extended = is_extended_frame(frame->type);

    if (frame->extended)
    {
        if (declared_len < 4)
        {
            return false;
        }
        frame->ext_dest = data[3];
        frame->ext_src = data[4];
        // Point into frame->data (our own copy), not into the caller's buffer.
        // This keeps payload valid after the input buffer is recycled.
        frame->payload = &frame->data[5];
        frame->payload_len = declared_len - 4;
    }
    else
    {
        frame->payload = &frame->data[3];
        frame->payload_len = declared_len - 2;
    }

    return true;
}

void crsf_parser_init(crsf_parser_t *parser)
{
    parser->write_pos = 0;
}

size_t crsf_parser_feed(crsf_parser_t *parser, const uint8_t *data, size_t len,
                        crsf_frame_cb_t cb, void *user_data)
{
    size_t frames = 0;

    for (size_t i = 0; i < len; i++)
    {
        uint8_t byte = data[i];

        if (parser->write_pos == 0)
        {
            if (!is_valid_sync(byte))
            {
                continue;
            }
            parser->buffer[parser->write_pos++] = byte;
            continue;
        }

        parser->buffer[parser->write_pos++] = byte;

        if (parser->write_pos == 2)
        {
            if (parser->buffer[1] < 2 || parser->buffer[1] > (CRSF_MAX_FRAME_SIZE - 2))
            {
                parser->write_pos = 0;
            }
            continue;
        }

        size_t expected_len = (size_t)parser->buffer[1] + 2;
        if (parser->write_pos >= expected_len)
        {
            crsf_frame_t frame;
            if (crsf_parse_frame(parser->buffer, expected_len, &frame) && cb)
            {
                cb(&frame, user_data);
            }
            frames++;

            size_t leftover = parser->write_pos - expected_len;
            if (leftover > 0)
            {
                memmove(parser->buffer, &parser->buffer[expected_len], leftover);
            }
            parser->write_pos = leftover;
        }

        if (parser->write_pos >= sizeof(parser->buffer))
        {
            parser->write_pos = 0;
        }
    }

    return frames;
}
