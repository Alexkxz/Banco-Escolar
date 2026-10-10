#pragma once

#include <stddef.h>
#include <stdint.h>

namespace diag_capture {

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint32_t SERIAL_BAUD_RATE = 460800;
constexpr uint16_t HEADER_SIZE = 32;
constexpr uint32_t MAX_FRAME_PAYLOAD = 4096;
constexpr uint32_t IMAGE_WIDTH = 800;
constexpr uint32_t IMAGE_HEIGHT = 480;
constexpr uint32_t IMAGE_BYTES = IMAGE_WIDTH * IMAGE_HEIGHT * 2;
constexpr uint8_t PIXEL_FORMAT_RGB565_LE = 1;
constexpr char REQUEST_LINE[] = "DIAG1 CAPTURE";

enum class FrameType : uint8_t {
    META = 1,
    DATA = 2,
    END = 3,
    STATUS = 4,
    ERROR = 5,
};

struct FrameHeader {
    FrameType type = FrameType::ERROR;
    uint32_t capture_id = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t pixel_format = 0;
    uint16_t sequence = 0;
    uint32_t payload_length = 0;
    uint32_t payload_crc32 = 0;
};

uint32_t crc32(const uint8_t *data, size_t length);
bool encode_frame_header(const FrameHeader &header, uint8_t output[HEADER_SIZE]);
bool decode_frame_header(const uint8_t input[HEADER_SIZE], FrameHeader &header);

} // namespace diag_capture
