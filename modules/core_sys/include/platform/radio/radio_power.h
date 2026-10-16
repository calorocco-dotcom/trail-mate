#pragma once

#include <atomic>
#include <cstdint>

namespace platform::radio
{
// The user's RF permission and temporary exclusive ownership are separate.
// Only the radio task acknowledges a physical transition; UI never owns hardware.
class PowerPolicy
{
    static constexpr uint32_t kEnabled = 1, kApplied = 2, kExclusive = 4, kFailed = 8;
    std::atomic<uint32_t> state_{kEnabled | kApplied};

  public:
    struct Status
    {
        bool enabled, applied, exclusive, failed;
    };
    Status status() const
    {
        const auto state = state_.load();
        return {bool(state & kEnabled), bool(state & kApplied), bool(state & kExclusive), bool(state & kFailed)};
    }
    bool enabled() const { return state_.load() & kEnabled; }
    bool request(bool enabled)
    {
        auto before = state_.load();
        for (;;)
        {
            if (before & kExclusive) return false;
            const auto after = (before & ~(kEnabled | kFailed)) | (enabled ? kEnabled : 0U);
            if (state_.compare_exchange_weak(before, after)) return true;
        }
    }
    // Also permits USB storage suspension when the user already turned RF off.
    bool begin_exclusive()
    {
        return !(state_.fetch_or(kExclusive) & kExclusive);
    }
    void end_exclusive() { state_.fetch_and(~kExclusive); }
    void acknowledge(bool enabled)
    {
        auto before = state_.load();
        while (bool(before & kEnabled) == enabled)
        {
            const auto after = (before & ~(kApplied | kFailed)) | (enabled ? kApplied : 0U);
            if (state_.compare_exchange_weak(before, after)) return;
        }
    }
    void fail() { state_.fetch_or(kFailed); }
};
PowerPolicy& power_policy();
} // namespace platform::radio
