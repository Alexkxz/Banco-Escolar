#include "terminal_capture.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../esp_lv_adapter_arduino.h"
#include "../storage/sd_manager.h"
#include "diag_capture_protocol.h"

namespace {
constexpr uint32_t CAPTURE_PIXELS = diag_capture::IMAGE_WIDTH * diag_capture::IMAGE_HEIGHT;
constexpr uint32_t CAPTURE_BYTES = diag_capture::IMAGE_BYTES;
constexpr uint32_t CAPTURE_TIMEOUT_MS = 2500;
constexpr uint32_t ACK_TIMEOUT_MS = 5000;
constexpr uint32_t RECEIVER_VERIFY_TIMEOUT_MS = 30000;
constexpr uint8_t MAX_CHUNK_RETRIES = terminal_capture_ui::MAX_RETRIES;

enum class AckReply : uint8_t { NONE, ACK, NAK };
enum class AckFailure : uint8_t { NONE, TIMEOUT, NAK };

enum class State : uint8_t { IDLE, REQUESTED, CAPTURING, READY, TRANSFERRING, FAILED };
enum class TransferPhase : uint8_t { NONE, META, SEND_DATA, WAIT_ACK, END, WAIT_VERIFY, SD_STATUS, CLEANUP };

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_ui_mux = portMUX_INITIALIZER_UNLOCKED;
volatile State s_state = State::IDLE;
volatile uint8_t s_error_code = 0;
bool s_show_capture_ui = false;
bool s_capture_ui_view_ready = false;
terminal_capture_ui::Snapshot s_ui_snapshot;
lv_timer_t *s_timer = nullptr;
lv_color_t *s_pixels = nullptr;
uint32_t s_capture_id = 0;
uint32_t s_image_crc = 0;
uint32_t s_capture_started_ms = 0;
uint32_t s_next_capture_id = 1;
uint16_t s_sequence = 0;
uint8_t s_retries = 0;
uint8_t s_line[96] = {};
uint8_t s_line_length = 0;
bool s_line_overflow = false;
bool s_ack_received = false;
bool s_ack_valid = false;
uint32_t s_ack_capture_id = 0;
uint16_t s_ack_sequence = 0;
bool s_busy_response_pending = false;
TransferPhase s_phase = TransferPhase::NONE;
uint32_t s_ack_started_ms = 0;
uint32_t s_block_started_ms = 0;
uint32_t s_last_reply_elapsed_ms = 0;
uint32_t s_last_reply_block_elapsed_ms = 0;
uint32_t s_last_reply_capture_id = 0;
uint16_t s_last_reply_sequence = 0;
uint8_t s_last_reply_retries = 0;
uint8_t s_last_reply_attempt = 0;
AckReply s_last_reply = AckReply::NONE;
AckFailure s_ack_failure = AckFailure::NONE;
bool s_last_reply_matches = false;
bool s_last_reply_in_wait = false;
bool s_ack_timer_pending = false;
bool s_ui_meta_pending = false;
bool s_receiver_verified = false;
bool s_verify_timer_pending = false;
uint32_t s_verify_started_ms = 0;
uint8_t s_packet[diag_capture::HEADER_SIZE + diag_capture::MAX_FRAME_PAYLOAD] = {};
size_t s_tx_length = 0;
size_t s_tx_offset = 0;
bool s_failure_report_queued = false;

State get_state()
{
    portENTER_CRITICAL(&s_mux);
    const State state = s_state;
    portEXIT_CRITICAL(&s_mux);
    return state;
}

bool capture_ui_requested()
{
    portENTER_CRITICAL(&s_mux);
    const bool requested = s_show_capture_ui;
    portEXIT_CRITICAL(&s_mux);
    return requested;
}

bool capture_ui_view_ready()
{
    portENTER_CRITICAL(&s_mux);
    const bool ready = s_capture_ui_view_ready;
    portEXIT_CRITICAL(&s_mux);
    return ready;
}

void publish_ui_snapshot(terminal_capture_ui::State state, uint32_t confirmed_blocks,
                         uint16_t expected_sequence, uint8_t retries, uint8_t error_code,
                         bool visible)
{
    if (!capture_ui_requested()) return;
    portENTER_CRITICAL(&s_ui_mux);
    if (terminal_capture_ui::transition_allowed(s_ui_snapshot.state, state)) {
        s_ui_snapshot.state = state;
        s_ui_snapshot.confirmed_blocks = terminal_capture_ui::clamp_confirmed_blocks(confirmed_blocks);
        s_ui_snapshot.expected_sequence = expected_sequence;
        s_ui_snapshot.retries = retries;
        s_ui_snapshot.error_code = error_code;
        s_ui_snapshot.visible = visible;
    }
    portEXIT_CRITICAL(&s_ui_mux);
}

void log_capture_memory_snapshot(const char *stage)
{
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    Serial.printf("[DIAG1][MEM] %s: interna libre=%u max=%u; PSRAM total=%u libre=%u max=%u\n",
                  stage, static_cast<unsigned>(internal_free), static_cast<unsigned>(internal_largest),
                  static_cast<unsigned>(psram_total), static_cast<unsigned>(psram_free),
                  static_cast<unsigned>(psram_largest));
}

void set_state(State state)
{
    portENTER_CRITICAL(&s_mux);
    s_state = state;
    portEXIT_CRITICAL(&s_mux);
}

void fail(uint8_t code)
{
    portENTER_CRITICAL(&s_mux);
    s_error_code = code;
    s_state = State::FAILED;
    portEXIT_CRITICAL(&s_mux);
    publish_ui_snapshot(terminal_capture_ui::State::ERROR, s_sequence, s_sequence,
                        s_retries, code, true);
}

bool send_frame(diag_capture::FrameType type, uint32_t capture_id,
                uint16_t sequence, const uint8_t *payload, uint32_t payload_length,
                uint16_t width = 0, uint16_t height = 0, uint8_t pixel_format = 0)
{
    if (s_tx_length != 0 || payload_length > diag_capture::MAX_FRAME_PAYLOAD ||
        (payload_length != 0 && payload == nullptr)) return false;

    diag_capture::FrameHeader header;
    header.type = type;
    header.capture_id = capture_id;
    header.width = width;
    header.height = height;
    header.pixel_format = pixel_format;
    header.sequence = sequence;
    header.payload_length = payload_length;
    header.payload_crc32 = diag_capture::crc32(payload, payload_length);
    if (!diag_capture::encode_frame_header(header, s_packet)) return false;
    if (payload_length != 0) memcpy(s_packet + diag_capture::HEADER_SIZE, payload, payload_length);

    s_tx_length = diag_capture::HEADER_SIZE + payload_length;
    s_tx_offset = 0;
    return true;
}

bool send_error(const char *message, uint32_t capture_id = 0)
{
    const uint32_t length = static_cast<uint32_t>(strlen(message));
    return send_frame(diag_capture::FrameType::ERROR, capture_id, 0,
                      reinterpret_cast<const uint8_t *>(message), length);
}

void cleanup_capture()
{
    if (s_pixels != nullptr) {
        free(s_pixels);
        s_pixels = nullptr;
    }
    s_capture_id = 0;
    s_image_crc = 0;
    s_sequence = 0;
    s_retries = 0;
    s_phase = TransferPhase::NONE;
    s_tx_length = 0;
    s_tx_offset = 0;
    s_ack_timer_pending = false;
    s_failure_report_queued = false;
    s_ack_received = false;
    s_ack_valid = false;
    set_state(State::IDLE);
}

void parse_line(const char *line)
{
    if (strcmp(line, diag_capture::REQUEST_LINE) == 0) {
        if (!terminal_capture_request()) s_busy_response_pending = true;
        return;
    }

    unsigned long verified_capture_id = 0;
    unsigned long verified_checksum = 0;
    if (sscanf(line, "DIAG1 VERIFIED %8lx %8lx", &verified_capture_id, &verified_checksum) == 2) {
        if (get_state() == State::TRANSFERRING && s_phase == TransferPhase::WAIT_VERIFY &&
            static_cast<uint32_t>(verified_capture_id) == s_capture_id &&
            static_cast<uint32_t>(verified_checksum) == s_image_crc) {
            s_receiver_verified = true;
        }
        return;
    }

    unsigned long capture_id = 0;
    unsigned sequence = 0;
    if (sscanf(line, "DIAG1 ACK %8lx %u", &capture_id, &sequence) == 2 ||
        sscanf(line, "DIAG1 NAK %8lx %u", &capture_id, &sequence) == 2) {
        const bool is_ack = strncmp(line, "DIAG1 ACK ", 10) == 0;
        if (sequence <= UINT16_MAX && get_state() == State::TRANSFERRING) {
            const uint32_t now = millis();
            s_last_reply_in_wait = s_phase == TransferPhase::WAIT_ACK;
            s_last_reply = is_ack ? AckReply::ACK : AckReply::NAK;
            s_last_reply_capture_id = static_cast<uint32_t>(capture_id);
            s_last_reply_sequence = static_cast<uint16_t>(sequence);
            s_last_reply_retries = s_retries;
            s_last_reply_attempt = static_cast<uint8_t>(s_retries + (s_last_reply_in_wait ? 1U : 0U));
            s_last_reply_matches = s_last_reply_in_wait &&
                                   s_last_reply_capture_id == s_capture_id &&
                                   s_last_reply_sequence == s_sequence;
            s_last_reply_elapsed_ms = s_ack_timer_pending ? 0 : now - s_ack_started_ms;
            s_last_reply_block_elapsed_ms = s_block_started_ms == 0 ? 0 : now - s_block_started_ms;
            if (s_last_reply_in_wait) {
                s_ack_capture_id = static_cast<uint32_t>(capture_id);
                s_ack_sequence = static_cast<uint16_t>(sequence);
                s_ack_valid = is_ack;
                s_ack_received = true;
            }
        }
    }
}

void read_serial_lines()
{
    while (Serial.available() > 0) {
        const int value = Serial.read();
        if (value < 0) break;
        const char ch = static_cast<char>(value);
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (!s_line_overflow && s_line_length > 0) {
                s_line[s_line_length] = 0;
                parse_line(reinterpret_cast<const char *>(s_line));
            }
            s_line_length = 0;
            s_line_overflow = false;
        } else if (!s_line_overflow) {
            if (s_line_length + 1 < sizeof(s_line)) s_line[s_line_length++] = static_cast<uint8_t>(ch);
            else s_line_overflow = true;
        }
    }
}

void capture_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    const State state = get_state();
    if (state == State::REQUESTED) {
        log_capture_memory_snapshot("antes de preparar captura");
        const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (psram_total == 0) {
            Serial.println("[DIAG1][MEM] Captura cancelada: PSRAM no disponible");
            fail(1);
            return;
        }
        if (psram_largest < CAPTURE_BYTES) {
            Serial.printf("[DIAG1][MEM] Captura cancelada: bloque PSRAM máximo %u; se requieren %u bytes para la imagen\n",
                          static_cast<unsigned>(psram_largest), static_cast<unsigned>(CAPTURE_BYTES));
            fail(8);
            return;
        }

        s_pixels = static_cast<lv_color_t *>(heap_caps_malloc(CAPTURE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        Serial.printf("[DIAG1][MEM] Reserva de imagen (%u bytes): %s (%p)\n",
                      static_cast<unsigned>(CAPTURE_BYTES), s_pixels != nullptr ? "OK" : "FALLO", s_pixels);
        log_capture_memory_snapshot("después de reservar imagen");
        if (s_pixels == nullptr) {
            fail(8);
            return;
        }
        const esp_err_t capture_begin_result = esp_lv_adapter_capture_begin(s_pixels, CAPTURE_PIXELS);
        if (capture_begin_result != ESP_OK) {
            Serial.printf("[DIAG1][MEM] Preparación de captura cancelada: %s (%d)\n",
                          esp_err_to_name(capture_begin_result), static_cast<int>(capture_begin_result));
            heap_caps_free(s_pixels);
            s_pixels = nullptr;
            fail(capture_begin_result == ESP_ERR_NO_MEM ? 9 : 2);
            return;
        }
        s_capture_started_ms = millis();
        set_state(State::CAPTURING);
        lv_obj_invalidate(lv_scr_act());
        // This timer runs inside LVGL's locked task. Render the invalidated
        // screen now so every captured rectangle belongs to one LVGL refresh
        // cycle; the flush callback still performs the normal LCD submission.
        lv_refr_now(nullptr);
        if (esp_lv_adapter_capture_is_complete()) {
            esp_lv_adapter_capture_cancel();
            set_state(State::READY);
            publish_ui_snapshot(terminal_capture_ui::State::PREPARING, 0, 0, 0, 0, true);
        }
        return;
    }

    if (state != State::CAPTURING) return;
    if (esp_lv_adapter_capture_is_complete()) {
        esp_lv_adapter_capture_cancel();
        set_state(State::READY);
        publish_ui_snapshot(terminal_capture_ui::State::PREPARING, 0, 0, 0, 0, true);
    } else if (static_cast<uint32_t>(millis() - s_capture_started_ms) >= CAPTURE_TIMEOUT_MS) {
        esp_lv_adapter_capture_cancel();
        fail(3);
    }
}

void begin_transfer()
{
    s_capture_id = s_next_capture_id++;
    if (s_next_capture_id == 0) s_next_capture_id = 1;
    s_image_crc = diag_capture::crc32(reinterpret_cast<const uint8_t *>(s_pixels), CAPTURE_BYTES);
    s_sequence = 0;
    s_retries = 0;
    s_phase = TransferPhase::META;
    s_ui_meta_pending = false;
    s_receiver_verified = false;
    s_verify_timer_pending = false;
    publish_ui_snapshot(terminal_capture_ui::State::PREPARING, 0, 0, 0, 0, true);
    set_state(State::TRANSFERRING);
}

void send_meta()
{
    uint8_t metadata[8];
    const uint32_t length = CAPTURE_BYTES;
    metadata[0] = static_cast<uint8_t>(length);
    metadata[1] = static_cast<uint8_t>(length >> 8U);
    metadata[2] = static_cast<uint8_t>(length >> 16U);
    metadata[3] = static_cast<uint8_t>(length >> 24U);
    metadata[4] = static_cast<uint8_t>(s_image_crc);
    metadata[5] = static_cast<uint8_t>(s_image_crc >> 8U);
    metadata[6] = static_cast<uint8_t>(s_image_crc >> 16U);
    metadata[7] = static_cast<uint8_t>(s_image_crc >> 24U);
    if (!send_frame(diag_capture::FrameType::META, s_capture_id, 0, metadata, sizeof(metadata),
                    diag_capture::IMAGE_WIDTH, diag_capture::IMAGE_HEIGHT,
                    diag_capture::PIXEL_FORMAT_RGB565_LE)) {
        fail(4);
        return;
    }
    s_phase = TransferPhase::SEND_DATA;
    s_ui_meta_pending = true;
}

void send_chunk()
{
    const uint32_t offset = static_cast<uint32_t>(s_sequence) * diag_capture::MAX_FRAME_PAYLOAD;
    const uint32_t length = (CAPTURE_BYTES - offset > diag_capture::MAX_FRAME_PAYLOAD)
        ? diag_capture::MAX_FRAME_PAYLOAD : CAPTURE_BYTES - offset;
    if (!send_frame(diag_capture::FrameType::DATA, s_capture_id, s_sequence,
                    reinterpret_cast<const uint8_t *>(s_pixels) + offset, length,
                    diag_capture::IMAGE_WIDTH, diag_capture::IMAGE_HEIGHT,
                    diag_capture::PIXEL_FORMAT_RGB565_LE)) {
        fail(5);
        return;
    }
    if (s_retries == 0) {
        s_block_started_ms = 0;
        s_last_reply_elapsed_ms = 0;
        s_last_reply_block_elapsed_ms = 0;
        s_last_reply_capture_id = 0;
        s_last_reply_sequence = 0;
        s_last_reply_retries = 0;
        s_last_reply_attempt = 0;
        s_last_reply = AckReply::NONE;
        s_last_reply_matches = false;
        s_last_reply_in_wait = false;
        s_ack_failure = AckFailure::NONE;
    }
    s_ack_received = false;
    s_ack_timer_pending = true;
    s_phase = TransferPhase::WAIT_ACK;
}

void retry_or_fail(AckFailure failure)
{
    if (s_retries < MAX_CHUNK_RETRIES) {
        ++s_retries;
        s_phase = TransferPhase::SEND_DATA;
        publish_ui_snapshot(terminal_capture_ui::State::RETRYING, s_sequence,
                            s_sequence, s_retries, 0, true);
    } else {
        s_ack_failure = failure;
        fail(6);
    }
}

void format_ack_failure(char *output, size_t output_size)
{
    const char *failure = s_ack_failure == AckFailure::NAK ? "NAK" : "TIMEOUT";
    const char *reply = s_last_reply == AckReply::ACK ? "ACK" :
                        s_last_reply == AckReply::NAK ? "NAK" : "NONE";
    const uint32_t now = millis();
    const uint32_t block_elapsed = s_block_started_ms == 0 ? 0 : now - s_block_started_ms;
    const uint32_t attempt_elapsed = s_ack_timer_pending ? 0 : now - s_ack_started_ms;
    const unsigned reply_attempt = static_cast<unsigned>(s_last_reply_attempt);
    snprintf(output, output_size,
             "ACK_TIMEOUT_OR_RETRY_LIMIT id=%08lX seq=%u retries=%u cause=%s "
             "reply=%s reply_id=%08lX reply_seq=%u match=%u reply_wait=%u reply_attempt=%u "
             "reply_wait_ms=%lu reply_block_ms=%lu final_wait_ms=%lu block_ms=%lu",
             static_cast<unsigned long>(s_capture_id), static_cast<unsigned>(s_sequence),
             static_cast<unsigned>(s_retries), failure, reply,
             static_cast<unsigned long>(s_last_reply_capture_id),
             static_cast<unsigned>(s_last_reply_sequence), s_last_reply_matches ? 1U : 0U,
             s_last_reply_in_wait ? 1U : 0U, reply_attempt,
             static_cast<unsigned long>(s_last_reply_elapsed_ms),
             static_cast<unsigned long>(s_last_reply_block_elapsed_ms),
             static_cast<unsigned long>(attempt_elapsed), static_cast<unsigned long>(block_elapsed));
}

void send_end()
{
    uint8_t summary[8];
    const uint32_t length = CAPTURE_BYTES;
    summary[0] = static_cast<uint8_t>(length);
    summary[1] = static_cast<uint8_t>(length >> 8U);
    summary[2] = static_cast<uint8_t>(length >> 16U);
    summary[3] = static_cast<uint8_t>(length >> 24U);
    summary[4] = static_cast<uint8_t>(s_image_crc);
    summary[5] = static_cast<uint8_t>(s_image_crc >> 8U);
    summary[6] = static_cast<uint8_t>(s_image_crc >> 16U);
    summary[7] = static_cast<uint8_t>(s_image_crc >> 24U);
    if (!send_frame(diag_capture::FrameType::END, s_capture_id, s_sequence,
                    summary, sizeof(summary), diag_capture::IMAGE_WIDTH,
                    diag_capture::IMAGE_HEIGHT, diag_capture::PIXEL_FORMAT_RGB565_LE)) {
        fail(7);
        return;
    }
    s_receiver_verified = false;
    s_verify_timer_pending = true;
    s_phase = TransferPhase::WAIT_VERIFY;
}

const char *sd_capture_status()
{
    return sd_manager.isReady()
        ? "SD disponible; guardado de captura aún no implementado"
        : "SD no disponible";
}

void send_sd_status()
{
    const char *message = sd_capture_status();
    send_frame(diag_capture::FrameType::STATUS, s_capture_id, 0,
               reinterpret_cast<const uint8_t *>(message), static_cast<uint32_t>(strlen(message)));
    s_phase = TransferPhase::CLEANUP;
}
} // namespace

bool terminal_capture_init()
{
    s_next_capture_id = esp_random();
    if (s_next_capture_id == 0) s_next_capture_id = 1;
    s_timer = lv_timer_create(capture_timer_cb, 20, nullptr);
    return s_timer != nullptr;
}

bool terminal_capture_request(bool show_ui)
{
    bool accepted = false;
    portENTER_CRITICAL(&s_mux);
    if (s_state == State::IDLE) {
        s_state = State::REQUESTED;
        s_show_capture_ui = show_ui;
        s_capture_ui_view_ready = !show_ui;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (accepted && show_ui) {
        publish_ui_snapshot(terminal_capture_ui::State::PREPARING, 0, 0, 0, 0, false);
    }
    return accepted;
}

bool terminal_capture_get_ui_snapshot(terminal_capture_ui::Snapshot *snapshot)
{
    if (snapshot == nullptr) return false;
    portENTER_CRITICAL(&s_ui_mux);
    *snapshot = s_ui_snapshot;
    portEXIT_CRITICAL(&s_ui_mux);
    return snapshot->visible;
}

void terminal_capture_ui_view_ready()
{
    portENTER_CRITICAL(&s_mux);
    if (s_show_capture_ui) s_capture_ui_view_ready = true;
    portEXIT_CRITICAL(&s_mux);
}

void terminal_capture_ui_dismiss()
{
    portENTER_CRITICAL(&s_mux);
    s_show_capture_ui = false;
    s_capture_ui_view_ready = false;
    portEXIT_CRITICAL(&s_mux);
    portENTER_CRITICAL(&s_ui_mux);
    s_ui_snapshot = terminal_capture_ui::Snapshot{};
    portEXIT_CRITICAL(&s_ui_mux);
}

void terminal_capture_poll()
{
    read_serial_lines();

    // Drain USB in bounded, non-blocking slices from loop(). The LVGL flush
    // callback never performs protocol I/O, and one poll cannot wait for a
    // complete 1 KiB frame to leave the serial TX buffer.
    if (s_tx_offset < s_tx_length) {
        const int available = Serial.availableForWrite();
        if (available <= 0) return;
        const size_t remaining = s_tx_length - s_tx_offset;
        const size_t count = remaining < static_cast<size_t>(available)
            ? remaining : static_cast<size_t>(available);
        const size_t written = Serial.write(s_packet + s_tx_offset, count);
        s_tx_offset += written;
        if (s_tx_offset < s_tx_length) return;
        s_tx_length = 0;
        s_tx_offset = 0;
        if (s_ack_timer_pending) {
            const uint32_t now = millis();
            if (s_retries == 0) s_block_started_ms = now;
            s_ack_started_ms = now;
            s_ack_timer_pending = false;
        }
        if (s_ui_meta_pending) {
            s_ui_meta_pending = false;
            publish_ui_snapshot(terminal_capture_ui::State::SENDING, 0, 0, 0, 0, true);
        }
        if (s_verify_timer_pending) {
            s_verify_started_ms = millis();
            s_verify_timer_pending = false;
        }
        if (s_failure_report_queued) {
            cleanup_capture();
            return;
        }
        // Give the main loop and LVGL task a scheduling point between frames.
        return;
    }

    if (s_failure_report_queued) {
        cleanup_capture();
        return;
    }

    if (s_busy_response_pending) {
        if (send_error("BUSY: solicitud de captura en curso"))
            s_busy_response_pending = false;
        return;
    }

    State state = get_state();
    if (state == State::FAILED) {
        static const char *const errors[] = {
            "ERROR", "NO_PSRAM_MEMORY", "CAPTURE_START_FAILED", "CAPTURE_TIMEOUT",
            "SERIAL_WRITE_FAILED", "CHUNK_WRITE_FAILED", "ACK_TIMEOUT_OR_RETRY_LIMIT",
            "END_WRITE_FAILED", "CAPTURE_IMAGE_ALLOCATION_FAILED", "CAPTURE_COVERAGE_ALLOCATION_FAILED",
            "RECEIVER_VERIFY_TIMEOUT"
        };
        const uint8_t code = s_error_code;
        if (code == 6) {
            char diagnostic[256];
            format_ack_failure(diagnostic, sizeof(diagnostic));
            s_failure_report_queued = send_error(diagnostic, s_capture_id);
        } else {
            s_failure_report_queued = send_error(
                code < sizeof(errors) / sizeof(errors[0]) ? errors[code] : "CAPTURE_FAILED", s_capture_id);
        }
        if (!s_failure_report_queued) cleanup_capture();
        return;
    }
    if (state == State::READY) {
        if (capture_ui_requested() && !capture_ui_view_ready()) return;
        begin_transfer();
    }
    if (get_state() != State::TRANSFERRING) return;

    switch (s_phase) {
        case TransferPhase::META:
            send_meta();
            break;
        case TransferPhase::SEND_DATA:
            send_chunk();
            break;
        case TransferPhase::WAIT_ACK:
            if (s_ack_received && s_ack_capture_id == s_capture_id && s_ack_sequence == s_sequence) {
                s_ack_received = false;
                if (s_ack_valid) {
                    ++s_sequence;
                    s_retries = 0;
                    const bool all_blocks_confirmed = s_sequence >= terminal_capture_ui::TOTAL_BLOCKS;
                    s_phase = all_blocks_confirmed ? TransferPhase::END : TransferPhase::SEND_DATA;
                    publish_ui_snapshot(all_blocks_confirmed ? terminal_capture_ui::State::VERIFYING :
                                                               terminal_capture_ui::State::SENDING,
                                        s_sequence, s_sequence, 0, 0, true);
                } else retry_or_fail(AckFailure::NAK);
            } else if (!s_ack_timer_pending &&
                       static_cast<uint32_t>(millis() - s_ack_started_ms) >= ACK_TIMEOUT_MS) {
                retry_or_fail(AckFailure::TIMEOUT);
            }
            break;
        case TransferPhase::END:
            send_end();
            break;
        case TransferPhase::WAIT_VERIFY:
            if (s_receiver_verified) {
                s_phase = TransferPhase::SD_STATUS;
                publish_ui_snapshot(terminal_capture_ui::State::RECEIVED,
                                    terminal_capture_ui::TOTAL_BLOCKS,
                                    terminal_capture_ui::TOTAL_BLOCKS, 0, 0, true);
            } else if (!s_verify_timer_pending &&
                       static_cast<uint32_t>(millis() - s_verify_started_ms) >= RECEIVER_VERIFY_TIMEOUT_MS) {
                fail(10);
            }
            break;
        case TransferPhase::SD_STATUS:
            send_sd_status();
            break;
        case TransferPhase::CLEANUP:
            cleanup_capture();
            break;
        case TransferPhase::NONE:
            fail(7);
            break;
    }
}
