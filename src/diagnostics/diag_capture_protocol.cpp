#include "diag_capture_protocol.h"

#include <string.h>

namespace diag_capture {
namespace {
constexpr uint8_t MAGIC[4] = {'B', 'E', 'D', '1'};

void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8U);
}

void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8U);
    out[2] = static_cast<uint8_t>(value >> 16U);
    out[3] = static_cast<uint8_t>(value >> 24U);
}

uint16_t get_u16(const uint8_t *in)
{
    return static_cast<uint16_t>(in[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8U);
}

uint32_t get_u32(const uint8_t *in)
{
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8U) |
           (static_cast<uint32_t>(in[2]) << 16U) |
           (static_cast<uint32_t>(in[3]) << 24U);
}
} // namespace

uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    if (data == nullptr && length != 0) return 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

bool encode_frame_header(const FrameHeader &header, uint8_t output[HEADER_SIZE])
{
    if (output == nullptr || header.payload_length > MAX_FRAME_PAYLOAD) return false;
    memset(output, 0, HEADER_SIZE);
    memcpy(output, MAGIC, sizeof(MAGIC));
    output[4] = PROTOCOL_VERSION;
    output[5] = static_cast<uint8_t>(header.type);
    put_u16(output + 6, HEADER_SIZE);
    put_u32(output + 8, header.capture_id);
    put_u16(output + 12, header.width);
    put_u16(output + 14, header.height);
    output[16] = header.pixel_format;
    put_u16(output + 18, header.sequence);
    put_u32(output + 20, header.payload_length);
    put_u32(output + 24, header.payload_crc32);
    put_u32(output + 28, crc32(output, 28));
    return true;
}

bool decode_frame_header(const uint8_t input[HEADER_SIZE], FrameHeader &header)
{
    if (input == nullptr || memcmp(input, MAGIC, sizeof(MAGIC)) != 0 ||
        input[4] != PROTOCOL_VERSION || get_u16(input + 6) != HEADER_SIZE ||
        get_u32(input + 28) != crc32(input, 28)) return false;

    const uint8_t type = input[5];
    if (type < static_cast<uint8_t>(FrameType::META) || type > static_cast<uint8_t>(FrameType::ERROR)) return false;
    header.type = static_cast<FrameType>(type);
    header.capture_id = get_u32(input + 8);
    header.width = get_u16(input + 12);
    header.height = get_u16(input + 14);
    header.pixel_format = input[16];
    header.sequence = get_u16(input + 18);
    header.payload_length = get_u32(input + 20);
    header.payload_crc32 = get_u32(input + 24);
    return header.payload_length <= MAX_FRAME_PAYLOAD;
}

} // namespace diag_capture
