#pragma once

#include <stddef.h>
#include <stdint.h>

#include "diag_capture_protocol.h"

namespace terminal_capture_ui {

constexpr uint16_t TOTAL_BLOCKS = static_cast<uint16_t>(
    (diag_capture::IMAGE_BYTES + diag_capture::MAX_FRAME_PAYLOAD - 1U) /
    diag_capture::MAX_FRAME_PAYLOAD);
constexpr uint8_t MAX_RETRIES = 3;
static_assert(TOTAL_BLOCKS <= UINT16_MAX, "DIAG.1 sequence must fit in BED1 header");

enum class State : uint8_t {
    IDLE,
    PREPARING,
    SENDING,
    RETRYING,
    VERIFYING,
    RECEIVED,
    ERROR,
};

struct Snapshot {
    State state = State::IDLE;
    uint16_t confirmed_blocks = 0;
    uint16_t expected_sequence = 0;
    uint8_t retries = 0;
    uint8_t error_code = 0;
    bool visible = false;
};

constexpr uint16_t clamp_confirmed_blocks(uint32_t blocks)
{
    return static_cast<uint16_t>(blocks > TOTAL_BLOCKS ? TOTAL_BLOCKS : blocks);
}

constexpr uint8_t percent(uint32_t confirmed_blocks)
{
    return static_cast<uint8_t>(clamp_confirmed_blocks(confirmed_blocks) * 100U / TOTAL_BLOCKS);
}

constexpr bool transition_allowed(State from, State to)
{
    if (from == to) return true;
    switch (from) {
        case State::IDLE: return to == State::PREPARING;
        case State::PREPARING: return to == State::SENDING || to == State::ERROR;
        case State::SENDING:
            return to == State::RETRYING || to == State::VERIFYING || to == State::ERROR;
        case State::RETRYING: return to == State::SENDING || to == State::ERROR;
        case State::VERIFYING: return to == State::RECEIVED || to == State::ERROR;
        case State::RECEIVED:
        case State::ERROR: return to == State::IDLE;
    }
    return false;
}

template <size_t N>
constexpr bool valid_sequence(const State (&states)[N])
{
    if (N == 0) return true;
    for (size_t index = 1; index < N; ++index) {
        if (!transition_allowed(states[index - 1], states[index])) return false;
    }
    return true;
}

constexpr bool progress_is_monotonic(uint32_t previous, uint32_t next)
{
    return next >= previous && next <= TOTAL_BLOCKS;
}

// Compile-time tests run as part of every firmware BUILD.
constexpr State TEST_SUCCESS_SEQUENCE[] = {
    State::IDLE, State::PREPARING, State::SENDING, State::RETRYING,
    State::SENDING, State::VERIFYING, State::RECEIVED, State::IDLE,
};
constexpr State TEST_ERROR_SEQUENCE[] = {
    State::IDLE, State::PREPARING, State::ERROR, State::IDLE,
};
static_assert(TOTAL_BLOCKS == 188, "DIAG.1 screen progress must cover 188 blocks");
static_assert(valid_sequence(TEST_SUCCESS_SEQUENCE), "DIAG.1 success states are out of order");
static_assert(valid_sequence(TEST_ERROR_SEQUENCE), "DIAG.1 error states are out of order");
static_assert(!transition_allowed(State::SENDING, State::RECEIVED),
              "DIAG.1 must verify before showing receipt success");
static_assert(progress_is_monotonic(0, 1) && progress_is_monotonic(187, 188),
              "DIAG.1 acknowledged progress should be monotonic");
static_assert(!progress_is_monotonic(10, 9) && !progress_is_monotonic(188, 189),
              "DIAG.1 progress must not decrease or exceed 750 blocks");
static_assert(percent(0) == 0 && percent(94) == 50 && percent(187) == 99 && percent(188) == 100,
              "DIAG.1 percentage calculation is incorrect");

} // namespace terminal_capture_ui
