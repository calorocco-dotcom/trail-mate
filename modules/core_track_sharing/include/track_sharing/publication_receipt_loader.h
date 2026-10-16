#pragma once
#include "track_sharing/publication_directory.h"
#include "track_sharing/verify_receipt.h"
#include <algorithm>

namespace track_sharing
{
enum class ReceiptLoadResult : uint8_t
{
    Idle,
    Pending,
    Ready,
    Missing,
    Conflict,
    StorageFailure,
    Invalid,
    Cancelled
};
// Caller owns canonical stable path, admitted receipt/proof/domain buffers and
// authenticated job/directory/current-author/media leases. Confirmed journal
// alone is not receipt evidence. Publishing also permits recovery when receipt
// persistence succeeded but the final journal advance was interrupted.
template <class Io, class Crypto>
class PublicationReceiptLoader
{
  public:
    PublicationReceiptLoader(Io& io, Crypto& crypto, const PublicationJournal& job,
                             const PublicationDirectory& directory, VerifiedReceiptView& proof, uint8_t state)
        : io_(io), crypto_(crypto), job_(job), directory_(directory), proof_(proof), state_(state) {}
    bool setExpectedState(uint8_t state)
    {
        if (result_ != ReceiptLoadResult::Idle || state > 1) return false;
        state_ = state;
        return true;
    }
    ReceiptLoadResult begin(const char* path, uint8_t* output, size_t capacity)
    {
        if (result_ != ReceiptLoadResult::Idle || !path || !*path || !output || capacity < 1100 || !bindings()) return ReceiptLoadResult::Invalid;
        const geocaching::ByteView inputs[] = {
            {reinterpret_cast<const uint8_t*>(this), sizeof(*this)}, {reinterpret_cast<const uint8_t*>(&io_), sizeof(io_)}, {reinterpret_cast<const uint8_t*>(&crypto_), sizeof(crypto_)}, {reinterpret_cast<const uint8_t*>(&job_), sizeof(job_)}, {reinterpret_cast<const uint8_t*>(&directory_), sizeof(directory_)}, {reinterpret_cast<const uint8_t*>(&proof_), sizeof(proof_)}, {reinterpret_cast<const uint8_t*>(path), std::strlen(path) + 1}};
        for (const auto input : inputs)
            if (signingSpansOverlap(output, 1100, input.data, input.size)) return ReceiptLoadResult::Invalid;
        for (const auto input : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&job_), sizeof(job_)},
                                 {reinterpret_cast<const uint8_t*>(&directory_), sizeof(directory_)}})
            if (signingSpansOverlap(reinterpret_cast<const uint8_t*>(&proof_), sizeof(proof_), input.data, input.size)) return ReceiptLoadResult::Invalid;
        proof_ = {};
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return result_ = opened.status == Io::OpenStatus::Missing ? ReceiptLoadResult::Missing : ReceiptLoadResult::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size), closed = io_.close(opened.handle);
        if (!sized || !closed) return result_ = ReceiptLoadResult::StorageFailure;
        if (size < 69 || size > 1100) return result_ = ReceiptLoadResult::Invalid;
        path_ = path;
        output_ = output;
        size_ = uint16_t(size);
        return result_ = ReceiptLoadResult::Pending;
    }
    ReceiptLoadResult step(uint8_t* workspace, size_t capacity)
    {
        if (result_ != ReceiptLoadResult::Pending) return result_;
        if (!bindings()) return stop(ReceiptLoadResult::Conflict);
        if (offset_ < size_)
        {
            const auto opened = io_.open(path_, Io::Mode::Read);
            if (!opened.handle) return stop(ReceiptLoadResult::StorageFailure);
            uint64_t size = 0;
            const auto count = std::min<size_t>(320, size_ - offset_);
            const bool read = io_.size(opened.handle, size) && size == size_ && io_.seek(opened.handle, offset_) &&
                              io_.read(opened.handle, output_ + offset_, count) == count;
            const bool closed = io_.close(opened.handle);
            if (!read || !closed) return stop(ReceiptLoadResult::StorageFailure);
            offset_ += uint16_t(count);
            return result_;
        }
        if (!workspace || !capacity || signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(&io_), sizeof(io_)) ||
            signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(&crypto_), sizeof(crypto_)) ||
            signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(&directory_), sizeof(directory_)) ||
            signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(path_), std::strlen(path_) + 1)) return stop(ReceiptLoadResult::Invalid);
        const auto verified = verifyReceipt({output_, size_}, job_, {job_.operation, 16}, {directory_.public_key, 64}, state_, crypto_, workspace, capacity, proof_);
        if (verified != ReceiptVerification::Valid) return stop(verified == ReceiptVerification::WorkspaceTooSmall ? ReceiptLoadResult::Invalid : ReceiptLoadResult::Conflict);
        return result_ = ReceiptLoadResult::Ready;
    }
    ReceiptLoadResult cancel() { return result_ == ReceiptLoadResult::Pending ? stop(ReceiptLoadResult::Cancelled) : result_; }
    bool ready() const { return result_ == ReceiptLoadResult::Ready && proof_.verified && bindings(); }

  private:
    bool bindings() const
    {
        return state_ <= 1 && publicationDirectoryMatches(directory_, job_) && job_.offset == job_.file_bytes &&
               (job_.phase == PublicationPhase::Publishing || job_.phase == PublicationPhase::Confirmed);
    }
    ReceiptLoadResult stop(ReceiptLoadResult result)
    {
        proof_ = {};
        if (output_) std::memset(output_, 0, size_);
        return result_ = result;
    }
    Io& io_;
    Crypto& crypto_;
    const PublicationJournal& job_;
    const PublicationDirectory& directory_;
    VerifiedReceiptView& proof_;
    const char* path_ = nullptr;
    uint8_t* output_ = nullptr;
    uint16_t size_ = 0, offset_ = 0;
    uint8_t state_;
    ReceiptLoadResult result_ = ReceiptLoadResult::Idle;
};
} // namespace track_sharing
