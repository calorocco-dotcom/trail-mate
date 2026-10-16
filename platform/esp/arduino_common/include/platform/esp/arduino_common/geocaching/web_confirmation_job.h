#pragma once
#include "geocaching/storage/confirmed_publication.h"
#include "platform/esp/arduino_common/geocaching/publication_store.h"
#include "platform/memory/psram_ptr.h"

namespace platform::esp::arduino_common::geocaching
{
enum class WebConfirmationPhase : uint8_t
{
    Idle,
    Prepare,
    Admit,
    Commit,
    Confirmed,
    Failed,
    Cancelled
};
// The serialized owner pins immutable inputs and lends max(320, source-size + 128) bytes.
// This job owns metadata only; it never opens a file or a network connection.
template <class Store>
class WebConfirmationJob
{
  public:
    WebConfirmationJob(Store& store, ::geocaching::protocol::RecordCrypto& crypto) : store_(store), crypto_(crypto) {}
    bool begin(const std::array<uint8_t, 16>& operation, ::geocaching::ByteView signed_cache, ::geocaching::ByteView proof,
               ::geocaching::ByteView author, const ::geocaching::storage::StoredTime& time, uint8_t* work, size_t capacity)
    {
        if (phase_ != WebConfirmationPhase::Idle || !signed_cache.data || !signed_cache.size || signed_cache.size > 4166 || !proof.data || !proof.size || proof.size > 384 ||
            !author.data || author.size != 64 || !work || capacity < 320 || capacity < signed_cache.size + 128) return false;
        uint8_t present = 0;
        for (auto value : operation) present |= value;
        if (!present) return false;
        const ::geocaching::ByteView inputs[] = {signed_cache, proof, author};
        for (auto input : inputs)
        {
            const auto a = reinterpret_cast<uintptr_t>(work), b = reinterpret_cast<uintptr_t>(input.data);
            if (a <= b ? b - a < capacity : a - b < input.size) return false;
        }
        operation_ = operation;
        signed_ = signed_cache;
        proof_ = proof;
        author_ = author;
        time_ = time;
        work_ = work;
        capacity_ = capacity;
        phase_ = WebConfirmationPhase::Prepare;
        return true;
    }
    WebConfirmationPhase phase() const { return phase_; }
    const ::geocaching::GeocacheId& cacheId() const { return cache_; }
    const ::geocaching::RevisionHash& hash() const { return hash_; }
    uint32_t revision() const { return revision_; }
    void cancel()
    {
        if (phase_ == WebConfirmationPhase::Confirmed || phase_ == WebConfirmationPhase::Failed || phase_ == WebConfirmationPhase::Cancelled) return;
        if (phase_ == WebConfirmationPhase::Commit) store_.cancelCommit();
        phase_ = WebConfirmationPhase::Cancelled;
    }
    void advance()
    {
        using namespace ::geocaching;
        if (phase_ == WebConfirmationPhase::Prepare)
        {
            auto content = ::platform::memory::PsramPtr<protocol::VerifiedRecordView>(::platform::memory::createPsram<protocol::VerifiedRecordView>());
            if (!content || store_.needsRecovery() || protocol::verifyGeocache(signed_, crypto_, work_, capacity_, *content) != protocol::VerificationResult::Valid ||
                std::memcmp(content->record.author_public_key.data, author_.data, 64))
            {
                phase_ = WebConfirmationPhase::Failed;
                return;
            }
            protocol::PublicationReceiptView receipt;
            ::geocaching::storage::PublicationConfirmationView proof;
            protocol::CmpReader raw(proof_); // Decode first to obtain its signed directory destination.
            if (!::geocaching::storage::decodePublicationConfirmation(proof_, proof))
            {
                phase_ = WebConfirmationPhase::Failed;
                return;
            }
            raw = protocol::CmpReader(proof.raw);
            size_t count = 0;
            uint64_t version = 0;
            ByteView directory;
            if (!raw.array(count, 8) || count != 8 || !raw.unsignedInteger(version) || version != 1 || !raw.binary(directory, 16) || directory.size != 16 ||
                ::geocaching::storage::verifyPublicationConfirmation(proof_, directory, *content, crypto_, work_, capacity_, receipt) != protocol::VerificationResult::Valid)
            {
                phase_ = WebConfirmationPhase::Failed;
                return;
            }
            cache_ = content->id;
            hash_ = content->hash;
            revision_ = content->record.revision;
            std::memcpy(remote_.bytes.data(), directory.data, 16);
            if (!crypto_.sha256(author_, work_ + 64))
            {
                phase_ = WebConfirmationPhase::Failed;
                return;
            }
            std::memcpy(work_ + 10, work_ + 64, 16);
            constexpr char name[] = "lxmf.delivery";
            if (!crypto_.sha256({reinterpret_cast<const uint8_t*>(name), sizeof(name) - 1}, work_ + 32))
            {
                phase_ = WebConfirmationPhase::Failed;
                return;
            }
            std::memcpy(work_, work_ + 32, 10);
            if (!crypto_.sha256({work_, 26}, work_ + 64))
            {
                phase_ = WebConfirmationPhase::Failed;
                return;
            }
            std::memcpy(local_.bytes.data(), work_ + 64, 16);
            request_.bytes = operation_;
            if (!protocol::encodePublishRequest(request_, content->record.encoded, content->signature, 8192, work_, capacity_, request_size_))
            {
                phase_ = WebConfirmationPhase::Failed;
                return;
            }
            phase_ = WebConfirmationPhase::Admit;
        }
        else if (phase_ == WebConfirmationPhase::Admit || phase_ == WebConfirmationPhase::Commit)
        {
            const auto result = phase_ == WebConfirmationPhase::Commit ? store_.stepCommit() : store_.commitWebPublication(local_, remote_, request_, operation_, {work_, request_size_}, proof_, time_, crypto_);
            if (result == JournalWriteResult::Busy) return;
            if (result == JournalWriteResult::InProgress)
            {
                phase_ = WebConfirmationPhase::Commit;
                return;
            }
            phase_ = result == JournalWriteResult::Verified ? WebConfirmationPhase::Confirmed : WebConfirmationPhase::Failed;
        }
    }

  private:
    Store& store_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::ByteView signed_, proof_, author_;
    ::geocaching::storage::StoredTime time_;
    ::geocaching::Destination local_, remote_;
    ::geocaching::RequestId request_;
    ::geocaching::GeocacheId cache_;
    ::geocaching::RevisionHash hash_;
    std::array<uint8_t, 16> operation_{};
    uint8_t* work_ = nullptr;
    size_t capacity_ = 0, request_size_ = 0;
    uint32_t revision_ = 0;
    WebConfirmationPhase phase_ = WebConfirmationPhase::Idle;
};
} // namespace platform::esp::arduino_common::geocaching
