#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/protocol/verify_record.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_saved_cache.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/memory/psram_ptr.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include <esp_heap_caps.h>
#include <memory>

namespace platform::esp::arduino_common::geocaching
{
// One open detail, allocated off the ESP stack. Verified text borrows the
// retained immutable response and is consumed synchronously under the source lock.
struct CacheDetail
{
    // Latest GET uses less than 112 bytes; its unsent tail binds directory epoch.
    static constexpr size_t kLatestEpochOffset = 112;
    enum class State
    {
        Network,
        Saved,
        Ready,
        Failed,
        LatestNetwork,
        LatestReady,
        LatestFailed,
        LatestNotFound
    };
    ::geocaching::GeocacheId id;
    ::geocaching::RevisionHash hash;
    ::geocaching::Destination remote;
    ::geocaching::RequestId request;
    std::array<uint8_t, 128> request_bytes{};
    size_t request_size = 0;
    std::array<uint8_t, 32> receipt{};
    bool has_receipt = false;
    uint8_t* response = nullptr;
    size_t response_size = 0;
    ::geocaching::protocol::VerifiedRecordView verified_record;
    ::platform::memory::PsramPtr<SdIndexedSavedCache> saved_read;
    uint64_t started = 0, next_send = 0;
    State state = State::Network;
    const char* error = "Details unavailable. Go back and reopen to retry.";
    ~CacheDetail()
    {
        if (response) heap_caps_free(response);
    }
    bool matches(const std::array<uint8_t, 32>& cache, const std::array<uint8_t, 32>& revision) const
    {
        return id.bytes == cache && hash.bytes == revision;
    }
    bool matches(const ::geocaching::Destination& source, const ::geocaching::RequestId& incoming) const
    {
        return request_size && source.bytes == remote.bytes && incoming.bytes == request.bytes;
    }
    bool verify(::geocaching::ByteView bytes, ::geocaching::protocol::RecordCrypto& crypto)
    {
        if (state == State::Saved && !response)
        {
            response = static_cast<uint8_t*>(::platform::esp::common::memory::allocatePreferred("geocaching.detail.saved", bytes.size, false));
            if (!response) return false;
            std::memcpy(response, bytes.data, bytes.size);
            response_size = bytes.size;
            bytes = {response, response_size};
        }
        constexpr size_t capacity = ::geocaching::kMaxRecordBytes + 64;
        auto* scratch = static_cast<uint8_t*>(::platform::esp::common::memory::allocatePreferred("geocaching.detail.verify", capacity, false));
        if (!scratch) return false;
        ::geocaching::protocol::VerifiedRecordView verified;
        const bool latest = state == State::LatestNetwork;
        const bool valid = ::geocaching::protocol::verifyGeocache(bytes, crypto, scratch, capacity, verified, &id, latest ? nullptr : &hash) ==
                           ::geocaching::protocol::VerificationResult::Valid;
        if (valid)
        {
            verified_record = verified;
            state = latest ? State::LatestReady : State::Ready;
        }
        heap_caps_free(scratch);
        return valid;
    }
    bool receive(const ::geocaching::Destination& source, const ::geocaching::RequestId& incoming,
                 ::geocaching::ByteView bytes, ::geocaching::protocol::RecordCrypto& crypto, uint8_t** owned)
    {
        if (!matches(source, incoming)) return false;
        if ((state == State::Ready || state == State::LatestReady || state == State::LatestNotFound) && has_receipt)
        {
            std::array<uint8_t, 32> digest{};
            return crypto.sha256(bytes, digest.data()) && digest == receipt;
        }
        if ((state != State::Network && state != State::LatestNetwork) || response) return false;
        response = owned ? *owned : static_cast<uint8_t*>(::platform::esp::common::memory::allocatePreferred("geocaching.detail.rx", bytes.size, false));
        if (!response) return false;
        if (owned) *owned = nullptr;
        else std::memcpy(response, bytes.data, bytes.size);
        response_size = bytes.size;
        return false;
    }
    void advanceNetwork(chat::MeshAdapterRouter& router, const ::geocaching::Destination& local,
                        ::geocaching::protocol::RecordCrypto& crypto, uint64_t now)
    {
        if (state != State::Network && state != State::LatestNetwork) return;
        const bool latest = state == State::LatestNetwork;
        if (response)
        {
            if (latest && ::geocaching::protocol::decodeGetNotFound({response, response_size}, request, 8192) &&
                crypto.sha256({response, response_size}, receipt.data()))
            {
                has_receipt = true;
                state = State::LatestNotFound;
                return;
            }
            ::geocaching::protocol::GetResponseView parsed;
            if (::geocaching::protocol::decodeGetResponse({response, response_size}, request, 8192, parsed) && !parsed.has_conflict &&
                (!latest || parsed.is_current) &&
                verify(parsed.signed_cache, crypto))
                has_receipt = crypto.sha256({response, response_size}, receipt.data());
            // Keep only the open, verified response so Save can transfer it to
            // the durable installation without another network request.
            if (state == State::Ready || state == State::LatestReady) return;
            heap_caps_free(response);
            response = nullptr;
            response_size = 0;
        }
        if (now - started >= 120000)
        {
            state = latest ? State::LatestFailed : State::Failed;
            error = "Directory did not return verified details. Go back and reopen to retry.";
            return;
        }
        if (now < next_send) return;
        std::array<uint8_t, 32> accepted{};
        const auto sent = router.sendGeocachingData(remote.bytes.data(), {request_bytes.data(), request_size}, false, &accepted, local.bytes.data());
        next_send = now + (sent.ok ? 15000 : 1000);
    }
};
static_assert(sizeof(CacheDetail) < 1536, "Detail metadata must borrow text from its retained response");
} // namespace platform::esp::arduino_common::geocaching
