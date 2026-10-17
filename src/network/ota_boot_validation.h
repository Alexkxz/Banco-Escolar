#pragma once

#include <atomic>
#include <cstdint>

// Proof is volatile by design: every MCU reset clears both acknowledgements.
// The nonce binds UI events and health checks to this pending boot only.
class OtaBootValidationProof {
public:
    void begin(uint32_t boot_id)
    {
        screen_acknowledged_.store(false, std::memory_order_relaxed);
        touch_verified_.store(false, std::memory_order_relaxed);
        boot_id_.store(boot_id, std::memory_order_release);
    }

    void clear()
    {
        boot_id_.store(0, std::memory_order_release);
        screen_acknowledged_.store(false, std::memory_order_relaxed);
        touch_verified_.store(false, std::memory_order_relaxed);
    }

    bool acknowledgeScreen(uint32_t boot_id)
    {
        if (!isCurrentBoot(boot_id)) return false;
        screen_acknowledged_.store(true, std::memory_order_release);
        return true;
    }

    bool verifyTouch(uint32_t boot_id)
    {
        if (!isCurrentBoot(boot_id)) return false;
        touch_verified_.store(true, std::memory_order_release);
        return true;
    }

    bool isCurrentBoot(uint32_t boot_id) const
    {
        return boot_id != 0 && boot_id_.load(std::memory_order_acquire) == boot_id;
    }

    bool complete(uint32_t boot_id) const
    {
        return isCurrentBoot(boot_id) &&
               screen_acknowledged_.load(std::memory_order_acquire) &&
               touch_verified_.load(std::memory_order_acquire);
    }

    uint32_t bootId() const { return boot_id_.load(std::memory_order_acquire); }
    bool screenAcknowledged() const { return screen_acknowledged_.load(std::memory_order_acquire); }
    bool touchVerified() const { return touch_verified_.load(std::memory_order_acquire); }

private:
    std::atomic<uint32_t> boot_id_{0};
    std::atomic<bool> screen_acknowledged_{false};
    std::atomic<bool> touch_verified_{false};
};
