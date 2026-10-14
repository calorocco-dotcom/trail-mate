#pragma once
#include "track_sharing/upload_seal_task.h"
namespace track_sharing
{
// Serialized platform management session. Stopping cancels hashing but retains
// durable files; recovery requires a new owner and authenticated session.
template <class Resources>
class UploadSession
{
  public:
    UploadSession(Resources& owner, const uint8_t* session, uint32_t media, uint64_t now)
        : owner_(owner), media_(media), touched_(now)
    {
        uint8_t bits = 0;
        if (session)
            for (unsigned i = 0; i < 16; ++i) bits |= session[i];
        active_ = bits != 0;
        if (active_) std::memcpy(session_, session, 16);
    }
    bool matches(const uint8_t* session, uint32_t media, uint64_t now)
    {
        if (!alive(media, now, true)) return false;
        if (!session || std::memcmp(session_, session, 16)) return false;
        return true;
    }
    bool authorize(const uint8_t* session, uint32_t media, uint64_t now)
    {
        if (!matches(session,media,now)) return false;
        touched_ = now;
        return true;
    }
    JournalStoreResult append(const uint8_t* session, uint32_t media, uint64_t now, const uint8_t* id,
                              uint32_t offset, const uint8_t* bytes, size_t length, uint8_t* scratch, size_t capacity)
    {
        if (!matches(session, media, now)) return JournalStoreResult::Conflict;
        const auto result = owner_.append(id, offset, bytes, length, scratch, capacity);
        if (result == JournalStoreResult::Ready) touched_ = now;
        return result;
    }
    UploadSealResult beginSeal(const uint8_t* session, uint32_t media, uint64_t now, uint8_t* scratch, size_t capacity)
    {
        if (!matches(session, media, now)) return UploadSealResult::Conflict;
        const auto result = owner_.beginSeal(scratch, capacity);
        if (result == UploadSealResult::Pending) touched_ = now;
        return result;
    }
    UploadSealResult advance(uint32_t media, uint64_t now, bool connected, uint8_t* scratch, size_t capacity)
    {
        if (!alive(media, now, connected)) return UploadSealResult::Cancelled;
        return owner_.step(scratch, capacity);
    }
    bool close(const uint8_t* session)
    {
        if (!active_ || !session || std::memcmp(session_, session, 16)) return false;
        stop();
        return true;
    }
    bool releasable() const { return !active_; }
    static constexpr uint64_t kIdleTimeoutMs = 30000;

  private:
    void stop()
    {
        owner_.cancel();
        active_ = false;
    }
    bool alive(uint32_t media, uint64_t now, bool connected)
    {
        if (!active_) return false;
        if (!connected || media != media_ || now < touched_ || now - touched_ >= kIdleTimeoutMs)
        {
            stop();
            return false;
        }
        return true;
    }
    Resources& owner_;
    uint32_t media_;
    uint64_t touched_;
    uint8_t session_[16]{};
    bool active_ = false;
};
} // namespace track_sharing
