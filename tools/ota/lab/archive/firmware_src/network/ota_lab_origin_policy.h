#pragma once

#include <cstdint>

namespace ota_lab_origin_policy {

// Values are defined by ESP-IDF's esp_ota_img_states_t. Only a cryptographically
// authenticated lab installer may use the legacy UNDEFINED state; VALID is
// also accepted only after the same image-identity check.
constexpr uint32_t kUndefined = 0xFFFFFFFFU;
constexpr uint32_t kNew = 0x0U;
constexpr uint32_t kPendingVerify = 0x1U;
constexpr uint32_t kValid = 0x2U;
constexpr uint32_t kInvalid = 0x3U;
constexpr uint32_t kAborted = 0x4U;

constexpr bool mayUseRunningImage(bool stateQuerySucceeded,
                                  uint32_t state,
                                  bool approvedIdentityVerified)
{
    return stateQuerySucceeded && approvedIdentityVerified &&
           (state == kValid || state == kUndefined);
}

// Compile-time state matrix: compiled into every installer build.
static_assert(mayUseRunningImage(true, kValid, true));
static_assert(mayUseRunningImage(true, kUndefined, true));
static_assert(!mayUseRunningImage(false, kValid, true));
static_assert(!mayUseRunningImage(true, kValid, false));
static_assert(!mayUseRunningImage(true, kNew, true));
static_assert(!mayUseRunningImage(true, kPendingVerify, true));
static_assert(!mayUseRunningImage(true, kInvalid, true));
static_assert(!mayUseRunningImage(true, kAborted, true));
static_assert(!mayUseRunningImage(true, 0xDEADBEEFU, true));

}  // namespace ota_lab_origin_policy
