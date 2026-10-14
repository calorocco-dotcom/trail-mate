#pragma once
#include "platform/esp/idf_common/hostlink_track_directories_runtime.h"
#include "platform/esp/idf_common/hostlink_track_publication_admission.h"
#include "track_sharing/publication_network_session.h"

namespace hostlink::management::publication_runtime
{
// Owns a validated SD job after successful binding handover. Scheduler visits
// and callbacks share the existing router lock. Sending occurs outside it,
// using immutable staged bytes and copied destination/current author fields.
// No USB session dependency, separate task, queue or internal-RAM fallback.
class TrackPublicationExecution
{
  public:
    using Jobs = track_sharing::PublicationJournalStore<sd::SdBorrowedRecordFileIo>;
    using Receipts = track_sharing::SignedManifestStore<sd::SdBorrowedRecordFileIo>;
    struct Lease
    {
        TrackPublicationExecution& owner;
        bool holds(const track_sharing::PublicationJournal& job) const { return owner.holds(job); }
    };
    struct Sender
    {
        TrackPublicationExecution& owner;
        chat::MeshSendResult sendTrackData(const uint8_t* destination, chat::lxmf::ByteSpan bytes, const uint8_t* author, uint32_t generation, bool response, std::array<uint8_t, 32>* hash)
        {
            return owner.sendTrackData(destination, bytes, author, generation, response, hash);
        }
    };
    struct Nonce
    {
        TrackPublicationExecution& owner;
        bool fill(uint8_t* output, size_t size) { return owner.fill(output, size); }
    };
    using Session = track_sharing::PublicationNetworkSession<sd::SdBorrowedRecordFileIo, Jobs, Receipts, platform::esp::storage::TrackReceiptCrypto, Lease, Sender, Nonce>;
    static TrackPublicationExecution* create(TrackPublicationAdmission*& input, chat::IMeshAdapter& adapter, const uint8_t* local,
                                             chat::TrackBindingChange& changed)
    {
        changed = chat::TrackBindingChange::Conflict;
        if (!input || input->readOnly() || !input->ready() || !track_sharing::networkRequestId({local, 16})) return nullptr;
        constexpr auto caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        if (heap_caps_get_free_size(caps) < sizeof(TrackPublicationExecution) + 8192 || heap_caps_get_largest_free_block(caps) < sizeof(TrackPublicationExecution))
        {
            changed = chat::TrackBindingChange::Busy;
            return nullptr;
        }
        void* memory = heap_caps_malloc(sizeof(TrackPublicationExecution), caps);
        if (!memory)
        {
            changed = chat::TrackBindingChange::Busy;
            return nullptr;
        }
        auto* created = new (memory) TrackPublicationExecution(*input, adapter, local);
        if (!created->initialize())
        {
            created->~TrackPublicationExecution();
            heap_caps_free(memory);
            return nullptr;
        }
        changed = directory_runtime::handover(adapter, &created->network_.binding());
        if (changed != chat::TrackBindingChange::Applied)
        {
            created->~TrackPublicationExecution();
            heap_caps_free(memory);
            return nullptr;
        }
        created->owns_ = true;
        input = nullptr;
        return created;
    }
    static bool release(TrackPublicationExecution*& work)
    {
        if (!work) return true;
        const auto changed = work->adapter_.replaceTrackDelivery(&work->network_.binding(), nullptr);
        if (changed != chat::TrackBindingChange::Applied && changed != chat::TrackBindingChange::Conflict) return false;
        work->~TrackPublicationExecution();
        heap_caps_free(work);
        work = nullptr;
        return true;
    }
    track_sharing::NetworkSessionResult advance(uint64_t now)
    {
        now_ = now;
        const auto visited = adapter_.visitTrackDelivery(&network_.binding(), author_, generation_, drive, this);
        if (visited != chat::TrackBindingChange::Applied) return visited == chat::TrackBindingChange::Busy ? track_sharing::NetworkSessionResult::Waiting : track_sharing::NetworkSessionResult::Conflict;
        if (!bytes_) return result_;
        const auto sent = adapter_.sendTrackData(destination_, {wire_, bytes_}, author_, generation_, false, nullptr);
        if (sent.ok) return track_sharing::NetworkSessionResult::Queued;
        rejected_ = true;
        // Busy leaves the rejection marker for the next serialized visit.
        adapter_.visitTrackDelivery(&network_.binding(), author_, generation_, reject, this);
        return track_sharing::NetworkSessionResult::Rejected;
    }
    chat::TrackBindingChange snapshot(track_sharing::PublicationJournal& output)
    {
        struct Copy {TrackPublicationExecution& work;track_sharing::PublicationJournal& output;} copy{*this,output};
        return adapter_.visitTrackDelivery(&network_.binding(),author_,generation_,[](void* value)
        {
            auto& data=*static_cast<Copy*>(value);data.output=data.work.input_.job_;
        },&copy);
    }
    Status cancel(geocaching::ByteView operation,uint32_t generation,track_sharing::PublicationJournal& output)
    {
        if(!operation.data || operation.size!=16 || !generation)return Status::InvalidField;
        if(generation!=input_.request_.generation)return Status::StaleIdentity;
        struct Cancel
        {
            TrackPublicationExecution& work;geocaching::ByteView operation;track_sharing::PublicationJournal& output;
            track_sharing::PublicationStoreResult result=track_sharing::PublicationStoreResult::Conflict;
        } call{*this,operation,output};
        const auto visited=adapter_.visitTrackDelivery(&network_.binding(),author_,generation_,[](void* value)
        {
            auto& data=*static_cast<Cancel*>(value);auto& work=data.work;
            if(std::memcmp(data.operation.data,work.input_.job_.operation,16))return;
            data.result=work.network_.cancel(work.generation_,{work.input_.author_hash_,32});
            if(data.result==track_sharing::PublicationStoreResult::Ready || data.result==track_sharing::PublicationStoreResult::StorageFailure)
            {work.bytes_=0;work.rejected_=false;}
            if(data.result==track_sharing::PublicationStoreResult::Ready)data.output=work.input_.job_;
        },&call);
        if(visited!=chat::TrackBindingChange::Applied)return visited==chat::TrackBindingChange::Busy?Status::Busy:Status::StaleIdentity;
        return call.result==track_sharing::PublicationStoreResult::Ready?Status::Ok:
            call.result==track_sharing::PublicationStoreResult::StorageFailure?Status::StorageUnavailable:Status::Conflict;
    }
    bool holds(const track_sharing::PublicationJournal& job) const
    {
        return &job == &input_.job_ && input_.ready() && track_sharing::publicationDirectoryMatches(input_.directory_, job) &&
               job.generation == input_.request_.generation && !std::memcmp(job.author_hash, input_.author_hash_, 32);
    }
    bool fill(uint8_t* output, size_t size)
    {
        if (!output || size != 16) return false;
        esp_fill_random(output, size);
        return track_sharing::networkRequestId({output, size});
    }
    chat::MeshSendResult sendTrackData(const uint8_t* destination, chat::lxmf::ByteSpan bytes, const uint8_t* author, uint32_t generation,
                                       bool response, std::array<uint8_t, 32>* hash)
    {
        if (hash) hash->fill(0);
        if (!destination || !author || bytes.data != wire_ || !bytes.size || bytes.size > sizeof(wire_) || response ||
            generation != generation_ || std::memcmp(author, author_, 64)) return chat::MeshSendResult::fail(chat::MeshOperationFailure::InvalidInput);
        std::memcpy(destination_, destination, 16);
        bytes_ = bytes.size;
        return chat::MeshSendResult::success(0); // Staging only; dispatch follows outside router lock.
    }

  private:
    TrackPublicationExecution(TrackPublicationAdmission& input, chat::IMeshAdapter& adapter, const uint8_t* local)
        : input_(input), adapter_(adapter), receipts_(input.io_, receipt_temporary_, receipt_path_), lease_{*this}, sender_{*this}, nonce_{*this},
          network_(input.io_, input.jobs_, receipts_, input.receipt_crypto_, lease_, sender_, nonce_, input.job_, input.desired_, input.job_verify_, input.manifest_, input.receipt_,
                   {input.directory_.public_key, 64}, local, input.author_, input.kml_path_, wire_, sizeof(wire_), input.workspace_, sizeof(input.workspace_), file_, sizeof(file_), 30000, 3000, 8) {}
    ~TrackPublicationExecution()
    {
        if (owns_) destroyPublicationAdmission(&input_);
    }
    bool initialize()
    {
        return track_sharing::resolveObjectPath(input_.request_.object, track_sharing::ObjectFile::ReceiptTemporary, input_.request_.operation, receipt_temporary_, sizeof(receipt_temporary_)) &&
               track_sharing::resolveObjectPath(input_.request_.object, track_sharing::ObjectFile::Receipt, input_.request_.operation, receipt_path_, sizeof(receipt_path_));
    }
    static void reject(void* context)
    {
        auto& work = *static_cast<TrackPublicationExecution*>(context);
        if (work.rejected_)
        {
            work.network_.dispatchRejected(work.now_);
            work.rejected_ = false;
        }
    }
    static void drive(void* context)
    {
        auto& work = *static_cast<TrackPublicationExecution*>(context);
        reject(context);
        work.bytes_ = 0;
        work.result_ = work.network_.advance(work.now_);
    }
    TrackPublicationAdmission& input_;
    chat::IMeshAdapter& adapter_;
    char receipt_temporary_[160]{}, receipt_path_[160]{};
    uint8_t wire_[4300]{}, file_[256]{}, author_[64]{}, destination_[16]{};
    uint32_t generation_ = 0;
    size_t bytes_ = 0;
    uint64_t now_ = 0;
    Receipts receipts_;
    Lease lease_;
    Sender sender_;
    Nonce nonce_;
    Session network_;
    track_sharing::NetworkSessionResult result_ = track_sharing::NetworkSessionResult::Waiting;
    bool owns_ = false, rejected_ = false;
};
static_assert(sizeof(TrackPublicationExecution) <= 6144, "Execution context must remain transient PSRAM only");
} // namespace hostlink::management::publication_runtime
