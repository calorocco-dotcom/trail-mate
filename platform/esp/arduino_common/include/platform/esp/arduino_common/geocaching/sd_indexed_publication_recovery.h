#pragma once
#include "geocaching/storage/publication_recovery.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include "platform/memory/psram_ptr.h"
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching
{
// One root/frame lease. The scan remembers only the best key and its metadata,
// then rereads that row for the caller to verify and consume before release.
class SdIndexedPublicationRecovery
{
  public:
    explicit SdIndexedPublicationRecovery(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, const ::geocaching::storage::PublicationRecoveryFilter& filter,
               uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexGetStep::Idle || (filter.include_terminal && !filter.has_operation)) return false;
        root_ = root;
        filter_ = filter;
        frame_ = frame;
        capacity_ = capacity;
        result_ = IndexGetStep::Working;
        if (filter.has_operation)
        {
            task_key_ = filter.operation;
            return startGet(10, {task_key_.data(), task_key_.size()}) == IndexGetStep::Working;
        }
        scan_.reset(::platform::memory::createPsram<SdIndexScan>(volume_));
        if (!scan_)
        {
            unavailable_ = true;
            return false;
        }
        if (!scan_->begin(root, 5, frame, capacity)) return false;
        return true;
    }
    bool matches(const ::geocaching::storage::PublicationRecoveryFilter& filter) const { return filter_.same(filter); }
    bool unavailable() const { return unavailable_; }
    bool value(::geocaching::storage::PublicationRecoveryView& out) const
    {
        if (result_ != IndexGetStep::Ready) return false;
        out = selected_;
        return true;
    }
    IndexGetStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexGetStep::Working) return result_;
        if (get_)
        {
            const auto status = get_->step();
            if (status == IndexGetStep::Working) return status;
            if (filter_.has_operation)
            {
                if (status != IndexGetStep::Ready)
                    return result_ = status == IndexGetStep::NotFound && reading_selected_ ? IndexGetStep::Invalid : status;
                if (!reading_selected_)
                {
                    TaskView task;
                    if (!decodeTask({task_key_.data(), task_key_.size()}, get_->value(), task) || task.kind != 1 ||
                        task.cache_id.size != 32 || task.revision_hash.size != 32) return result_ = IndexGetStep::Invalid;
                    if ((filter_.has_cache && std::memcmp(task.cache_id.data, filter_.cache.bytes.data(), 32)) ||
                        (filter_.has_hash && std::memcmp(task.revision_hash.data, filter_.hash.bytes.data(), 32))) return result_ = IndexGetStep::NotFound;
                    bool matched = false;
                    for (size_t i = 0; i < task.request_count; ++i)
                    {
                        if (!filter_.matchesRequest(task.requests[i])) continue;
                        // One web operation names one destination. An ambiguous
                        // group cannot be projected as one confirmed directory.
                        if (matched) return result_ = IndexGetStep::Invalid;
                        std::memcpy(selected_.key.data(), task.requests[i].data, selected_.key.size());
                        matched = true;
                    }
                    if (!matched) return result_ = IndexGetStep::NotFound;
                    selected_.task = task_key_;
                    std::memcpy(selected_.cache.bytes.data(), task.cache_id.data, 32);
                    std::memcpy(selected_.hash.bytes.data(), task.revision_hash.data, 32);
                    selected_.task_state = task.state;
                    selected_.continue_intent = task.continue_intent;
                    reading_selected_ = true;
                    return startGet(5, {selected_.key.data(), selected_.key.size()});
                }
                OutgoingView outgoing;
                if (!decodeOutgoing({selected_.key.data(), selected_.key.size()}, get_->value(), outgoing) ||
                    std::memcmp(outgoing.task_id.data, selected_.task.data(), 16)) return result_ = IndexGetStep::Invalid;
                if (!publicationRecoveryRequestEligible(filter_, outgoing) ||
                    (!filter_.include_terminal && outgoing.state < 4 &&
                     (!selected_.continue_intent || selected_.task_state == 3 || selected_.task_state == 5)))
                    return result_ = IndexGetStep::NotFound;
                selected_.outgoing_state = outgoing.state;
                selected_.confirmed = outgoing.state == 4;
                selected_.continue_intent &= outgoing.continue_intent;
                selected_.created = outgoing.created;
                selected_.request = outgoing.request;
                selected_.response = selected_.confirmed ? outgoing.terminal_data : ByteView{};
                return result_ = IndexGetStep::Ready;
            }
            if (status != IndexGetStep::Ready) return result_ = status == IndexGetStep::NotFound ? IndexGetStep::Invalid : status;
            if (reading_selected_)
            {
                OutgoingView outgoing;
                if (!decodeOutgoing({selected_.key.data(), selected_.key.size()}, get_->value(), outgoing) ||
                    std::memcmp(outgoing.task_id.data, selected_.task.data(), 16) || outgoing.state != selected_.outgoing_state ||
                    (outgoing.state == 4) != selected_.confirmed)
                    return result_ = IndexGetStep::Invalid;
                selected_.request = outgoing.request;
                selected_.response = selected_.confirmed ? outgoing.terminal_data : ByteView{};
                return result_ = IndexGetStep::Ready;
            }
            TaskView task;
            if (!decodeTask({task_key_.data(), task_key_.size()}, get_->value(), task)) return result_ = IndexGetStep::Invalid;
            PublicationRecoveryView candidate;
            const auto accepted = publicationRecoveryCandidate(filter_, {candidate_key_.data(), candidate_key_.size()}, outgoing_, task, candidate);
            if (accepted == PublicationRecoveryResult::Invalid) return result_ = IndexGetStep::Invalid;
            if (accepted == PublicationRecoveryResult::Ready && (!found_ || preferPublicationRecovery(candidate, selected_)))
            {
                selected_ = candidate;
                found_ = true;
            }
            get_.reset();
            return result_;
        }
        const auto status = scan_->step();
        if (status == IndexScanStep::Working) return result_;
        if (status == IndexScanStep::End)
        {
            scan_.reset();
            if (!found_) return result_ = IndexGetStep::NotFound;
            reading_selected_ = true;
            return startGet(5, {selected_.key.data(), selected_.key.size()});
        }
        if (status != IndexScanStep::Item)
            return result_ = status == IndexScanStep::IoError ? IndexGetStep::IoError : status == IndexScanStep::VolumeChanged   ? IndexGetStep::VolumeChanged
                                                                                    : status == IndexScanStep::WorkspaceTooSmall ? IndexGetStep::WorkspaceTooSmall
                                                                                                                                 : IndexGetStep::Invalid;
        MutationView row;
        if (!scan_->item(row)) return result_ = IndexGetStep::Invalid;
        if (filter_.matchesRequest(row.key))
        {
            if (!decodeOutgoing(row.key, row.value, outgoing_)) return result_ = IndexGetStep::Invalid;
            if (!publicationRecoveryRequestEligible(filter_, outgoing_))
            {
                if (!scan_->advance()) return result_ = IndexGetStep::Invalid;
                return result_;
            }
            std::memcpy(candidate_key_.data(), row.key.data, candidate_key_.size());
            std::memcpy(task_key_.data(), outgoing_.task_id.data, task_key_.size());
            // The task read reuses the row frame. Only scalar metadata and an
            // owned task key are needed to check eligibility and references.
            outgoing_.request = outgoing_.terminal_data = {};
            outgoing_.task_id = {task_key_.data(), task_key_.size()};
            if (startGet(10, outgoing_.task_id) != IndexGetStep::Working) return result_;
        }
        if (!scan_->advance()) return result_ = IndexGetStep::Invalid;
        return result_;
    }

  private:
    IndexGetStep startGet(uint8_t table, ::geocaching::ByteView key)
    {
        get_.reset(::platform::memory::createPsram<SdIndexGet>(volume_));
        if (!get_)
        {
            unavailable_ = true;
            return result_ = IndexGetStep::Invalid;
        }
        if (!get_->begin(root_, table, key, frame_, capacity_)) return result_ = IndexGetStep::Invalid;
        return result_;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::PublicationRecoveryFilter filter_;
    ::geocaching::storage::PublicationRecoveryView selected_;
    ::geocaching::storage::OutgoingView outgoing_;
    std::array<uint8_t, 48> candidate_key_{};
    std::array<uint8_t, 16> task_key_{};
    ::platform::memory::PsramPtr<SdIndexScan> scan_;
    ::platform::memory::PsramPtr<SdIndexGet> get_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    IndexGetStep result_ = IndexGetStep::Idle;
    bool found_ = false, reading_selected_ = false, unavailable_ = false;
};
static_assert(sizeof(SdIndexedPublicationRecovery) <= 640, "Publication recovery retains keys and metadata only");
} // namespace platform::esp::arduino_common::geocaching
