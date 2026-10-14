#pragma once
#include "geocaching/protocol/get_request.h"
#include "geocaching/storage/object_ref.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/storage/confirmed_publication.h"
#include "geocaching/storage/author_issued.h"
#include "geocaching/storage/draft_record.h"
#include "platform/memory/psram_ptr.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"

namespace platform::esp::arduino_common::geocaching
{
// The caller leases encoding workspace, frame and roots until inputConsumed /
// completion. The same task/request encoders serve both storage implementations.
class SdIndexedNewTask
{
  public:
    explicit SdIndexedNewTask(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool beginConfirmed(const ::geocaching::storage::IndexRootView& root, unsigned copy,
                        const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                        const ::geocaching::RequestId& request_id, const std::array<uint8_t,16>& task_id,
                        ::geocaching::ByteView request, ::geocaching::ByteView confirmation,
                        const ::geocaching::storage::StoredTime& time, ::geocaching::storage::QueuedRequestWorkspace& workspace,
                        uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate,
                        ::geocaching::protocol::RecordCrypto& crypto, uint8_t* verification, size_t verification_capacity)
    {
        using namespace ::geocaching;
        if(result_!=IndexedCommitStep::Idle)return false;
        verified_.reset(::platform::memory::createPsram<protocol::VerifiedRecordView>());
        std::memcpy(key_.data(),local.bytes.data(),16);std::memcpy(key_.data()+16,remote.bytes.data(),16);std::memcpy(key_.data()+32,request_id.bytes.data(),16);
        ::geocaching::storage::PublicationConfirmationView proof;
        if(!verified_||!::geocaching::storage::decodePublicationConfirmation(confirmation,proof)||
           ::geocaching::storage::verifyConfirmedPublication({key_.data(),key_.size()},request,confirmation,crypto,verification,verification_capacity,*verified_)!=protocol::VerificationResult::Valid)return false;
        ::geocaching::storage::RequestTaskTarget target;
        target.cache_id={verified_->id.bytes.data(),32};target.revision_hash={verified_->hash.bytes.data(),32};
        return begin(root,copy,local,remote,request_id,task_id,1,request,time,target,workspace,frame,capacity,candidate,confirmation);
    }
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy,
               const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
               const ::geocaching::RequestId& request_id, const std::array<uint8_t, 16>& task_id, uint8_t kind,
               ::geocaching::ByteView request, const ::geocaching::storage::StoredTime& time,
               const ::geocaching::storage::RequestTaskTarget& target,
               ::geocaching::storage::QueuedRequestWorkspace& workspace,
               uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate,
               ::geocaching::ByteView confirmation = {})
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !validIndexRoot(root)) return false;
        const auto overlaps = [](ByteView a, ByteView b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
            return a.size && b.size && (x <= y ? y - x < a.size : x - y < b.size);
        };
        const ByteView outgoing{workspace.outgoing, workspace.outgoing_capacity}, task{workspace.task.data(), workspace.task.size()};
        const ByteView scratch{frame, capacity}, output{candidate.data(), candidate.size()};
        if (overlaps(request, outgoing) || overlaps(request, task) || overlaps(outgoing, task) ||
            overlaps(outgoing, scratch) || overlaps(task, scratch) || overlaps(outgoing, output) || overlaps(task, output) ||
            overlaps(scratch, output) || overlaps(root.shards, output) || overlaps(root.shards, outgoing) || overlaps(root.shards, task) ||
            overlaps(target.cache_id, outgoing) || overlaps(target.cache_id, task) ||
            overlaps(target.revision_hash, outgoing) || overlaps(target.revision_hash, task)) return false;
        task_id_ = task_id;
        // prepareNewRequestTask requires a two-element array reference.
        MutationView prepared[2];
        if (!prepareNewRequestTask(local, remote, request_id, task_id_, kind, request, time, workspace, key_, prepared, target)) return false;
        mutations_[0] = prepared[0];
        mutations_[1] = prepared[1];
        root_ = root;
        copy_ = copy;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        count_ = 2;
        result_ = IndexedCommitStep::Working;
        if(confirmation.size)
        {
            if(!verified_||kind!=1||overlaps(confirmation,outgoing)||overlaps(confirmation,task))return false;
            OutgoingView completed;completed.request=request;completed.state=4;completed.task_id={task_id_.data(),16};completed.created=time;completed.terminal_data=confirmation;
            TaskView done;done.kind=1;done.state=3;done.cache_id=target.cache_id;done.revision_hash=target.revision_hash;done.requests[0]={key_.data(),key_.size()};done.request_count=1;
            size_t outgoing_size=0,task_size=0;
            if(!encodeOutgoing({key_.data(),key_.size()},completed,workspace.outgoing,workspace.outgoing_capacity,outgoing_size)||
               !encodeTask({task_id_.data(),16},done,workspace.task.data(),workspace.task.size(),task_size))return false;
            mutations_[0].value={workspace.outgoing,outgoing_size};mutations_[1].value={workspace.task.data(),task_size};
            // Stage optional history metadata in the existing output lease.
            // No recovery buffer is allocated; absence needs enough tail room.
            recovery_work_={workspace.outgoing+outgoing_size,workspace.outgoing_capacity-outgoing_size};
            size_t issued_size=0;
            auto history_time=time;
            history_time.has_utc=true;history_time.utc_seconds=verified_->record.updated_at;history_time.utc_trusted=false;
            if(encodeAuthorIssued(verified_->hash,verified_->record.author_public_key,history_time,
                                 workspace.outgoing+outgoing_size,recovery_work_.size,issued_size,verified_->record.created_at))
            {
                mutations_[2]={3,{head_.data(),36},{recovery_work_.data,issued_size},false};
                recovery_work_={recovery_work_.data+issued_size,recovery_work_.size-issued_size};
            }
            confirmed_request_=request;confirmation_=confirmation;confirming_=true;confirmation_phase_=0;
            return operation_.emplace<SdIndexGet>(volume_).begin(root_,5,{key_.data(),key_.size()},frame_,capacity_);
        }
        if (kind != 2) return startCommit() == IndexedCommitStep::Working;
        protocol::GetRequestView get;
        if (!protocol::decodeGetRequest(request, request_id, get) || get.wanted_hash.size != 32 ||
            std::memcmp(get.cache_id.data, target.cache_id.data, 32) || std::memcmp(get.wanted_hash.data, target.revision_hash.data, 32))
        {
            fail(IndexedCommitStep::Invalid);
            return false;
        }
        std::memcpy(cache_id_.data(), target.cache_id.data, cache_id_.size());
        generation_ = target.install_generation;
        if (!operation_.emplace<SdIndexGet>(volume_).begin(root_, 2, {cache_id_.data(), cache_id_.size()}, frame_, capacity_))
        {
            fail(IndexedCommitStep::Invalid);
            return false;
        }
        return true;
    }
    bool inputConsumed() const
    {
        return result_ != IndexedCommitStep::Working || (committing_ && std::get<SdIndexedCommit>(operation_).inputConsumed());
    }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        if(result_==IndexedCommitStep::Verified&&duplicate_){out=root_;return true;}
        return result_ == IndexedCommitStep::Verified && std::get<SdIndexedCommit>(operation_).committed(out);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (committing_) return result_ = std::get<SdIndexedCommit>(operation_).step();
        if (confirming_ && confirmation_phase_ == 4) return confirmHistory();
        auto& read = std::get<SdIndexGet>(operation_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready && status != IndexGetStep::NotFound)
            return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                   : IndexedCommitStep::Invalid);
        if(confirming_)
        {
            if(confirmation_phase_==0)
            {
                if(status==IndexGetStep::Ready)
                {
                    OutgoingView saved;
                    if(!decodeOutgoing({key_.data(),key_.size()},read.value(),saved)||saved.state!=4||saved.continue_intent||
                       std::memcmp(saved.task_id.data,task_id_.data(),16)||saved.request.size!=confirmed_request_.size||
                       std::memcmp(saved.request.data,confirmed_request_.data,confirmed_request_.size)||saved.terminal_data.size!=confirmation_.size||
                       std::memcmp(saved.terminal_data.data,confirmation_.data,confirmation_.size))return fail(IndexedCommitStep::Invalid);
                    duplicate_=true;confirmation_phase_=2;
                    if(!operation_.emplace<SdIndexGet>(volume_).begin(root_,10,{task_id_.data(),16},frame_,capacity_))return fail(IndexedCommitStep::Invalid);
                    return result_;
                }
            }
            else if(confirmation_phase_==2)
            {
                TaskView saved;
                if(status!=IndexGetStep::Ready||!decodeTask({task_id_.data(),16},read.value(),saved)||saved.kind!=1||saved.state!=3||saved.continue_intent||
                   saved.request_count!=1||std::memcmp(saved.requests[0].data,key_.data(),48)||saved.cache_id.size!=32||saved.revision_hash.size!=32||
                   std::memcmp(saved.cache_id.data,verified_->id.bytes.data(),32)||std::memcmp(saved.revision_hash.data,verified_->hash.bytes.data(),32))return fail(IndexedCommitStep::Invalid);
            }
            else if(confirmation_phase_==1)
            {
                AuthorIssuedView issued;
                if(status==IndexGetStep::Ready)
                {
                    if(!decodeAuthorIssued({head_.data(),36},read.value(),issued)||
                       std::memcmp(issued.revision_hash.data,verified_->hash.bytes.data(),32)||std::memcmp(issued.author_public_key.data,verified_->record.author_public_key.data,64))return fail(IndexedCommitStep::Invalid);
                    if(duplicate_)return result_=IndexedCommitStep::Verified;
                }
                else
                {
                    if(duplicate_||status!=IndexGetStep::NotFound||!mutations_[2].value.size)return fail(IndexedCommitStep::Invalid);
                    count_=3;
                }
                // Validate existing origin evidence before adding recovery
                // metadata. Reuse the same frame and pinned immutable root.
                confirmation_phase_=4;
                if(!operation_.emplace<SdIndexScan>(volume_).begin(root_,3,frame_,capacity_))return fail(IndexedCommitStep::Invalid);
                return result_;
            }
            else if(confirmation_phase_==3)
            {
                if(status==IndexGetStep::Ready)
                {
                    DraftView existing;
                    if(!decodeDraft({cache_id_.data(),16},read.value(),existing)||existing.author.size!=64||
                       std::memcmp(existing.author.data,verified_->record.author_public_key.data,64))return fail(IndexedCommitStep::Invalid);
                    // Retain later edits, generations and predecessors as-is.
                }
                else if(status==IndexGetStep::NotFound)
                {
                    const auto& record=verified_->record;
                    DraftView restored;
                    restored.author=record.author_public_key;restored.base_hash={verified_->hash.bytes.data(),32};
                    restored.has_coordinates=true;restored.latitude_e7=record.latitude_e7;restored.longitude_e7=record.longitude_e7;
                    restored.name=record.name;restored.description=record.description;restored.hint=record.hint;
                    restored.state=static_cast<uint8_t>(record.state);restored.difficulty_x2=record.difficulty_x2;
                    restored.terrain_x2=record.terrain_x2;restored.container_size=static_cast<uint8_t>(record.container_size);
                    size_t draft_size=0;
                    if(!encodeDraft({cache_id_.data(),16},restored,const_cast<uint8_t*>(recovery_work_.data),recovery_work_.size,draft_size))return fail(IndexedCommitStep::Invalid);
                    mutations_[count_++]={4,{cache_id_.data(),16},{recovery_work_.data,draft_size},false};
                }
                else return fail(IndexedCommitStep::Invalid);
                return startCommit();
            }
            std::memcpy(head_.data(),verified_->id.bytes.data(),32);
            for(unsigned i=0;i<4;++i)head_[32+i]=static_cast<uint8_t>(verified_->record.revision>>((3-i)*8));
            confirmation_phase_=1;
            if(!operation_.emplace<SdIndexGet>(volume_).begin(root_,3,{head_.data(),36},frame_,capacity_))return fail(IndexedCommitStep::Invalid);
            return result_;
        }
        CacheHeadView head;
        const ::geocaching::ByteView key{cache_id_.data(), cache_id_.size()};
        if (status == IndexGetStep::Ready && !decodeCacheHead(key, read.value(), head)) return fail(IndexedCommitStep::Invalid);
        if (head.install_generation == UINT64_MAX || generation_ != head.install_generation + 1) return fail(IndexedCommitStep::Invalid);
        head.install_generation = generation_;
        size_t size = 0;
        if (!encodeCacheHead(key, head, head_.data(), head_.size(), size)) return fail(IndexedCommitStep::Invalid);
        mutations_[2] = {2, key, {head_.data(), size}, false};
        count_ = 3;
        return startCommit();
    }

  private:
    IndexedCommitStep confirmHistory()
    {
        using namespace ::geocaching::storage;
        auto& scan = std::get<SdIndexScan>(operation_);
        const auto status = scan.step();
        if (status == IndexScanStep::Working) return result_;
        if (status == IndexScanStep::End)
        {
            std::memcpy(cache_id_.data(),verified_->record.creation_nonce.data,16);
            confirmation_phase_=3;
            if(!operation_.emplace<SdIndexGet>(volume_).begin(root_,4,{cache_id_.data(),16},frame_,capacity_))return fail(IndexedCommitStep::Invalid);
            return result_;
        }
        if (status != IndexScanStep::Item)
            return fail(status == IndexScanStep::VolumeChanged ? IndexedCommitStep::VolumeChanged :
                        status == IndexScanStep::IoError ? IndexedCommitStep::IoError : IndexedCommitStep::Invalid);
        MutationView row;
        AuthorIssuedView issued;
        if (!scan.item(row) || !decodeAuthorIssued(row.key,row.value,issued)) return fail(IndexedCommitStep::Invalid);
        if (!std::memcmp(issued.cache_id.data,verified_->id.bytes.data(),32))
        {
            const auto& record=verified_->record;
            if (std::memcmp(issued.author_public_key.data,record.author_public_key.data,64) ||
                (issued.has_recovered_creation && issued.recovered_created_at != record.created_at) ||
                (issued.revision == 1 && (!issued.issued_at.has_utc || issued.issued_at.utc_seconds != record.created_at)))
                return fail(IndexedCommitStep::Invalid);
        }
        if (!scan.advance()) return fail(IndexedCommitStep::Invalid);
        return result_;
    }
    IndexedCommitStep startCommit()
    {
        // The encoder creates one matching task/request pair. The commit
        // History additions are absent-only and share this one transaction.
        // No existing request, task, issuance or draft is replaced.
        const size_t absent=confirming_?count_:2;
        if (!operation_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), count_, frame_, capacity_, *candidate_, false, absent, true))
            return fail(IndexedCommitStep::Invalid);
        committing_ = true;
        return result_;
    }
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        operation_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::array<::geocaching::storage::MutationView, 4> mutations_{};
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 16> task_id_{};
    std::array<uint8_t, 32> cache_id_{};
    std::array<uint8_t, 64> head_{};
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, count_ = 0;
    uint64_t generation_ = 0;
    unsigned copy_ = 0;
    bool committing_ = false;
    bool confirming_ = false, duplicate_ = false;
    uint8_t confirmation_phase_ = 0;
    ::geocaching::ByteView confirmed_request_,confirmation_,recovery_work_;
    ::platform::memory::PsramPtr<::geocaching::protocol::VerifiedRecordView> verified_;
    std::variant<std::monostate, SdIndexGet, SdIndexScan, SdIndexedCommit> operation_;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedNewTask) <= 1536, "Task creation owns metadata only");
} // namespace platform::esp::arduino_common::geocaching
