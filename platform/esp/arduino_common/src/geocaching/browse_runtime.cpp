#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "app/app_facade_access.h"
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/storage/download_recovery.h"
#include "geocaching/storage/draft_publication.h"
#include "geocaching/storage/publication_history_job.h"
#include "geocaching/storage/record_shape.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/esp/arduino_common/geocaching/author_issue_port.h"
#include "platform/esp/arduino_common/geocaching/cache_detail.h"
#include "platform/esp/arduino_common/geocaching/indexed_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_download_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_publication_store.h"
#include "platform/esp/arduino_common/geocaching/live_query_port.h"
#include "platform/esp/arduino_common/geocaching/query_browse_source.h"
#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "platform/esp/arduino_common/geocaching/saved_cache_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_rotation.h"
#include "platform/esp/arduino_common/geocaching/sd_download_port.h"
#include "platform/esp/arduino_common/geocaching/sd_index_repair.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_remove_saved.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"
#include "platform/esp/arduino_common/geocaching/sd_publish_port.h"
#include "platform/esp/arduino_common/geocaching/stored_reply_receipt.h"
#include "platform/esp/arduino_common/geocaching/web_confirmation_job.h"
#include "platform/esp/common/geocaching_crypto.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/esp/common/meshcore_runtime_compat.h"
#include "platform/esp/common/reticulum_runtime_compat.h"
#include "platform/memory/psram_ptr.h"
#include "ui/screens/geocaching/geocaching_page_shell.h"
#include <atomic>
#include <ctime>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching::browse_runtime
{
namespace
{
namespace gc = ::geocaching;
namespace mem = ::platform::esp::common::memory;
using Digest = ::platform::esp::common::meshcore_runtime::Sha256Digest;
constexpr size_t kFrameCapacity = 8192, kEncodingCapacity = 8192, kPayloadCapacity = 8192;
constexpr size_t kVerificationCapacity = gc::kMaxRecordBytes + 64;
constexpr gc::protocol::QueryRegion kWorld{-900000000, -1800000000, 900000000, 1800000000};
enum class Phase : uint8_t
{
    Inspect,
    CheckNew,
    Directories,
    OpenFormat,
    WriteFormat,
    FlushFormat,
    CloseFormat,
    VerifyFormat,
    Recover,
    ResumeDownloads,
    Ready,
    Failed
};
struct Announcement
{
    gc::Destination discovery, delivery;
    std::array<uint8_t, 64> key{};
    std::array<uint8_t, 128> data{};
    size_t size = 0;
};
// One producer (serialized router callbacks), one storage-owner consumer.
// The mailbox payload exists only while pending and is allocated in PSRAM.
std::atomic<Announcement*> pending_announcement{nullptr};
static_assert(sizeof(Announcement) <= 240);
// Router callbacks are serialized. The storage owner consumes this one-slot
// mailbox without sharing its long-lived session/SD mutex with the producer.
// A full mailbox applies backpressure: no receipt is acknowledged or replaced.
struct PendingReply
{
    gc::Destination source, destination;
    uint8_t* bytes = nullptr;
    size_t size = 0;
    ~PendingReply() { heap_caps_free(bytes); }
};
std::atomic<PendingReply*> pending_reply{nullptr};
struct Session
{
    uint64_t local_map_revision = 0;
    uint64_t saved_catalog_epoch = 0, draft_catalog_epoch = 0;
    uint64_t discover_catalog_epoch = 0, discover_saved_epoch = 0, discover_saved_generation = 0;
    struct Publication
    {
        enum class DraftStage : uint8_t
        {
            None,
            Bind,
            Binding,
            Encode,
            ArchiveLookup,
            Archive,
            ArchiveSaving,
            Sign
        };
        DraftStage draft_stage = DraftStage::None;
        std::array<uint8_t, 16> draft_id{};
        std::array<uint8_t, 64> author{};
        uint64_t draft_generation = 0;
        uint32_t expected_revision = 0;
        gc::storage::StoredTime issued;
        uint8_t* unsigned_bytes = nullptr;
        size_t unsigned_size = 0;
        uint8_t* draft_bytes = nullptr;
        size_t draft_size = 0;
        gc::GeocacheId cache;
        gc::storage::PublicationHistory history;
        ::platform::memory::PsramPtr<DeviceAuthorIssuePort> author_port;
        ::platform::memory::PsramPtr<gc::AuthorIssue> issue;
        ::platform::memory::PsramPtr<SdIndexGet> archive_read;
        const char* error = nullptr;
        uint8_t* bytes = nullptr;
        size_t size = 0;
        gc::Destination remote;
        ::platform::memory::PsramPtr<SdPublishPort> port;
        ::platform::memory::PsramPtr<gc::PublishAttempt> attempt;
        uint64_t started = 0;
        uint32_t wait_ms = gc::QueryClient::kReplyTimeoutMs;
        bool submitted = false;
        bool web_operation = false;
        bool stop_requested = false;            // Existing tail padding; size assertion below forbids growth.
        std::array<uint8_t, 16> operation_id{}; // Only the existing on-demand PSRAM job grows.
        ~Publication()
        {
            heap_caps_free(bytes);
            heap_caps_free(unsigned_bytes);
            heap_caps_free(draft_bytes);
        }
    };
    static_assert(sizeof(Publication) == ((offsetof(Publication, submitted) + 1 + alignof(Publication) - 1) / alignof(Publication)) * alignof(Publication) + 16,
                  "Web operation linkage must cost exactly 16 bytes in the on-demand job");
    ::platform::memory::PsramPtr<Publication> publication;
    chat::MeshOperationFailure last_dispatch_failure = chat::MeshOperationFailure::None;
    // Recovery discovers work left by a previous session. New work in this
    // session already has an owner; unrelated query commits must not rescan it.
    bool publication_recovery_complete = false;
    bool publication_restore_pending = false, publication_restore_background = false;
    struct DraftSave
    {
        std::array<uint8_t, 16> id{};
        uint8_t* bytes = nullptr;
        size_t size = 0, capacity = 0;
        uint64_t expected = 0;
        bool started = false, done = false, saved = false, editor_fields = true;
        bool erase = false;
        bool erase_download = false;
        bool web_file = false, web_job = false;
        std::array<uint8_t, 32> cache{}, hash{};
        ::platform::memory::PsramPtr<SdIndexedRemoveSaved> removal;
        ~DraftSave() { heap_caps_free(bytes); }
    };
    static_assert(sizeof(DraftSave) == (sizeof(void*) == 4 ? 120 : 128), "Web staging grew the existing save job");
    ::platform::memory::PsramPtr<DraftSave> draft_save;
    struct DraftRead
    {
        std::array<uint8_t, 16> id{};
        uint8_t* bytes = nullptr;
        size_t size = 0;
        ::ui::geocaching::DraftReadStatus status = ::ui::geocaching::DraftReadStatus::Pending;
        uint8_t users = 0; // 1 UI, 2 browser; occupies existing tail padding.
        DraftReadResult result = DraftReadResult::Pending;
        ~DraftRead() { heap_caps_free(bytes); }
    };
    static_assert(sizeof(DraftRead) == (sizeof(void*) == 4 ? 28 : 40), "Shared draft reading grew its memory budget");
    ::platform::memory::PsramPtr<DraftRead> draft_read;
    bool draft_io_reset = false;
    struct DraftCatalog
    {
        gc::storage::DraftCatalogPage page;
        size_t requested_offset = 0, total = 0;
        uint64_t sequence = UINT64_MAX, generation = 1;
        bool reading = false, ready = false, failed = false;
    };
    ::platform::memory::PsramPtr<DraftCatalog> draft_catalog;
    bool draft_catalog_wanted = false;
    bool map_metadata_only = false;
    Phase phase = Phase::Inspect;
    const char* status = "Opening geocaching storage...";
    const char* notice = nullptr;
    const char* browse_status = "Connecting to Reticulum...";
    bool storage_requested = false;
    bool local_read_only = false, local_snapshot_attempted = false;
    std::array<uint8_t, 32> legacy_projection_cache{};
    bool legacy_projection_pending = false, projection_upgrade_admitted = false;
    uint8_t startup_read_retries = 0;
    size_t saved_offset = 0, saved_count = 0;
    gc::storage::VolumeInstance volume{};
    storage::SdRuntimeFile format;
    size_t directory = 0;
    bool mkdir_pending = false;
    // Roots and the current query page survive between operations. All other
    // byte buffers belong to the single active storage lease and are trimmed
    // after the maintenance step that consumed its final borrowed views.
    std::array<gc::storage::IndexRootBytes, 2> roots{};
    gc::storage::IndexRootView root;
    unsigned root_copy = 0;
    IndexWorkspaceOwner workspace_owner;
    gc::storage::QueuedRequestWorkspace workspace{nullptr, kEncodingCapacity};
    uint8_t *frame = nullptr, *encoded = nullptr, *payload = nullptr, *verification = nullptr;
    uint8_t* response = nullptr;
    bool workspace_unavailable = false;
    size_t response_size = 0;
    ::platform::memory::PsramPtr<gc::protocol::VerifiedRecordView> response_verified;
    gc::Destination response_source;
    gc::Destination local;
    gc::RequestId response_id;
    uint8_t response_operation = 0;
    std::array<gc::storage::MutationView, 3> mutations{};
    ::platform::memory::PsramPtr<SdIndexRepair<Digest>> recovery;
    ::platform::memory::PsramPtr<SdCheckpointRotation<Digest>> checkpoint;
    uint64_t checkpoint_attempt_sequence = 0;
    bool current_index_upgrade_pending = false;
    uint32_t current_index_upgrade_retry_ms = 0;
    uint64_t current_index_upgrade_deferred_revision = 0;
    bool checkpoint_recovery_required = false;
    ::platform::memory::PsramPtr<IndexedPublicationStore> store;
    struct WebSignature
    {
        std::array<uint8_t, 16> draft{};
        uint64_t draft_generation = 0;
        DeviceAuthorIssuePort port;
        gc::AuthorIssue issue;
        std::array<uint8_t, 16> operation{}, owner{}, transfer{};
        std::array<uint8_t, 64> author{};
        std::array<uint8_t, 32> hash{};
        uint32_t generation = 0, media = 0, total = 0, offset = 0;
        uint64_t started = 0;
        uint8_t* record = nullptr;
        uint8_t* work = nullptr;
        size_t capacity = 0;
        storage::SdRuntimeFile file;
        Digest digest;
        bool issuing = false, failed = false;
        WebSignature(chat::MeshAdapterRouter& router, PublicationStore& store, gc::protocol::RecordCrypto& crypto,
                     const gc::storage::StoredTime& issued, const uint8_t* draft_id = nullptr, uint64_t draft_version = 0)
            : draft_generation(draft_version), port(router, store, crypto, issued,
                                                    draft_id ? gc::ByteView{draft.data(), draft.size()} : gc::ByteView{}, draft_version, draft_id != nullptr),
              issue(port)
        {
            if (draft_id) std::memcpy(draft.data(), draft_id, draft.size());
        }
        void fail()
        {
            issue.cancel();
            file.close();
            failed = true;
            heap_caps_free(record);
            record = nullptr;
            heap_caps_free(work);
            work = nullptr;
        }
        ~WebSignature()
        {
            issue.cancel();
            file.close();
            heap_caps_free(record);
            heap_caps_free(work);
        }
    };
    ::platform::memory::PsramPtr<WebSignature> web_signature;
    struct WebConfirmation
    {
        WebConfirmationJob<PublicationStore> job;
        std::array<uint8_t, 16> transfer{}, owner{};
        std::array<uint8_t, 32> hash{}, proof_hash{};
        std::array<uint8_t, 64> author{};
        uint32_t generation = 0, media = 0, total = 0, offset = 0;
        uint64_t started = 0;
        uint8_t *source = nullptr, *work = nullptr, *proof = nullptr;
        size_t capacity = 0, proof_size = 0;
        storage::SdRuntimeFile file;
        Digest digest;
        bool begun = false, failed = false, released = false;
        WebConfirmation(PublicationStore& store, gc::protocol::RecordCrypto& crypto) : job(store, crypto) {}
        void releasePayload()
        {
            file.close();
            heap_caps_free(source);
            source = nullptr;
            heap_caps_free(work);
            work = nullptr;
            heap_caps_free(proof);
            proof = nullptr;
            released = true;
        }
        void fail()
        {
            job.cancel();
            failed = true;
            releasePayload();
        }
        ~WebConfirmation()
        {
            job.cancel();
            releasePayload();
        }
    };
    ::platform::memory::PsramPtr<WebConfirmation> web_confirmation;
    ::platform::memory::PsramPtr<IndexedDispatchStore> dispatch_store;
    ::platform::memory::PsramPtr<LiveQueryPort> port;
    ::platform::memory::PsramPtr<CacheDetail> detail, pending_detail;
    ::platform::memory::PsramPtr<StoredReplyReceipt> receipts;
    ::platform::memory::PsramPtr<gc::QueryClient> client;
    ::platform::memory::PsramPtr<RequestDispatcher> dispatcher;
    ::platform::memory::PsramPtr<QueryBrowseSource> source;
    ::platform::memory::PsramPtr<SavedCacheCatalog<Digest>> saved;
    ::platform::esp::common::EspGeocachingCrypto crypto;
    ::platform::memory::PsramPtr<IndexedDownloadStore> download_store;
    struct DownloadStart
    {
        gc::protocol::SummaryView summary;
        std::array<char, gc::kMaxNameBytes> name{};
        gc::Destination remote;
        gc::RequestId request;
        uint8_t* response = nullptr;
        size_t response_size = 0;
        ::platform::memory::PsramPtr<gc::protocol::VerifiedRecordView> verified;
        ~DownloadStart() { heap_caps_free(response); }
    };
    ::platform::memory::PsramPtr<DownloadStart> download_start;
    const char* download_start_error = nullptr;
    bool download_recovery_complete = false;
    bool download_restore_pending = false;
    ::platform::memory::PsramPtr<SdDownloadPort<Digest>> download_port;
    ::platform::memory::PsramPtr<gc::DownloadClient> download;
    std::array<uint8_t, 48> recovered_download{};
    bool have_recovered_download = false;
    bool recovering_installed_download = false, recovery_attention = false;
    uint64_t download_started = 0;
    uint32_t download_wait_ms = gc::QueryClient::kReplyTimeoutMs;
    size_t download_scratch = 0;
    chat::IMeshAdapter* created_backend = nullptr;
    bool ensureBuffers(bool operations)
    {
        const bool needs_verification = operations;
        const size_t missing = (!frame ? kFrameCapacity : 0) + (!encoded ? kEncodingCapacity : 0) +
                               (operations && !payload ? kPayloadCapacity : 0) + (needs_verification && !verification ? kVerificationCapacity : 0);
        // These buffers are PSRAM-only (no internal fallback below). Charging
        // an unrelated internal reserve permanently blocks local reads on L2.
        if (missing && !mem::admit("geocaching.index.io", 0, 0, missing, 0, 0))
        {
            workspace_unavailable = true;
            return false;
        }
        if (!frame) frame = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.read", kFrameCapacity, false));
        if (!encoded) encoded = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.encode", kEncodingCapacity, false));
        if (operations && !payload) payload = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.payload", kPayloadCapacity, false));
        if (needs_verification && !verification) verification = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.verify", kVerificationCapacity, false));
        workspace.outgoing = encoded;
        workspace_unavailable = !frame || !encoded || (operations && !payload) || (needs_verification && !verification);
        return !workspace_unavailable;
    }
    static bool prepareWorkspace(void* context, const void* owner)
    {
        auto& s = *static_cast<Session*>(context);
        const bool metadata = owner == s.download_store.get() && s.download_store->metadataRead();
        const bool needs_record = owner == s.store.get() || (owner == s.download_store.get() && !metadata);
        if (!s.ensureBuffers(needs_record)) return false;
        if (s.store) s.store->bindWorkspace(s.frame, s.payload, s.verification);
        if (s.download_store) s.download_store->bindWorkspace(s.frame, s.payload, s.verification);
        if (s.dispatch_store) s.dispatch_store->bindWorkspace(s.frame);
        if (s.receipts) s.receipts->bindWorkspace(s.frame);
        return true;
    }
    void trimBuffers()
    {
        if (recovery || workspace_owner.holder()) return;
        heap_caps_free(frame);
        heap_caps_free(encoded);
        heap_caps_free(payload);
        heap_caps_free(verification);
        frame = encoded = payload = verification = workspace.outgoing = nullptr;
    }
    bool needsRecovery() const
    {
        return checkpoint_recovery_required || (store && store->needsRecovery()) || (download_store && download_store->needsRecovery()) ||
               (dispatch_store && dispatch_store->needsRecovery());
    }
    ~Session()
    {
        web_signature.reset(); // Its reservation port borrows store.
        web_confirmation.reset();
        workspace_owner.release(detail.get());
        detail.reset();
        workspace_owner.release(checkpoint.get());
        checkpoint.reset();
        if (store && store->commitPending()) store->cancelCommit();
        source.reset();
        saved.reset();
        download.reset();
        download_port.reset();
        download_store.reset();
        publication.reset();
        dispatcher.reset();
        dispatch_store.reset();
        client.reset();
        port.reset();
        store.reset();
        recovery.reset();
        heap_caps_free(response);
        receipts.reset();
        trimBuffers();
    }
};
chat::MeshAdapterRouter* router = nullptr;
LoraBoard* board = nullptr;
std::atomic<SemaphoreHandle_t> mutex{nullptr};
static_assert(sizeof(mutex) == sizeof(SemaphoreHandle_t), "Lazy Geocaching lock must not grow static state");
::platform::memory::PsramPtr<Session> session;
std::array<uint8_t, 16> boot{};
std::atomic<uint8_t> wanted{0}; // 1 UI, 2 browser, same size as the former bool.
std::atomic<bool> active{false}, restart{false};
std::atomic<bool> network_requested{false};
std::atomic<bool> local_storage_requested{false};
std::atomic<uint8_t> cancel_draft_read{0};
std::atomic<bool> cancel_detail{false};
std::atomic<uint32_t> next_step{0};
std::atomic<uint32_t> replies_seen{0}, replies_busy{0}, replies_queued{0};
uint32_t replies_processed = 0, replies_accepted = 0;
uint64_t epoch = 0;
uint64_t discoverScope(Session& s)
{
    const auto saved_generation = s.saved ? s.saved->generation() : 0;
    if (s.discover_saved_epoch != s.saved_catalog_epoch || s.discover_saved_generation != saved_generation)
    {
        s.discover_saved_epoch = s.saved_catalog_epoch;
        s.discover_saved_generation = saved_generation;
        s.discover_catalog_epoch = ++epoch;
    }
    return s.discover_catalog_epoch;
}
void reportPublication(const char* event, const gc::PublishAttempt& attempt)
{
    const auto& id = attempt.cacheId().bytes;
    Serial.printf("[Geocaching][Publication] %s cache_prefix=%02x%02x%02x%02x revision=%lu state=%u\n",
                  event, id[0], id[1], id[2], id[3], static_cast<unsigned long>(attempt.revision()), static_cast<unsigned>(attempt.state()));
}
// Dispatch consumes borrowed request bytes after releasing its read lease.
// Free the backing memory only when the entire maintenance slice returns.
struct WorkspaceSlice
{
    Session& session;
    const bool unavailable = session.workspace_unavailable;
    ~WorkspaceSlice()
    {
        session.trimBuffers();
        if (unavailable != session.workspace_unavailable) ++epoch;
        if (session.workspace_unavailable) next_step.store(millis() + 1000);
        const uint32_t now_ms = millis();
        static uint32_t reported_ms = 0;
        static unsigned reported_phase = ~0U;
        const unsigned query = session.client ? static_cast<unsigned>(session.client->phase()) : 255;
        const unsigned phase = (static_cast<unsigned>(session.phase) << 8) | query;
        if (phase == reported_phase && static_cast<uint32_t>(now_ms - reported_ms) < 60000) return;
        reported_ms = now_ms;
        reported_phase = phase;
        const auto& owner = session.workspace_owner;
        const char* lease = !owner.holder()                              ? "none"
                            : owner.heldBy(session.receipts.get())       ? "query"
                            : owner.heldBy(session.dispatch_store.get()) ? "dispatch"
                            : owner.heldBy(session.download_store.get()) ? "download"
                            : owner.heldBy(session.store.get())          ? "publication"
                                                                         : "other";
        Serial.printf("[Geocaching] state=%u query=%u sequence=%llu lease=%s dispatch=%u response=%u proof=%u restore_pub=%u restore_download=%u rx=%lu busy=%lu queued=%lu processed=%lu accepted=%lu\n",
                      static_cast<unsigned>(session.phase), query, static_cast<unsigned long long>(session.root.sequence), lease,
                      session.dispatch_store && session.dispatch_store->busy(), session.response != nullptr,
                      session.receipts && session.receipts->pending(), session.publication_restore_pending, session.download_restore_pending,
                      static_cast<unsigned long>(replies_seen.load()), static_cast<unsigned long>(replies_busy.load()),
                      static_cast<unsigned long>(replies_queued.load()), static_cast<unsigned long>(replies_processed),
                      static_cast<unsigned long>(replies_accepted));
    }
};
bool downloadActive()
{
    if (session && session->download_start) return true;
    if (!session || !session->download) return false;
    const auto phase = session->download->phase();
    return phase == gc::DownloadPhase::Submitting || phase == gc::DownloadPhase::Waiting ||
           phase == gc::DownloadPhase::Installing || phase == gc::DownloadPhase::Cancelling;
}
bool publicationActive()
{
    if (session && (session->web_signature || session->web_confirmation)) return true;
    if (!session || !session->publication) return false;
    const auto& job = *session->publication;
    if (job.draft_stage != Session::Publication::DraftStage::None) return true;
    if (job.bytes) return true;
    if (!job.attempt) return false;
    const auto phase = job.attempt->phase();
    return phase == gc::PublishAttemptPhase::Submitting || phase == gc::PublishAttemptPhase::Waiting ||
           phase == gc::PublishAttemptPhase::Committing || phase == gc::PublishAttemptPhase::Cancelling;
}
bool draftSaveActive() { return session && session->draft_save && !session->draft_save->done; }
bool draftCatalogReady(const Session& s)
{
    return s.draft_catalog && s.draft_catalog->ready && s.store && !s.needsRecovery() &&
           s.draft_catalog->sequence == s.store->catalogGeneration() && s.draft_catalog->page.offset == s.draft_catalog->requested_offset;
}
bool draftMetadataReady(const Session& s)
{
    return s.draft_catalog && s.draft_catalog->page.metadata_ready && s.store && !s.needsRecovery() &&
           s.draft_catalog->sequence == s.store->catalogGeneration() &&
           s.draft_catalog->page.offset == s.draft_catalog->requested_offset;
}
bool advanceDraftCatalog(Session& s)
{
    if (!s.draft_catalog_wanted || !s.store || s.needsRecovery() || (s.phase != Phase::Ready && s.phase != Phase::ResumeDownloads)) return false;
    if (!s.draft_catalog)
    {
        s.draft_catalog.reset(::platform::memory::createPsram<Session::DraftCatalog>());
        if (s.draft_catalog) s.draft_catalog_epoch = ++epoch;
    }
    if (!s.draft_catalog) return false;
    auto& catalog = *s.draft_catalog;
    if (s.map_metadata_only && draftMetadataReady(s))
    {
        if (catalog.reading)
        {
            s.store->releaseDraftRead();
            catalog.reading = false;
        }
        return false;
    }
    if (draftCatalogReady(s) || (catalog.failed && catalog.sequence == s.store->catalogGeneration())) return false;
    if (!catalog.reading)
    {
        catalog.sequence = s.store->catalogGeneration();
        catalog.ready = false;
        catalog.page.metadata_ready = false;
        catalog.failed = false;
    }
    const bool had_metadata = catalog.page.metadata_ready;
    const auto result = s.store->readDraftCatalog(catalog.requested_offset, s.crypto, catalog.page);
    if (!had_metadata && catalog.page.metadata_ready)
    {
        catalog.total = catalog.page.total;
        ++catalog.generation;
    }
    if (result == DraftReadResult::Busy) return false;
    catalog.reading = result == DraftReadResult::Pending;
    if (catalog.reading) return true;
    catalog.ready = result == DraftReadResult::Ready;
    catalog.failed = !catalog.ready;
    if (catalog.ready) catalog.total = catalog.page.total;
    ++catalog.generation;
    return true;
}
struct Guard
{
    SemaphoreHandle_t handle = mutex.load(std::memory_order_acquire);
    bool locked = handle && xSemaphoreTake(handle, 0) == pdTRUE;
    ~Guard()
    {
        if (locked) xSemaphoreGive(handle);
    }
};
bool ensureMutex()
{
    if (mutex.load(std::memory_order_acquire)) return true;
    const auto created = xSemaphoreCreateMutex();
    if (!created) return false;
    SemaphoreHandle_t expected = nullptr;
    if (!mutex.compare_exchange_strong(expected, created, std::memory_order_acq_rel))
        vSemaphoreDelete(created);
    return true;
}
void releaseDraftReaders(Session& s, uint8_t mask)
{
    if (!s.draft_read) return;
    s.draft_read->users &= static_cast<uint8_t>(~mask);
    if (!s.draft_read->users)
    {
        const bool pending = s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending;
        s.draft_read.reset();
        if (pending) s.draft_io_reset = true;
    }
}
gc::storage::StoredTime now(void*)
{
    gc::storage::StoredTime time;
    time.boot_id = boot;
    time.monotonic_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    return time;
}
bool randomId(void*, uint8_t out[16])
{
    esp_fill_random(out, 16);
    return true;
}
void fail(const char* reason)
{
    Serial.printf("[Geocaching][Storage] failed phase=%u sequence=%llu reason=%s\n",
                  static_cast<unsigned>(session->phase), static_cast<unsigned long long>(session->root.sequence), reason);
    session->local_read_only = false;
    if (session->saved) session->saved->releaseRead();
    if (session->store && session->store->commitPending()) session->store->cancelCommit();
    if (session->store) session->store->releaseDraftRead();
    session->recovery.reset();
    session->publication_restore_pending = false;
    if (session->draft_catalog)
    {
        session->draft_catalog->reading = session->draft_catalog->ready = false;
        session->draft_catalog->failed = true;
    }
    session->publication.reset();
    session->draft_save.reset();
    session->download.reset();
    session->download_port.reset();
    session->download_start.reset();
    if (session->download_store) session->download_store->releaseRead();
    session->download_restore_pending = false;
    session->phase = Phase::Failed;
    session->status = reason;
    ++epoch;
}

bool advanceCheckpoint(Session& s)
{
    if (!s.checkpoint) return false;
    const bool foreground = s.response || (s.detail && s.detail->state == CacheDetail::State::Saved) || downloadActive() || publicationActive() || draftSaveActive() ||
                            (s.saved && s.saved->pending() && !s.draft_catalog_wanted) ||
                            (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending) ||
                            (s.draft_catalog_wanted && !draftCatalogReady(s));
    if (foreground) s.checkpoint->yieldToForeground();
    const auto result = s.checkpoint->step();
    if (result == CheckpointRotationStep::Working) return true;
    if (result == CheckpointRotationStep::Busy || result == CheckpointRotationStep::Unavailable)
    {
        next_step.store(millis() + 1000);
        return true;
    }
    const auto previous_epoch = s.root.epoch;
    const bool complete = (result == CheckpointRotationStep::Complete || result == CheckpointRotationStep::Yielded) &&
                          s.checkpoint->selected(s.root, s.root_copy);
    if (complete && s.root.epoch != previous_epoch) s.current_index_upgrade_pending = false;
    if (result == CheckpointRotationStep::Deferred) s.current_index_upgrade_deferred_revision = s.root.revision;
    if (s.current_index_upgrade_pending) s.current_index_upgrade_retry_ms = millis() + 5000;
    s.workspace_owner.release(s.checkpoint.get());
    s.checkpoint.reset();
    if (!complete && result != CheckpointRotationStep::Deferred)
    {
        // Even an allocation failure may follow a published metadata copy.
        // Do not resume clients against their pre-rotation borrowed root.
        s.checkpoint_recovery_required = true;
        fail(result == CheckpointRotationStep::OutOfMemory ? "Storage maintenance needs more memory - reopen to retry"
                                                           : "Storage maintenance interrupted - reopen to recover");
    }
    return true;
}

bool startCheckpoint(Session& s)
{
    // Bound obsolete index growth without interrupting an active network
    // transaction. All consumers use the same workspace lease; acquiring it
    // proves their borrowed index cursors have been released.
    constexpr uint64_t interval = 256;
    const bool upgrade = s.current_index_upgrade_pending;
    if (upgrade && (s.current_index_upgrade_deferred_revision == s.root.revision ||
                    static_cast<int32_t>(millis() - s.current_index_upgrade_retry_ms) < 0)) return false;
    if (!upgrade && (s.root.sequence < s.checkpoint_attempt_sequence || s.root.sequence - s.checkpoint_attempt_sequence < interval)) return false;
    if (s.local_read_only ||
        s.workspace_owner.holder() || s.response || pending_announcement.load(std::memory_order_acquire) || downloadActive() || publicationActive() || draftSaveActive()) return false;
    if (upgrade && ((s.saved && s.saved->pending() && !s.draft_catalog_wanted) || (s.draft_catalog_wanted && !draftCatalogReady(s)) ||
                    (s.detail && s.detail->state == CacheDetail::State::Saved) ||
                    (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending))) return false;
    s.checkpoint.reset(::platform::memory::createPsram<SdCheckpointRotation<Digest>>(s.volume));
    if (!s.checkpoint)
    {
        next_step.store(millis() + 1000);
        return true;
    }
    if (!s.workspace_owner.acquire(s.checkpoint.get()))
    {
        s.checkpoint.reset();
        return false;
    }
    // Sorting reuses the 8 KiB encoding buffer; the smaller signature scratch
    // cannot hold a run. Import subsequently reuses it for value comparison.
    if (!s.checkpoint->begin(s.roots[0], s.roots[1], s.root_copy, s.frame, kFrameCapacity, s.encoded, kEncodingCapacity, upgrade ? 0 : interval))
    {
        s.workspace_owner.release(s.checkpoint.get());
        s.checkpoint.reset();
        s.checkpoint_recovery_required = true;
        fail("Storage maintenance could not validate index roots");
        return true;
    }
    s.checkpoint_attempt_sequence = s.root.sequence;
    return true;
}

bool resumeWaitingDownload(Session& s)
{
    if (s.download_recovery_complete) return false;
    gc::storage::DownloadRecoveryRequest recovered;
    gc::protocol::SummaryView summary;
    const auto result = s.download_store->readWaitingDownload(s.local, recovered, summary);
    if (result == DownloadRecoveryRead::Busy) return false;
    s.download_restore_pending = result == DownloadRecoveryRead::Pending;
    if (s.download_restore_pending) return true;
    if (result == DownloadRecoveryRead::Unavailable && !s.download_store->needsRecovery())
    {
        next_step.store(millis() + 1000);
        return true;
    }
    if (result == DownloadRecoveryRead::End)
    {
        s.download_recovery_complete = true;
        return false;
    }
    if (result != DownloadRecoveryRead::Ready)
    {
        fail(result == DownloadRecoveryRead::WorkspaceTooSmall ? "Insufficient download recovery workspace"
             : result == DownloadRecoveryRead::IoError         ? "Cannot read waiting download"
             : result == DownloadRecoveryRead::VolumeChanged   ? "Download storage volume changed"
                                                               : "Download preview missing - recovery needs attention");
        return true;
    }
    // The name is the only borrowed preview field. Preserve this small text
    // while releasing the shared frame for the port's request validation.
    std::array<char, gc::kMaxNameBytes> name{};
    std::memcpy(name.data(), summary.name.data(), summary.name.size());
    summary.name = {name.data(), summary.name.size()};
    s.download_store->releaseRead();
    gc::Destination remote;
    gc::RequestId request;
    std::memcpy(remote.bytes.data(), recovered.key.data() + 16, 16);
    std::memcpy(request.bytes.data(), recovered.key.data() + 32, 16);
    s.download_port.reset(::platform::memory::createPsram<SdDownloadPort<Digest>>(*s.download_store, s.crypto, s.local,
                                                                                  recovered.identity, recovered.task, recovered.created));
    if (s.download_port) s.download.reset(::platform::memory::createPsram<gc::DownloadClient>(*s.download_port, s.crypto));
    if (!s.download)
    {
        s.download_port.reset();
        next_step.store(millis() + 1000);
        return true;
    }
    if (!s.download->resume(remote, request, summary, recovered.identity.generation, s.download_port->resumeWaiting(remote, request)))
    {
        fail("Cannot resume download request");
        return true;
    }
    s.download_started = now(nullptr).monotonic_ms;
    // Allow expiry/retry of the previous boot's attempt before reply timeout.
    s.download_wait_ms = 2 * gc::QueryClient::kReplyTimeoutMs + 5000;
    s.download_scratch = summary.signed_bytes + 64;
    ++epoch;
    return true;
}

bool advanceDownloadStart(Session& s)
{
    if (!s.download_start) return false;
    // Dispose of the previous completed port before taking the new read lease.
    s.download.reset();
    s.download_port.reset();
    auto& job = *s.download_start;
    uint64_t generation = 0;
    const auto result = s.download_store->readNextGeneration(job.summary.id, generation);
    if (result == DownloadRecoveryRead::Busy) return false;
    if (result == DownloadRecoveryRead::Pending) return true;
    if (result == DownloadRecoveryRead::Unavailable && !s.download_store->needsRecovery())
    {
        next_step.store(millis() + 1000);
        return true;
    }
    if (result != DownloadRecoveryRead::Ready || !generation)
    {
        s.download_store->releaseRead();
        s.download_start.reset();
        s.download_start_error = result == DownloadRecoveryRead::Ready               ? "Download generation limit reached"
                                 : result == DownloadRecoveryRead::WorkspaceTooSmall ? "Insufficient download workspace"
                                 : result == DownloadRecoveryRead::IoError           ? "Cannot read download metadata"
                                 : result == DownloadRecoveryRead::VolumeChanged     ? "Download storage volume changed"
                                                                                     : "Download metadata needs recovery";
        ++epoch;
        return true;
    }
    std::array<uint8_t, 16> task;
    gc::RequestId request;
    randomId(nullptr, task.data());
    if (job.response) request = job.request;
    else randomId(nullptr, request.bytes.data());
    s.download_port.reset(::platform::memory::createPsram<SdDownloadPort<Digest>>(*s.download_store, s.crypto, s.local,
                                                                                  gc::InstallIdentity{job.summary.id, job.summary.hash, generation}, task, now(nullptr)));
    if (s.download_port) s.download.reset(::platform::memory::createPsram<gc::DownloadClient>(*s.download_port, s.crypto));
    if (!s.download)
    {
        s.download_port.reset();
        s.download_store->releaseRead();
        next_step.store(millis() + 1000);
        return true;
    }
    const auto scratch = job.summary.signed_bytes + 64;
    const bool begun = s.download->begin(job.remote, request, job.summary, generation);
    if (begun && job.response)
    {
        s.response = job.response;
        job.response = nullptr;
        s.response_size = job.response_size;
        s.response_source = job.remote;
        s.response_operation = 3;
        s.response_verified = std::move(job.verified);
    }
    s.download_start.reset();
    if (!begun)
    {
        s.download.reset();
        s.download_port.reset();
        s.download_start_error = "Download could not be started";
        ++epoch;
        return true;
    }
    s.download_started = now(nullptr).monotonic_ms;
    s.download_wait_ms = gc::QueryClient::kReplyTimeoutMs;
    s.download_scratch = scratch;
    ++epoch;
    return true;
}

void announcementReceived(const chat::lxmf::GeocachingAnnouncementView& message, void*)
{
    if (!wanted.load() || pending_announcement.load(std::memory_order_acquire) || message.discovery_destination.size != 16 ||
        message.delivery_destination.size != 16 || message.public_key.size != 64 || message.app_data.size > 128 ||
        !message.discovery_destination.data || !message.delivery_destination.data || !message.public_key.data || !message.app_data.data) return;
    auto pending = ::platform::memory::PsramPtr<Announcement>(::platform::memory::createPsram<Announcement>());
    if (!pending) return;
    auto& out = *pending;
    std::memcpy(out.discovery.bytes.data(), message.discovery_destination.data, 16);
    std::memcpy(out.delivery.bytes.data(), message.delivery_destination.data, 16);
    std::memcpy(out.key.data(), message.public_key.data, 64);
    std::memcpy(out.data.data(), message.app_data.data, message.app_data.size);
    out.size = message.app_data.size;
    pending_announcement.store(pending.release(), std::memory_order_release);
    next_step.store(0);
}
bool receiveResponse(const chat::lxmf::CustomDeliveryView& message, uint8_t** owned = nullptr)
{
    if (!session || !session->port || (!wanted.load() && !downloadActive() && !publicationActive()) || message.source.size != 16 ||
        message.destination.size != 16 || !message.data.data || message.data.size > 8192) return false;
    if (std::memcmp(message.destination.data, session->local.bytes.data(), 16)) return false;
    gc::Destination source;
    std::memcpy(source.bytes.data(), message.source.data, 16);
    gc::protocol::CmpReader reader({message.data.data, message.data.size});
    size_t fields = 0;
    uint64_t value = 0;
    gc::ByteView id;
    if (!reader.array(fields, 6) || fields != 6 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 1 || !reader.unsignedInteger(value) || value > 3 ||
        !reader.binary(id, 16) || id.size != 16) return false;
    gc::RequestId request;
    std::memcpy(request.bytes.data(), id.data, 16);
    if (value == 3 && session->detail && session->detail->matches(source, request))
    {
        const bool accepted = session->detail->receive(source, request, {message.data.data, message.data.size}, session->crypto, owned);
        next_step.store(0);
        return accepted;
    }
    if (value == 0 || value == 2)
    {
        const gc::ByteView bytes{message.data.data, message.data.size};
        if (session->port->accepted(source, request, bytes)) return true;
        if (!session->client || !session->client->expectsResponse(source, request)) return false;
        const bool accepted = session->client->accept(source, bytes);
        if (accepted)
        {
            ++replies_accepted;
            next_step.store(0);
        }
        return accepted;
    }
    // A first response for current work has no durable receipt yet. Process
    // it directly instead of queuing a fruitless historical receipt lookup.
    const bool current_publication = value == 1 && session->publication && session->publication->attempt &&
                                     session->publication->attempt->expectsResponse(source, request);
    const bool current_download = value == 3 && session->download && session->download->expectsResponse(source, request);
    if (!current_publication && !current_download && session->receipts &&
        session->receipts->accepted(source, request, {message.data.data, message.data.size})) return true;
    if (value == 1 && (!session->publication || !session->publication->attempt ||
                       !current_publication || session->publication->attempt->phase() != gc::PublishAttemptPhase::Waiting || message.data.size > 512)) return false;
    if (value == 3 && (!session->download || session->download->phase() != gc::DownloadPhase::Waiting ||
                       !current_download || message.data.size > session->download_scratch)) return false;
    if (value != 3 && message.data.size > 2048) return false;
    if (session->response) return false;
    auto* bytes = owned ? *owned : static_cast<uint8_t*>(mem::allocatePreferred("geocaching.rx", message.data.size, false));
    if (!bytes) return false;
    if (owned) *owned = nullptr;
    else std::memcpy(bytes, message.data.data, message.data.size);
    session->response = bytes;
    session->response_size = message.data.size;
    session->response_source = source;
    session->response_id = request;
    session->response_operation = static_cast<uint8_t>(value);
    ++replies_queued;
    next_step.store(0);
    // The sender can retry; only the exact committed response is acknowledged.
    return false;
}

bool responseReceived(const chat::lxmf::CustomDeliveryView& message, void*)
{
    ++replies_seen;
    if (!message.source.data || message.source.size != 16 || !message.destination.data || message.destination.size != 16 ||
        !message.data.data || !message.data.size || message.data.size > 8192) return false;
    Guard guard;
    if (guard.locked) return receiveResponse(message);
    ++replies_busy;
    if (!active.load() || pending_reply.load(std::memory_order_acquire)) return false;
    auto reply = ::platform::memory::PsramPtr<PendingReply>(::platform::memory::createPsram<PendingReply>());
    if (!reply) return false;
    reply->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.rx", message.data.size, false));
    if (!reply->bytes) return false;
    std::memcpy(reply->source.bytes.data(), message.source.data, 16);
    std::memcpy(reply->destination.bytes.data(), message.destination.data, 16);
    std::memcpy(reply->bytes, message.data.data, message.data.size);
    reply->size = message.data.size;
    pending_reply.store(reply.release(), std::memory_order_release);
    next_step.store(0);
    return false; // A queued reply is acknowledged after validation.
}

void drainReply(Session& s)
{
    if (s.response || !s.port) return;
    ::platform::memory::PsramPtr<PendingReply> reply(pending_reply.exchange(nullptr, std::memory_order_acq_rel));
    if (!reply) return;
    receiveResponse({{reply->source.bytes.data(), 16}, {reply->destination.bytes.data(), 16}, {}, {reply->bytes, reply->size}}, &reply->bytes);
}

bool processResponse(Session& s)
{
    if (!s.response || !s.dispatch_store || s.dispatch_store->busy()) return false;
    // The retained detail is already available while its durable download
    // request is still being committed. Do not consume it before Waiting.
    if (s.response_operation == 3 && s.download && s.download->phase() == gc::DownloadPhase::Submitting) return false;
    ++replies_processed;
    if (s.response_operation == 1 && s.publication && s.publication->attempt)
    {
        auto& attempt = *s.publication->attempt;
        const auto before = attempt.phase();
        if (attempt.accept(s.response_source, {s.response, s.response_size}) && before != gc::PublishAttemptPhase::Confirmed &&
            attempt.phase() == gc::PublishAttemptPhase::Confirmed)
        {
            ++s.local_map_revision;
            reportPublication("confirmed", attempt);
        }
        ++epoch;
    }
    else if (s.response_operation == 3 && s.download)
    {
        const auto before = s.download->phase();
        if (s.response_verified)
            s.download->acceptVerified(s.response_source, {s.response, s.response_size}, *s.response_verified);
        else
        {
            auto* scratch = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.verify", s.download_scratch, false));
            if (!scratch) return false;
            s.download->accept(s.response_source, {s.response, s.response_size}, scratch, s.download_scratch);
            heap_caps_free(scratch);
        }
        if (before != s.download->phase()) ++epoch;
    }
    heap_caps_free(s.response);
    s.response_verified.reset();
    s.response = nullptr;
    s.response_size = 0;
    return true;
}

// Browsing has no volume dependency. Run before every storage slice, including
// while saved data is recovering, unavailable, or held by USB.
void advanceBrowse(Session& s)
{
    if (!network_requested.load() && !s.client && !downloadActive() && !publicationActive()) return;
    if (auto cached = router->takeInactiveReticulumCache())
    {
        s.created_backend = nullptr;
        return;
    }
    if (!s.client || !router->backendForProtocol(chat::MeshProtocol::Reticulum))
    {
        if (!router->bindGeocachingHandlers(announcementReceived, responseReceived, nullptr)) return;
        if (!router->backendForProtocol(chat::MeshProtocol::Reticulum))
        {
            if (!mem::admit("geocaching.transport", sizeof(chat::reticulum::ReticulumAdapter) + 4096, 0,
                            sizeof(chat::lxmf::LxmfAdapter), 40 * 1024, 0, 4096))
            {
                s.browse_status = "Insufficient transport memory";
                return;
            }
            auto backend = std::unique_ptr<chat::reticulum::ReticulumAdapter>(new (std::nothrow)
                                                                                  chat::reticulum::ReticulumAdapter(*board, nullptr, chat::reticulum::ReticulumUsage::BackgroundIpService));
            if (!backend) return;
            backend->applyConfig(app::appFacade().readConfig().reticulumConfig());
            auto* created = backend.get();
            if (!router->installServiceBackend(chat::MeshProtocol::Reticulum, std::move(backend))) return;
            s.created_backend = created;
        }
    }
    if (!s.client)
    {
        if (!router->getGeocachingDispatchDestination(s.local.bytes.data()))
        {
            s.browse_status = "Waiting for Reticulum IP connection";
            return;
        }
        if (!s.port) s.port.reset(::platform::memory::createPsram<LiveQueryPort>(s.crypto, randomId));
        if (!s.port) return;
        s.client.reset(::platform::memory::createPsram<gc::QueryClient>(*s.port));
        if (!s.client) return;
        s.source.reset(::platform::memory::createPsram<QueryBrowseSource>(*s.client, *s.port, kWorld));
        if (!s.source)
        {
            s.client.reset();
            return;
        }
        s.client->query(kWorld);
        s.discover_catalog_epoch = ++epoch;
    }
    drainReply(s);
    if (auto* pending = pending_announcement.exchange(nullptr, std::memory_order_acq_rel))
    {
        ::platform::memory::PsramPtr<Announcement> owned(pending);
        const auto& incoming = *owned;
        s.client->observe(incoming.discovery, incoming.delivery, {incoming.key.data(), incoming.key.size()},
                          {incoming.data.data(), incoming.size}, now(nullptr).monotonic_ms);
    }
    s.client->tick(now(nullptr).monotonic_ms);
    const auto sent = s.port->dispatch(*router, s.local, now(nullptr).monotonic_ms);
    if (sent.failure != chat::MeshOperationFailure::None && sent.failure != s.last_dispatch_failure)
        Serial.printf("[Geocaching][Query] send_deferred failure=%u\n", static_cast<unsigned>(sent.failure));
    if (sent.ok || sent.failure != chat::MeshOperationFailure::None) s.last_dispatch_failure = sent.failure;
}

void startRecovery()
{
    auto& s = *session;
    if (!s.ensureBuffers(false))
    {
        fail("Insufficient storage workspace");
        return;
    }
    s.recovery.reset(::platform::memory::createPsram<SdIndexRepair<Digest>>(s.volume, s.roots[0], s.roots[1],
                                                                            s.frame, kFrameCapacity, s.encoded, kEncodingCapacity,
                                                                            s.mutations.data(), s.mutations.size()));
    if (!s.recovery)
    {
        fail("Insufficient memory");
        return;
    }
    s.phase = Phase::Recover;
    s.status = "Restoring geocaching tasks...";
    ++epoch;
}

bool ensurePublicationStore(Session& s)
{
    if (s.store) return true;
    if (!s.ensureBuffers(true)) return false;
    s.store.reset(::platform::memory::createPsram<IndexedPublicationStore>(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                           s.workspace_owner, s.workspace, s.frame, kFrameCapacity,
                                                                           s.payload, kPayloadCapacity, s.verification, kVerificationCapacity, s.crypto));
    return s.store != nullptr;
}

void advanceStorageRecovery(Session& s)
{
    const auto result = s.recovery->step();
    // Map tiles and the local catalogue share SD access. A transient read
    // failure before any snapshot exists must not disable this session forever.
    // Bound retries; corruption and media changes still fail immediately.
    if (result == IndexedRecoveryStep::IoError && !s.local_snapshot_attempted && s.startup_read_retries < 2)
    {
        ++s.startup_read_retries;
        startRecovery();
        next_step.store(millis() + 250 * s.startup_read_retries);
        return;
    }
    if (result == IndexedRecoveryStep::Working)
    {
        if (s.local_snapshot_attempted || draftSaveActive() || downloadActive() || publicationActive() ||
            !s.recovery->readableSnapshot(s.root, s.root_copy)) return;
        s.local_snapshot_attempted = s.local_read_only = true;
        // The root bytes belong to Session. Do not keep the recovery engine and
        // its scratch leases resident for the lifetime of an offline page.
        s.recovery.reset();
    }
    else
    {
        const auto previous_sequence = s.root.sequence;
        if (result != IndexedRecoveryStep::Restored || !s.recovery->selected(s.root, s.root_copy))
        {
            fail(result == IndexedRecoveryStep::VolumeChanged ? "Storage volume changed during recovery"
                 : result == IndexedRecoveryStep::OutOfMemory ? "Insufficient memory for storage recovery"
                 : result == IndexedRecoveryStep::IoError     ? "Cannot read cached storage"
                                                              : "Cached index needs recovery or more workspace");
            return;
        }
        if (s.root.sequence != previous_sequence) s.draft_catalog.reset();
        s.current_index_upgrade_pending = s.recovery->needsCurrentIndexUpgrade();
        s.projection_upgrade_admitted = true;
        s.current_index_upgrade_retry_ms = millis();
        s.recovery.reset();
    }
    if (!s.ensureBuffers(false))
    {
        fail("Insufficient storage workspace");
        return;
    }
    s.download_store.reset(::platform::memory::createPsram<IndexedDownloadStore>(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                                 s.workspace_owner, s.workspace, s.frame, kFrameCapacity,
                                                                                 s.payload, kPayloadCapacity, s.verification, kVerificationCapacity, s.crypto));
    if (((!s.local_read_only && !s.legacy_projection_pending) || s.draft_catalog_wanted) && !ensurePublicationStore(s))
    {
        fail("Insufficient publication storage memory");
        return;
    }
    if (!s.download_store || !s.workspace_owner.setPrepare(Session::prepareWorkspace, &s))
    {
        fail("Insufficient indexed storage memory");
        return;
    }
    s.saved.reset(::platform::memory::createPsram<SavedCacheCatalog<Digest>>(*s.download_store, s.crypto));
    if (!s.saved)
    {
        fail("Insufficient catalogue memory");
        return;
    }
    s.saved_catalog_epoch = ++epoch;
    if (s.saved_count) s.saved->requestWindow(s.saved_offset, s.saved_count);
    s.phase = s.local_read_only ? Phase::Ready : Phase::ResumeDownloads;
    s.status = s.local_read_only ? "Saved storage ready" : "Recovering downloaded GPX files...";
    ++epoch;
}

// Use the same installed-record lookup as the saved list, but retain the full
// verified text for the open detail. No new files or persistent tasks.
bool advanceSavedDetail(Session& s)
{
    if (!s.detail || s.detail->state != CacheDetail::State::Saved) return false;
    auto& job = *s.detail;
    const auto finish = [&](const char* error)
    {
        job.saved_read.reset();
        s.workspace_owner.release(&job);
        if (error)
        {
            job.error = error;
            job.state = CacheDetail::State::Failed;
        }
        return true;
    };
    if (s.phase == Phase::Failed || s.needsRecovery()) return finish("Saved details need storage recovery.");
    if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return finish("SD card unavailable. Go back and reopen to retry.");
    if (now(nullptr).monotonic_ms - job.started >= 120000) return finish("Saved details timed out. Go back and reopen to retry.");
    if (s.phase != Phase::Ready) return false;
    if (!job.saved_read)
    {
        if (downloadActive() || publicationActive() || draftSaveActive()) return false;
        if (!s.workspace_owner.acquire(&job)) return false;
        job.saved_read.reset(::platform::memory::createPsram<SdIndexedSavedCache>(s.volume));
        if (!job.saved_read || !job.saved_read->begin(s.root, {job.id.bytes.data(), job.id.bytes.size()}, true, s.frame, kFrameCapacity))
            return finish("Insufficient memory to read saved details.");
    }
    const auto result = job.saved_read->step();
    if (result == IndexScanStep::Working) return true;
    gc::storage::SavedCacheRecord saved;
    gc::ByteView signed_cache;
    if (result != IndexScanStep::Item || !job.saved_read->result(saved, signed_cache) ||
        saved.hash != job.hash.bytes || !job.verify(signed_cache, s.crypto))
        return finish("Saved details could not be verified.");
    return finish(nullptr);
}

enum class PublicationRestore : uint8_t
{
    None,
    Pending,
    Restored,
    Invalid,
    Forbidden
};
PublicationRestore restorePublication(Session& s, const gc::GeocacheId* cache = nullptr,
                                      const gc::RevisionHash* hash = nullptr, const gc::Destination* remote = nullptr,
                                      const std::array<uint8_t, 16>* expected_task = nullptr, const uint8_t* stop_author = nullptr)
{
    const bool background = !cache && !hash && !remote && !expected_task;
    if (background && s.publication_recovery_complete) return PublicationRestore::None;
    gc::storage::PublicationRecoveryFilter filter;
    filter.local = s.local;
    filter.has_cache = cache != nullptr;
    filter.has_hash = hash != nullptr;
    filter.has_remote = remote != nullptr;
    if (cache) filter.cache = *cache;
    if (hash) filter.hash = *hash;
    if (remote) filter.remote = *remote;
    filter.has_operation = expected_task != nullptr;
    filter.include_terminal = stop_author != nullptr;
    if (expected_task) filter.operation = *expected_task;
    gc::storage::PublicationRecoveryView selected;
    const auto result = s.store->readPublicationRecovery(filter, selected);
    s.publication_restore_pending = result == DraftReadResult::Pending;
    s.publication_restore_background = background;
    if (result == DraftReadResult::Pending || result == DraftReadResult::Busy) return PublicationRestore::Pending;
    if (result == DraftReadResult::Unavailable && !s.needsRecovery())
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    // All exits after Ready release the borrowed request/result frame. Resume
    // copies its small identity/state fields and never retains the payload.
    struct ReadLease
    {
        PublicationStore& store;
        ~ReadLease() { store.releaseDraftRead(); }
    } lease{*s.store};
    if (result == DraftReadResult::NotFound)
    {
        if (background) s.publication_recovery_complete = true;
        return PublicationRestore::None;
    }
    if (result != DraftReadResult::Ready || !selected.request.data || selected.request.size > gc::kMaxApplicationBytes)
        return PublicationRestore::Invalid;
    gc::RequestId request;
    std::memcpy(request.bytes.data(), selected.key.data() + 32, 16);
    if (expected_task && (selected.task != *expected_task || request.bytes != *expected_task)) return PublicationRestore::Invalid;
    auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
    if (!job)
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    std::memcpy(job->remote.bytes.data(), selected.key.data() + 16, 16);
    job->cache = selected.cache;
    job->port.reset(::platform::memory::createPsram<SdPublishPort>(*s.store, s.crypto, s.local, selected.cache, selected.hash, selected.task, selected.created));
    if (job->port) job->attempt.reset(::platform::memory::createPsram<gc::PublishAttempt>(*job->port, s.crypto));
    if (!job->attempt)
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    if (!job->port->attachRestoredRequest(job->remote, request)) return PublicationRestore::Invalid;
    // The read lease already owns this disjoint verification arena. Resume
    // needs record verification space, not a second complete request buffer.
    auto* scratch = s.verification;
    if (!scratch)
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    const bool restored = job->attempt->resume(job->remote, request, selected.request, scratch, kVerificationCapacity,
                                               selected.cache, selected.hash, selected.response);
    if (!restored) return PublicationRestore::Invalid;
    // resume verified this exact immutable request. Borrow its author only
    // while the existing read lease owns the response frame.
    gc::protocol::PublishRequestView published;
    if (!gc::protocol::decodePublishRequest(selected.request, request, published)) return PublicationRestore::Invalid;
    gc::protocol::CmpReader signed_reader(published.signed_cache);
    size_t fields = 0;
    gc::ByteView encoded, signature;
    gc::RecordView original;
    if (!signed_reader.array(fields, 2) || fields != 2 || !signed_reader.binary(encoded, gc::kMaxRecordBytes) ||
        !signed_reader.binary(signature, 64) || signature.size != 64 || !signed_reader.finished() ||
        !gc::protocol::decodeGeocacheRecord(encoded, original)) return PublicationRestore::Invalid;
    if (stop_author && std::memcmp(original.author_public_key.data, stop_author, 64)) return PublicationRestore::Forbidden;
    std::memcpy(job->author.data(), original.author_public_key.data, job->author.size());
    job->started = now(nullptr).monotonic_ms;
    job->wait_ms = 2 * gc::QueryClient::kReplyTimeoutMs + 5000;
    s.publication = std::move(job);
    s.draft_save.reset();
    ++epoch;
    return PublicationRestore::Restored;
}

void advanceDraftPublication(Session& s)
{
    auto& job = *s.publication;
    using Stage = Session::Publication::DraftStage;
    const auto stop = [&](const char* reason)
    {
        job.error = reason;
        Serial.printf("[Geocaching][Archive] failed cache_prefix=%02x%02x%02x%02x reason=%s\n", job.cache.bytes[0], job.cache.bytes[1], job.cache.bytes[2], job.cache.bytes[3], reason);
        job.archive_read.reset();
        s.workspace_owner.release(&job);
        job.draft_stage = Stage::None;
        job.issue.reset();
        job.author_port.reset();
        heap_caps_free(job.bytes);
        job.bytes = nullptr;
        heap_caps_free(job.unsigned_bytes);
        job.unsigned_bytes = nullptr;
        heap_caps_free(job.draft_bytes);
        job.draft_bytes = nullptr;
        s.store->releaseDraftRead();
        ++epoch;
    };
    if (job.draft_stage == Stage::ArchiveLookup)
    {
        if (!s.workspace_owner.acquire(&job)) return;
        std::array<uint8_t, 36> key{};
        std::memcpy(key.data(), job.cache.bytes.data(), 32);
        for (unsigned i = 0; i < 4; ++i) key[32 + i] = static_cast<uint8_t>(job.expected_revision >> ((3 - i) * 8));
        if (!job.archive_read)
        {
            job.archive_read.reset(::platform::memory::createPsram<SdIndexGet>(s.volume));
            if (!job.archive_read || !job.archive_read->begin(s.root, 3, {key.data(), key.size()}, s.frame, kFrameCapacity))
            {
                stop("Cannot read archive reservation");
                return;
            }
        }
        const auto status = job.archive_read->step();
        if (status == IndexGetStep::Working) return;
        if (status == IndexGetStep::Ready)
        {
            gc::storage::AuthorIssuedView prior;
            gc::RecordView record;
            gc::RevisionHash hash;
            gc::GeocacheId cache;
            size_t size = 0;
            if (!gc::storage::decodeAuthorIssued({key.data(), key.size()}, job.archive_read->value(), prior) || !prior.issued_at.has_utc ||
                !gc::protocol::decodeGeocacheRecord({job.unsigned_bytes, job.unsigned_size}, record))
            {
                stop("Invalid archive reservation");
                return;
            }
            record.updated_at = prior.issued_at.utc_seconds;
            if (!gc::protocol::encodeGeocacheRecord(record, job.bytes, job.size, size))
            {
                stop("Cannot restore archive version");
                return;
            }
            std::memcpy(job.unsigned_bytes, job.bytes, size);
            job.unsigned_size = size;
            if (gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, size}, s.crypto, job.bytes, job.size, cache, hash) != gc::protocol::VerificationResult::Valid ||
                std::memcmp(hash.bytes.data(), prior.revision_hash.data, 32))
            {
                stop("Version already used; reopen latest cache");
                return;
            }
            job.issued = prior.issued_at;
        }
        else if (status != IndexGetStep::NotFound)
        {
            stop("Archive reservation read failed");
            return;
        }
        job.archive_read.reset();
        s.workspace_owner.release(&job);
        job.draft_stage = Stage::Archive;
        return;
    }
    if (job.draft_stage == Stage::Binding)
    {
        const auto result = s.store->stepCommit();
        if (result == JournalWriteResult::InProgress || result == JournalWriteResult::Busy) return;
        if (result != JournalWriteResult::Verified)
        {
            stop("Author binding could not be saved");
            return;
        }
        heap_caps_free(job.unsigned_bytes);
        job.unsigned_bytes = nullptr;
        job.draft_stage = Stage::Encode;
        return;
    }
    if (job.draft_stage == Stage::Sign)
    {
        job.issue->advance();
        if (job.issue->phase() == gc::AuthorIssuePhase::Failed)
        {
            stop("Version reservation or signing failed");
            return;
        }
        if (job.issue->phase() != gc::AuthorIssuePhase::Signed) return;
        job.size = job.issue->signedRecord().size;
        job.issue.reset();
        job.author_port.reset();
        heap_caps_free(job.unsigned_bytes);
        job.unsigned_bytes = nullptr;
        job.draft_stage = Stage::None;
        ++epoch;
        return;
    }
    if (job.draft_stage == Stage::ArchiveSaving)
    {
        const auto result = s.store->stepCommit();
        if (result == JournalWriteResult::Busy || result == JournalWriteResult::InProgress) return;
        if (result != JournalWriteResult::Verified)
        {
            stop("Local archive could not be saved");
            return;
        }
        heap_caps_free(job.draft_bytes);
        job.draft_bytes = nullptr;
        ++s.local_map_revision;
        job.draft_stage = Stage::Archive;
        job.draft_generation = UINT64_MAX;
        return;
    }
    if (job.draft_stage == Stage::Archive)
    {
        if (job.draft_generation != UINT64_MAX)
        {
            gc::RecordView record;
            gc::ByteView value;
            if (!gc::protocol::decodeGeocacheRecord({job.unsigned_bytes, job.unsigned_size}, record))
            {
                stop("Invalid archive record");
                return;
            }
            const auto read = s.store->readDraft(record.creation_nonce, value);
            if (read == DraftReadResult::Pending || read == DraftReadResult::Busy) return;
            if (read == DraftReadResult::Ready)
            {
                gc::storage::DraftView draft;
                if (!gc::storage::decodeDraft(record.creation_nonce, value, draft) || draft.generation == UINT64_MAX ||
                    draft.author.size != 64 || std::memcmp(draft.author.data, job.author.data(), 64))
                {
                    stop("Local author mismatch");
                    return;
                }
                const auto expected = draft.generation;
                draft.state = 2;
                ++draft.generation;
                job.draft_size = value.size + 32;
                job.draft_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.archive-draft", job.draft_size, false));
                if (!job.draft_bytes || !gc::storage::encodeDraft(record.creation_nonce, draft, job.draft_bytes, job.draft_size, job.draft_size))
                {
                    stop("Cannot retain local archive");
                    return;
                }
                s.store->releaseDraftRead();
                const auto saved = s.store->saveDraft(record.creation_nonce, {job.draft_bytes, job.draft_size}, expected);
                if (saved == JournalWriteResult::Busy)
                {
                    heap_caps_free(job.draft_bytes);
                    job.draft_bytes = nullptr;
                    return;
                }
                if (saved != JournalWriteResult::InProgress && saved != JournalWriteResult::Verified)
                {
                    stop("Local archive save failed");
                    return;
                }
                job.draft_stage = Stage::ArchiveSaving;
                if (saved == JournalWriteResult::Verified)
                {
                    heap_caps_free(job.draft_bytes);
                    job.draft_bytes = nullptr;
                    job.draft_generation = UINT64_MAX;
                    job.draft_stage = Stage::Archive;
                    ++s.local_map_revision;
                }
                return;
            }
            s.store->releaseDraftRead();
            if (read != DraftReadResult::NotFound)
            {
                stop("Cannot read local archive ownership");
                return;
            }
            job.draft_generation = UINT64_MAX;
        }
        job.author_port.reset(::platform::memory::createPsram<DeviceAuthorIssuePort>(*router, *s.store, s.crypto, job.issued));
        if (job.author_port) job.issue.reset(::platform::memory::createPsram<gc::AuthorIssue>(*job.author_port));
        if (!job.issue || !job.issue->begin({job.unsigned_bytes, job.unsigned_size}, job.bytes, job.size))
            stop("Cannot prepare archive signing");
        else job.draft_stage = Stage::Sign;
        return;
    }
    gc::ByteView value;
    gc::storage::DraftView draft;
    const gc::ByteView key{job.draft_id.data(), job.draft_id.size()};
    if (!job.draft_bytes)
    {
        const auto result = s.store->readDraft(key, value);
        if (result == DraftReadResult::Pending || result == DraftReadResult::Busy) return;
        if (result != DraftReadResult::Ready)
        {
            stop("Draft could not be read");
            return;
        }
        // The store lends its shared frame only until releaseDraftRead. Keep
        // just this draft while encoding, then release it before reservation.
        job.draft_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.publish-draft", value.size, false));
        if (!job.draft_bytes)
        {
            stop("Insufficient draft workspace");
            return;
        }
        job.draft_size = value.size;
        std::memcpy(job.draft_bytes, value.data, value.size);
        s.store->releaseDraftRead();
    }
    value = {job.draft_bytes, job.draft_size};
    if (!gc::storage::decodeDraft(key, value, draft) || draft.generation != job.draft_generation)
    {
        stop("Draft changed; review before publishing");
        return;
    }
    if (job.draft_stage == Stage::Bind)
    {
        if (draft.author.size)
        {
            if (draft.author.size != job.author.size() || std::memcmp(draft.author.data, job.author.data(), job.author.size()))
                stop("Draft belongs to another author");
            else
                job.draft_stage = Stage::Encode;
            return;
        }
        if (draft.generation == UINT64_MAX)
        {
            stop("Draft generation limit reached");
            return;
        }
        job.unsigned_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.author", value.size + 80, false));
        if (!job.unsigned_bytes)
        {
            stop("Insufficient author workspace");
            return;
        }
        draft.author = {job.author.data(), job.author.size()};
        ++draft.generation;
        if (!gc::storage::encodeDraft(key, draft, job.unsigned_bytes, value.size + 80, job.unsigned_size))
        {
            stop("Cannot encode author binding");
            return;
        }
        const auto result = s.store->saveDraft(key, {job.unsigned_bytes, job.unsigned_size}, job.draft_generation);
        if (result == JournalWriteResult::Busy)
        {
            heap_caps_free(job.unsigned_bytes);
            job.unsigned_bytes = nullptr;
            return;
        }
        if (result != JournalWriteResult::InProgress && result != JournalWriteResult::Verified)
        {
            stop("Author binding rejected");
            return;
        }
        ++job.draft_generation;
        heap_caps_free(job.draft_bytes);
        job.draft_bytes = nullptr;
        if (result == JournalWriteResult::Verified)
        {
            heap_caps_free(job.unsigned_bytes);
            job.unsigned_bytes = nullptr;
        }
        job.draft_stage = result == JournalWriteResult::InProgress ? Stage::Binding : Stage::Encode;
        return;
    }
    if (draft.author.size != 64 || std::memcmp(draft.author.data, job.author.data(), 64) || !draft.has_coordinates)
    {
        stop("Draft is not ready for first publication");
        return;
    }
    gc::RecordView record;
    record.author_public_key = {job.author.data(), job.author.size()};
    // draft_id is already a durable CSPRNG nonce. Reuse it for this cache's
    // creation nonce so reopening or retrying never creates a different ID.
    record.creation_nonce = key;
    record.revision = 1;
    record.state = static_cast<gc::CacheState>(draft.state);
    record.latitude_e7 = draft.latitude_e7;
    record.longitude_e7 = draft.longitude_e7;
    record.name = draft.name;
    record.description = draft.description;
    record.hint = draft.hint;
    record.difficulty_x2 = draft.difficulty_x2;
    record.terrain_x2 = draft.terrain_x2;
    record.container_size = static_cast<gc::ContainerSize>(draft.container_size);
    record.created_at = record.updated_at = job.issued.utc_seconds;
    const size_t capacity = draft.name.size() + draft.description.size() + draft.hint.size() + 200;
    gc::RevisionHash hash;
    if (!job.unsigned_bytes)
    {
        job.unsigned_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.record", capacity, false));
        job.size = capacity + 70;
        job.bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.sign", 2 * job.size + 26, false));
        if (!job.unsigned_bytes || !job.bytes || !gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size) ||
            gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, job.unsigned_size}, s.crypto, job.bytes, job.size, job.cache, hash) != gc::protocol::VerificationResult::Valid)
        {
            stop("Cannot prepare signed record");
            return;
        }
    }
    const auto history_result = s.store->readPublicationHistory(key, job.draft_generation, job.cache,
                                                                {job.author.data(), job.author.size()}, job.history);
    if (history_result == DraftReadResult::Pending || history_result == DraftReadResult::Busy) return;
    if (history_result != DraftReadResult::Ready)
    {
        stop("Author history could not be read");
        return;
    }
    const auto& history = job.history;
    if (history.latest_revision)
    {
        record.created_at = history.created_at;
        record.revision = history.latest_revision;
        record.updated_at = history.latest_time.utc_seconds;
        if (record.revision > 1)
            record.previous_hash = {history.previous_hash.bytes.data(), history.previous_hash.bytes.size()};
        if (!gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size) ||
            gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, job.unsigned_size}, s.crypto, job.bytes, job.size, job.cache, hash) != gc::protocol::VerificationResult::Valid)
        {
            stop("Cannot reconstruct latest version");
            return;
        }
        if (hash.bytes == history.latest_hash.bytes) job.issued = history.latest_time;
        else
        {
            if (history.confirmed_revision != history.latest_revision)
            {
                stop("Previous version unconfirmed; resolve it first");
                return;
            }
            if (history.latest_revision == UINT32_MAX || job.issued.utc_seconds < history.latest_time.utc_seconds)
            {
                stop("Version limit or clock regression");
                return;
            }
            record.revision = history.latest_revision + 1;
            record.previous_hash = {history.latest_hash.bytes.data(), history.latest_hash.bytes.size()};
            record.updated_at = job.issued.utc_seconds;
            if (!gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size))
            {
                stop("Cannot encode successor version");
                return;
            }
        }
    }
    if (job.expected_revision && record.revision != job.expected_revision)
    {
        stop("Version changed; review publication again");
        return;
    }
    heap_caps_free(job.draft_bytes);
    job.draft_bytes = nullptr;
    job.author_port.reset(::platform::memory::createPsram<DeviceAuthorIssuePort>(*router, *s.store, s.crypto, job.issued, key, job.draft_generation));
    if (job.author_port) job.issue.reset(::platform::memory::createPsram<gc::AuthorIssue>(*job.author_port));
    if (!job.issue || !job.issue->begin({job.unsigned_bytes, job.unsigned_size}, job.bytes, job.size))
    {
        stop("Cannot start record signing");
        return;
    }
    job.draft_stage = Stage::Sign;
}

bool publicationDraftReady(const std::array<uint8_t, 16>& id, uint64_t generation, std::array<uint8_t, 64>& author,
                           uint32_t* from = nullptr, uint32_t* to = nullptr, const gc::Destination* selected = nullptr)
{
    if (!session) return false;
    network_requested.store(true);
    if (!draftCatalogReady(*session))
    {
        session->draft_catalog_wanted = true;
        next_step.store(0);
        return false;
    }
    const gc::storage::DraftCatalogEntry* draft = nullptr;
    for (size_t i = 0; i < session->draft_catalog->page.count; ++i)
    {
        const auto& candidate = session->draft_catalog->page.rows[i];
        if (candidate.id == id && candidate.generation == generation)
        {
            draft = &candidate;
            break;
        }
    }
    if (!draft) return false;
    gc::Destination remote;
    bool has_remote = false;
    if (selected && session->client)
    {
        for (size_t i = 0; i < session->client->directoryCount(); ++i)
        {
            const auto* entry = session->client->directory(i);
            if (entry && entry->delivery.bytes == selected->bytes && entry->capabilities_verified)
            {
                has_remote = true;
                break;
            }
        }
    }
    else if (!selected && session->port) has_remote = session->port->pageSource(remote);
    const auto utc = std::time(nullptr);
    const bool ready = session && session->phase == Phase::Ready && session->port && session->store &&
                       !session->needsRecovery() && !session->store->commitPending() &&
                       !downloadActive() && !publicationActive() && !draftSaveActive() && utc >= 946684800 &&
                       static_cast<uint64_t>(utc) <= 253402300799ULL && has_remote &&
                       draft->has_coordinates &&
                       gc::protocol::validRecordText(draft->name.data(), false, true) && router->getGeocachingAuthorKey(author.data()) &&
                       (!draft->has_author || draft->author == author);
    if (!ready) return false;
    const auto& history = draft->publication;
    if (history.local_changes && (history.latest_revision == UINT32_MAX || history.confirmed_revision != history.latest_revision)) return false;
    if (from) *from = history.latest_revision;
    if (to) *to = history.latest_revision ? history.latest_revision + (history.local_changes ? 1 : 0) : 1;
    return true;
}

class Facade final : public ::ui::geocaching::Source
{
  public:
    bool localMapRevision(uint64_t& out) override
    {
        Guard guard;
        if (!guard.locked || !session) return false;
        out = session->local_map_revision;
        return true;
    }
    void activate(bool open) override
    {
        if (open && !ensureMutex()) return;
        if (open && !wanted.load())
        {
            network_requested.store(false);
            local_storage_requested.store(false);
        }
        if (open) wanted.fetch_or(1);
        else wanted.fetch_and(static_cast<uint8_t>(~1));
        if (!open) cancel_detail.store(true);
        next_step.store(0);
    }
    void snapshot(::ui::geocaching::Section section, ::ui::geocaching::Snapshot& out) override
    {
        out = {};
        if (section == ::ui::geocaching::Section::Discover) network_requested.store(true);
        else local_storage_requested.store(true);
        Guard guard;
        if (!guard.locked)
        {
            out.busy = true;
            std::snprintf(out.status.data(), out.status.size(), "%s", mutex.load() ? "Updating..." : "Geocaching lock memory unavailable");
            return;
        }
        if (session && section != ::ui::geocaching::Section::Discover)
        {
            session->storage_requested = true;
            if (section == ::ui::geocaching::Section::Published) session->draft_catalog_wanted = true;
        }
        if (section == ::ui::geocaching::Section::Published && session && (session->phase == Phase::Ready || session->phase == Phase::ResumeDownloads) && session->store && !session->needsRecovery())
        {
            session->draft_catalog_wanted = true;
            const auto* catalog = session->draft_catalog.get();
            out.count = catalog ? catalog->total : 0;
            out.generation = catalog ? catalog->generation : 0;
            out.ready = draftMetadataReady(*session);
            out.can_create = session->phase == Phase::Ready && !session->store->commitPending() && !downloadActive() && !publicationActive() && !draftSaveActive() &&
                             storage::sd_card_ready() && !storage::sd_external_block_owner_active();
            std::snprintf(out.status.data(), out.status.size(), "%s", catalog && catalog->failed ? "Draft list could not be read" : !draftMetadataReady(*session) ? "Loading local caches..."
                                                                                                                                : out.count                       ? "Drafts and saved publication status"
                                                                                                                                                                  : "No local drafts");
            if (!draftCatalogReady(*session) && (!catalog || !catalog->failed)) next_step.store(0);
        }
        else if (section == ::ui::geocaching::Section::Downloaded && session && (session->phase == Phase::Ready || session->phase == Phase::ResumeDownloads) && session->saved && !session->needsRecovery())
            session->saved->snapshot(out);
        else if (section == ::ui::geocaching::Section::Discover && session && session->source) session->source->snapshot(section, out);
        else
        {
            std::snprintf(out.status.data(), out.status.size(), "%s", session ? (section == ::ui::geocaching::Section::Discover ? session->browse_status : session->status) : "Starting Geocaching...");
            out.can_refresh = session && session->phase == Phase::Failed;
        }
        if (session && session->recovery_attention && section == ::ui::geocaching::Section::Downloaded)
            std::snprintf(out.status.data(), out.status.size(), "Some saved GPX files or history need attention");
        if (session && session->notice && section != ::ui::geocaching::Section::Discover) std::snprintf(out.status.data(), out.status.size(), "%s", session->notice);
        if (downloadActive())
        {
            out.can_refresh = false;
            out.has_more = false;
            const auto phase = session->download ? session->download->phase() : gc::DownloadPhase::Submitting;
            std::snprintf(out.status.data(), out.status.size(), "%s", session->download_start ? "Preparing download..." : phase == gc::DownloadPhase::Waiting  ? "Downloading cache..."
                                                                                                                      : phase == gc::DownloadPhase::Cancelling ? "Cancelling download..."
                                                                                                                                                               : "Saving and verifying GPX...");
        }
        else if (session && session->download_start_error)
            std::snprintf(out.status.data(), out.status.size(), "%s", session->download_start_error);
        else if (session && session->download && session->download->phase() == gc::DownloadPhase::Failed)
            std::snprintf(out.status.data(), out.status.size(), "Download could not be saved");
        else if (session && session->download_port && session->download_port->historyPending())
            std::snprintf(out.status.data(), out.status.size(), "Saved; history retention pending");
        if (session && session->publication && (publicationActive() || section == ::ui::geocaching::Section::Published))
        {
            const auto& job = *session->publication;
            const auto phase = job.attempt ? job.attempt->phase() : gc::PublishAttemptPhase::Failed;
            const char* status = job.bytes ? "Preparing publication..." : phase == gc::PublishAttemptPhase::Confirmed                                                ? "Directory accepted publication"
                                                                      : phase == gc::PublishAttemptPhase::Waiting                                                    ? "Waiting for directory confirmation..."
                                                                      : phase == gc::PublishAttemptPhase::Submitting || phase == gc::PublishAttemptPhase::Committing ? "Saving publication state..."
                                                                      : phase == gc::PublishAttemptPhase::Cancelling                                                 ? "Stopping publication retries..."
                                                                      : phase == gc::PublishAttemptPhase::Cancelled                                                  ? "Stopped; publication result unconfirmed"
                                                                                                                                                                     : "Publication could not complete";
            if (job.error) status = job.error;
            else if (job.draft_stage != Session::Publication::DraftStage::None) status = "Preparing publication...";
            std::snprintf(out.status.data(), out.status.size(), "%s", status);
            if (publicationActive())
            {
                out.can_refresh = false;
                out.has_more = false;
            }
        }
        if (session && session->draft_save && (draftSaveActive() || section == ::ui::geocaching::Section::Published))
        {
            const auto& job = *session->draft_save;
            std::snprintf(out.status.data(), out.status.size(), "%s", job.erase   ? (!job.done ? "Deleting local cache..." : job.saved ? "Local cache deleted"
                                                                                                                                       : "Delete failed; cache retained")
                                                                      : !job.done ? "Saving local draft..."
                                                                      : job.saved ? "Draft saved locally"
                                                                                  : "Draft save failed; changes not saved");
            if (!job.done)
            {
                out.can_refresh = false;
                out.has_more = false;
            }
        }
        if (session && session->workspace_unavailable && section != ::ui::geocaching::Section::Discover)
            std::snprintf(out.status.data(), out.status.size(), "Waiting for storage workspace...");
        if (session && session->phase == Phase::Failed && section != ::ui::geocaching::Section::Discover)
        {
            std::snprintf(out.status.data(), out.status.size(), "%s", session->status);
            out.can_refresh = true;
            out.can_create = out.has_more = false;
        }
        // List ownership and its own content version determine row validity.
        // Transport and operation progress must not invalidate unchanged rows.
        const auto scope = section == ::ui::geocaching::Section::Published && session    ? session->draft_catalog_epoch
                           : section == ::ui::geocaching::Section::Downloaded && session ? session->saved_catalog_epoch
                           : section == ::ui::geocaching::Section::Discover && session   ? discoverScope(*session)
                                                                                         : epoch;
        out.generation ^= scope << 32;
    }
    void requestWindow(::ui::geocaching::Section section, size_t offset, size_t count) override
    {
        if (section == ::ui::geocaching::Section::Discover) network_requested.store(true);
        else local_storage_requested.store(true);
        Guard guard;
        if (!guard.locked || !session) return;
        session->map_metadata_only = false;
        if (section != ::ui::geocaching::Section::Discover) session->storage_requested = true;
        if (section == ::ui::geocaching::Section::Downloaded && count && count <= 4)
        {
            session->saved_offset = offset;
            session->saved_count = count;
        }
        session->draft_catalog_wanted = section == ::ui::geocaching::Section::Published;
        if (session->saved && count && count <= 4)
        {
            auto& saved = *session->saved;
            const auto before = saved.generation();
            if (section == ::ui::geocaching::Section::Downloaded) saved.requestWindow(offset, count);
            else if (section == ::ui::geocaching::Section::Discover && session->port)
            {
                gc::protocol::QueryPageView page;
                const auto available = session->port->page(page) && offset < page.count ? std::min(count, page.count - offset) : 0;
                if (saved.requestPreview(session->port->generation(), offset, available))
                    for (size_t i = 0; i < available; ++i)
                    {
                        gc::protocol::SummaryView row;
                        if (session->port->summary(offset + i, row)) saved.previewRow(i, row.id.bytes, row.hash.bytes);
                    }
            }
            if (before != saved.generation())
            {
                ++epoch;
                next_step.store(0);
            }
        }
        if (!session->draft_catalog_wanted || !count || count > 4) return;
        if (session->draft_catalog && session->draft_catalog->requested_offset != offset)
        {
            auto& catalog = *session->draft_catalog;
            catalog.requested_offset = offset;
            catalog.failed = catalog.ready = false;
            if (catalog.reading) session->draft_io_reset = true;
            ++catalog.generation;
        }
        next_step.store(0);
    }
    void requestMapWindow(::ui::geocaching::Section section, size_t offset, size_t count) override
    {
        requestWindow(section, offset, count);
        Guard guard;
        if (guard.locked && session) session->map_metadata_only = true;
    }
    bool item(::ui::geocaching::Section section, size_t index, uint64_t generation, ::ui::geocaching::Item& out) override
    {
        Guard guard;
        if (guard.locked && section == ::ui::geocaching::Section::Published && session && session->store && !session->needsRecovery())
        {
            out = {};
            if (!draftMetadataReady(*session)) return false;
            const auto& catalog = *session->draft_catalog;
            if ((generation ^ (session->draft_catalog_epoch << 32)) != catalog.generation || index < catalog.page.offset || index - catalog.page.offset >= catalog.page.count) return false;
            const auto& draft = catalog.page.rows[index - catalog.page.offset];
            out.is_draft = true;
            out.state = draft.state;
            out.has_coordinates = draft.has_coordinates;
            out.edit_generation = draft.generation;
            std::memcpy(out.id.data(), draft.id.data(), 16);
            if (!draft.name[0]) std::snprintf(out.name.data(), out.name.size(), "Untitled draft");
            else out.name = draft.name;
            out.latitude_e7 = draft.latitude_e7;
            out.longitude_e7 = draft.longitude_e7;
            if (!draftCatalogReady(*session))
            {
                std::snprintf(out.detail.data(), out.detail.size(), "Saved locally\n%s\nChecking publication status...", draft.has_coordinates ? "Location set" : "Location not set");
                return true;
            }
            std::snprintf(out.detail.data(), out.detail.size(), "Local draft - not published\n%s\n%s", draft.has_coordinates ? "Location set" : "Location not set", draft.has_author ? "Author selected" : "Author not selected");
            const auto& publication = draft.publication;
            out.publication_revision = publication.latest_revision;
            out.publication_confirmed = publication.latest_revision && publication.confirmed_revision == publication.latest_revision && !publication.local_changes;
            if (publication.latest_revision)
            {
                if (draft.state == 2)
                {
                    std::snprintf(out.detail.data(), out.detail.size(), "%s\n%s",
                                  out.publication_confirmed && !publication.local_changes ? "Archived - confirmed by directory" : "Archive not confirmed",
                                  publication.local_changes ? "Local archive saved; publish to withdraw the public cache" : publication.pending ? "Awaiting directory confirmation"
                                                                                                                                                : "Retained locally; retry publication if needed");
                    return true;
                }
                std::snprintf(out.detail.data(), out.detail.size(), publication.confirmed_revision ? "v%lu %s%s\nLast directory-confirmed version: %lu" : "v%lu %s%s\nNo directory confirmation recorded",
                              static_cast<unsigned long>(publication.latest_revision), out.publication_confirmed ? "accepted by directory" : publication.pending ? "awaiting confirmation"
                                                                                                                                                                 : "stopped; result unconfirmed",
                              publication.local_changes ? "; local edits" : "",
                              static_cast<unsigned long>(publication.confirmed_revision));
            }
            return true;
        }
        if (guard.locked && section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->needsRecovery())
            return session->saved->item(index, generation ^ (session->saved_catalog_epoch << 32), out);
        if (!guard.locked || !session || !session->source || !session->source->item(section, index, generation ^ (discoverScope(*session) << 32), out)) return false;
        out.downloaded = session->saved && session->saved->contains(out.id, out.revision_hash);
        out.can_download = !out.downloaded && !downloadActive() && !publicationActive() && !draftSaveActive() && (!session->store || !session->store->commitPending()) &&
                           !session->needsRecovery() && session->phase != Phase::Failed && storage::sd_card_ready() && !storage::sd_external_block_owner_active() &&
                           (session->client->phase() == gc::QueryClientPhase::PageReady || session->client->phase() == gc::QueryClientPhase::Failed);
        if (out.downloaded)
        {
            if (auto* suffix = std::strstr(out.detail.data(), "Directory preview - not yet downloaded"))
                std::snprintf(suffix, out.detail.size() - static_cast<size_t>(suffix - out.detail.data()), "Saved GPX on SD card");
        }
        return true;
    }
    void refresh(::ui::geocaching::Section section) override
    {
        Guard guard;
        if (!guard.locked || downloadActive() || publicationActive() || draftSaveActive()) return;
        if (section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->needsRecovery()) session->saved->reset();
        else if (section != ::ui::geocaching::Section::Discover && session && session->phase == Phase::Failed) restart.store(true);
        else if (session && session->source) session->source->refresh(section);
        next_step.store(0);
    }
    bool loadMore() override
    {
        Guard guard;
        if (!guard.locked || downloadActive() || publicationActive() || draftSaveActive() || !session || !session->source) return false;
        const bool begun = session->source->loadMore();
        if (begun) next_step.store(0);
        return begun;
    }
    void open(const ::ui::geocaching::Item& item, uint64_t) override
    {
        Guard guard;
        if (!guard.locked || !session || item.is_draft) return;
        auto job = ::platform::memory::PsramPtr<CacheDetail>(::platform::memory::createPsram<CacheDetail>());
        if (!job) return;
        job->id.bytes = item.id;
        job->hash.bytes = item.revision_hash;
        job->started = now(nullptr).monotonic_ms;
        if (item.downloaded)
        {
            job->state = CacheDetail::State::Saved;
            session->storage_requested = true;
        }
        else if (!session->port || !session->port->pageSource(job->remote) ||
                 !randomId(nullptr, job->request.bytes.data()) ||
                 !gc::protocol::encodeGetRequest(job->request, job->id, &job->hash, nullptr, 8192,
                                                 job->request_bytes.data(), job->request_bytes.size(), job->request_size))
            job->state = CacheDetail::State::Failed;
        session->pending_detail = std::move(job);
        cancel_detail.store(false);
        next_step.store(0);
    }
    bool readDetail(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash,
                    void (*sink)(const ::ui::geocaching::DetailView&, void*), void* context) override
    {
        Guard guard;
        if (!guard.locked || !session || !sink) return false;
        const auto* job = session->pending_detail ? session->pending_detail.get() : session->detail.get();
        ::ui::geocaching::DetailView view;
        if (session->publication && session->publication->cache.bytes == id && session->publication->error)
            view.archive_error = session->publication->error;
        if (!job || cancel_detail.load() || !job->matches(id, hash))
        {
            view.status = ::ui::geocaching::DetailStatus::Failed;
            view.error = "Details unavailable. Go back and reopen to retry.";
            sink(view, context);
            return true;
        }
        if (job->state == CacheDetail::State::Ready)
        {
            view.status = ::ui::geocaching::DetailStatus::Ready;
            view.description = job->verified_record.record.description;
            view.hint = job->verified_record.record.hint;
            std::array<uint8_t, 64> author{};
            view.can_archive = job->verified_record.record.state != gc::CacheState::Archived &&
                               router->getGeocachingAuthorKey(author.data()) &&
                               !std::memcmp(author.data(), job->verified_record.record.author_public_key.data, author.size());
        }
        else if (job->state == CacheDetail::State::Failed)
        {
            view.status = ::ui::geocaching::DetailStatus::Failed;
            view.error = job->error;
        }
        sink(view, context);
        return true;
    }
    void closeDetail() override
    {
        cancel_detail.store(true);
        next_step.store(0);
    }
    bool archiveCache(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) override
    {
        Guard guard;
        if (!guard.locked || !session || !session->detail || session->detail->state != CacheDetail::State::Ready ||
            !session->detail->matches(id, hash) || !session->port || session->needsRecovery() ||
            publicationActive() || downloadActive() || draftSaveActive()) return false;
        const auto& verified = session->detail->verified_record;
        auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
        if (!job || verified.record.revision == UINT32_MAX || verified.record.state == gc::CacheState::Archived ||
            !router->getGeocachingAuthorKey(job->author.data()) ||
            std::memcmp(job->author.data(), verified.record.author_public_key.data, job->author.size()) ||
            !session->port->pageSource(job->remote)) return false;
        auto record = verified.record;
        ++record.revision;
        record.previous_hash = {hash.data(), hash.size()};
        record.state = gc::CacheState::Archived;
        record.updated_at = std::max(record.updated_at, static_cast<uint64_t>(std::time(nullptr)));
        job->issued = now(nullptr);
        job->issued.has_utc = true;
        job->issued.utc_seconds = record.updated_at;
        job->cache = verified.id;
        job->expected_revision = record.revision;
        const size_t capacity = record.name.size() + record.description.size() + record.hint.size() + 240;
        job->unsigned_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.archive", capacity, false));
        job->size = capacity + 70;
        job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.sign", 2 * job->size + 26, false));
        if (!job->unsigned_bytes || !job->bytes || !gc::protocol::encodeGeocacheRecord(record, job->unsigned_bytes, capacity, job->unsigned_size)) return false;
        job->draft_stage = Session::Publication::DraftStage::ArchiveLookup;
        session->storage_requested = true;
        session->publication = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftSaveStatus archiveStatus(const std::array<uint8_t, 32>& id) override
    {
        using Status = ::ui::geocaching::DraftSaveStatus;
        Guard guard;
        if (!guard.locked) return Status::Pending;
        if (!session || !session->publication || session->publication->cache.bytes != id) return Status::Failed;
        const auto& job = *session->publication;
        if (job.error) return Status::Failed;
        if (job.draft_stage != Session::Publication::DraftStage::None || job.bytes) return Status::Pending;
        if (!job.attempt) return Status::Failed;
        if (job.attempt->phase() == gc::PublishAttemptPhase::Confirmed) return Status::Saved;
        return publicationActive() ? Status::Pending : Status::Failed;
    }
    bool publicationAuthor(const std::array<uint8_t, 16>& id, uint64_t generation, std::array<uint8_t, 64>& author, uint32_t* from, uint32_t* to) override
    {
        Guard guard;
        return guard.locked && publicationDraftReady(id, generation, author, from, to);
    }
    bool publishDraft(const std::array<uint8_t, 16>& id, uint64_t generation, const std::array<uint8_t, 64>& expected_author, uint32_t expected_revision) override
    {
        Guard guard;
        std::array<uint8_t, 64> author;
        uint32_t revision = 0;
        if (!guard.locked || !publicationDraftReady(id, generation, author, nullptr, &revision) || author != expected_author || revision != expected_revision) return false;
        auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
        if (!job || !session->port->pageSource(job->remote)) return false;
        job->draft_id = id;
        job->draft_generation = generation;
        job->author = author;
        job->expected_revision = expected_revision;
        job->issued = now(nullptr);
        job->issued.has_utc = true;
        job->issued.utc_seconds = static_cast<uint64_t>(std::time(nullptr));
        job->draft_stage = Session::Publication::DraftStage::Bind;
        session->draft_save.reset();
        session->publication = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftReadStatus readDraft(const std::array<uint8_t, 16>& id, void (*sink)(const ::ui::geocaching::DraftInput&, void*), void* context) override
    {
        Guard guard;
        using Status = ::ui::geocaching::DraftReadStatus;
        if (!guard.locked) return Status::Pending;
        if (!sink || !session || !session->store || session->needsRecovery()) return Status::Failed;
        releaseDraftReaders(*session, cancel_draft_read.exchange(0));
        if (!session->draft_read || session->draft_read->id != id)
        {
            if (session->draft_read && (session->draft_read->users & 2)) return Status::Pending;
            session->draft_io_reset = true;
            session->draft_read.reset(::platform::memory::createPsram<Session::DraftRead>());
            if (!session->draft_read) return Status::Failed;
            session->draft_read->id = id;
            session->draft_read->users = 1;
            next_step.store(0);
            return Status::Pending;
        }
        session->draft_read->users |= 1;
        if (session->draft_read->status == Status::Pending) return Status::Pending;
        if (session->draft_read->status == Status::Failed)
        {
            releaseDraftReaders(*session, 1);
            return Status::Failed;
        }
        gc::storage::DraftView draft;
        const auto& result = *session->draft_read;
        if (!gc::storage::decodeDraft({id.data(), id.size()}, {result.bytes, result.size}, draft))
        {
            releaseDraftReaders(*session, 1);
            return Status::Failed;
        }
        ::ui::geocaching::DraftInput out;
        out.id = id;
        out.generation = draft.generation;
        out.name = draft.name;
        out.description = draft.description;
        out.hint = draft.hint;
        out.latitude_e7 = draft.latitude_e7;
        out.longitude_e7 = draft.longitude_e7;
        out.has_coordinates = draft.has_coordinates;
        out.state = draft.state;
        out.difficulty_x2 = draft.difficulty_x2;
        out.terrain_x2 = draft.terrain_x2;
        out.container_size = draft.container_size;
        sink(out, context);
        releaseDraftReaders(*session, 1);
        return Status::Ready;
    }
    void cancelDraftRead(const std::array<uint8_t, 16>& id) override
    {
        Guard guard;
        if (guard.locked)
        {
            if (session && session->draft_read && session->draft_read->id == id)
            {
                releaseDraftReaders(*session, 1);
            }
        }
        else cancel_draft_read.fetch_or(1);
        next_step.store(0);
    }
    bool saveDraft(::ui::geocaching::DraftInput& input) override
    {
        Guard guard;
        if (!guard.locked || !session || !session->store || session->needsRecovery() || session->store->commitPending() ||
            downloadActive() || publicationActive() || draftSaveActive() || session->phase == Phase::ResumeDownloads ||
            input.generation == UINT64_MAX ||
            input.name.size() > 96 || input.description.size() > 2048 || input.hint.size() > 512) return false;
        gc::storage::DraftView draft;
        if (!input.generation && input.id == std::array<uint8_t, 16>{}) esp_fill_random(input.id.data(), input.id.size());
        draft.generation = input.generation + 1;
        draft.name = input.name;
        draft.description = input.description;
        draft.hint = input.hint;
        draft.latitude_e7 = input.latitude_e7;
        draft.longitude_e7 = input.longitude_e7;
        draft.has_coordinates = input.has_coordinates;
        draft.state = input.state;
        draft.difficulty_x2 = input.difficulty_x2;
        draft.terrain_x2 = input.terrain_x2;
        draft.container_size = input.container_size;
        auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
        if (!job) return false;
        const auto capacity = input.name.size() + input.description.size() + input.hint.size() + 160;
        job->capacity = capacity;
        job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.draft", capacity, false));
        if (!job->bytes || !gc::storage::encodeDraft({input.id.data(), input.id.size()}, draft, job->bytes, capacity, job->size)) return false;
        job->id = input.id;
        job->expected = input.generation;
        session->draft_save = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftSaveStatus draftSaveStatus(const std::array<uint8_t, 16>& id, uint64_t generation) override
    {
        using Status = ::ui::geocaching::DraftSaveStatus;
        Guard guard;
        if (!guard.locked) return Status::Pending;
        if (!session || !session->draft_save || session->draft_save->erase_download || session->draft_save->id != id || session->draft_save->expected != generation) return Status::Failed;
        const auto& job = *session->draft_save;
        return !job.done ? Status::Pending : job.saved ? Status::Saved
                                                       : Status::Failed;
    }
    bool deleteDraft(const std::array<uint8_t, 16>& id, uint64_t generation) override
    {
        Guard guard;
        if (!guard.locked || !generation || !session || session->phase != Phase::Ready || !session->store ||
            session->needsRecovery() || session->store->commitPending() || downloadActive() || publicationActive() || draftSaveActive()) return false;
        auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
        if (!job) return false;
        job->id = id;
        job->expected = generation;
        job->erase = true;
        session->draft_save = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    bool removeDownloaded(const ::ui::geocaching::Item& item) override
    {
        Guard guard;
        if (!guard.locked || item.is_draft || !item.downloaded || !session || session->phase != Phase::Ready ||
            session->needsRecovery() || downloadActive() || publicationActive() || draftSaveActive() ||
            (session->store && session->store->commitPending())) return false;
        auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
        if (!job) return false;
        job->erase_download = true;
        job->cache = item.id;
        job->hash = item.revision_hash;
        session->draft_save = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftSaveStatus downloadedRemovalStatus(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) override
    {
        using Status = ::ui::geocaching::DraftSaveStatus;
        Guard guard;
        if (!guard.locked) return Status::Pending;
        if (!session || !session->draft_save || !session->draft_save->erase_download ||
            session->draft_save->cache != id || session->draft_save->hash != hash) return Status::Failed;
        const auto& job = *session->draft_save;
        return !job.done ? Status::Pending : job.saved ? Status::Saved
                                                       : Status::Failed;
    }
    bool download(const ::ui::geocaching::Item& item, uint64_t generation) override
    {
        Guard guard;
        if (!guard.locked || !session || session->phase == Phase::Failed || !session->source || !session->port || !storage::sd_card_ready() || storage::sd_external_block_owner_active() ||
            session->needsRecovery() || downloadActive() || publicationActive() || draftSaveActive() || (session->store && session->store->commitPending())) return false;
        if (session->client->phase() != gc::QueryClientPhase::PageReady && session->client->phase() != gc::QueryClientPhase::Failed) return false;
        if (session->saved && session->saved->contains(item.id, item.revision_hash)) return false;
        ::ui::geocaching::Snapshot snapshot;
        session->source->snapshot(::ui::geocaching::Section::Discover, snapshot);
        if ((generation ^ (discoverScope(*session) << 32)) != snapshot.generation) return false;
        gc::protocol::SummaryView summary;
        bool found = false;
        for (size_t i = 0; i < snapshot.count; ++i)
            if (session->port->summary(i, summary) && summary.id.bytes == item.id && summary.hash.bytes == item.revision_hash)
            {
                found = true;
                break;
            }
        gc::Destination remote;
        if (!found || !session->port->pageSource(remote)) return false;
        auto job = ::platform::memory::PsramPtr<Session::DownloadStart>(::platform::memory::createPsram<Session::DownloadStart>());
        if (!job || summary.name.size() > job->name.size()) return false;
        job->summary = summary;
        std::memcpy(job->name.data(), summary.name.data(), summary.name.size());
        job->summary.name = {job->name.data(), summary.name.size()};
        job->remote = remote;
        auto* detail = session->detail.get();
        if (!session->response && detail && detail->state == CacheDetail::State::Ready && detail->response &&
            detail->matches(item.id, item.revision_hash) && detail->remote.bytes == remote.bytes)
        {
            job->verified.reset(::platform::memory::createPsram<gc::protocol::VerifiedRecordView>());
            if (!job->verified) return false;
            job->request = detail->request;
            job->response_size = detail->response_size;
            job->response = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.download.detail", job->response_size, false));
            if (!job->response) return false;
            std::memcpy(job->response, detail->response, job->response_size);
            // Both owners may outlive the other. Reparse the immutable copy so
            // every borrowed field belongs to the download's response, while
            // retaining the already verified identity/hash without more crypto.
            gc::protocol::GetResponseView parsed;
            if (!gc::protocol::decodeGetResponse({job->response, job->response_size}, job->request, 8192, parsed) || parsed.has_conflict) return false;
            gc::protocol::CmpReader reader(parsed.signed_cache);
            size_t count = 0;
            gc::ByteView encoded;
            if (!reader.array(count, 2) || count != 2 || !reader.binary(encoded, gc::kMaxRecordBytes) ||
                !reader.binary(job->verified->signature, 64) || job->verified->signature.size != 64 || !reader.finished() ||
                !gc::protocol::decodeGeocacheRecord(encoded, job->verified->record)) return false;
            job->verified->id = detail->verified_record.id;
            job->verified->hash = detail->verified_record.hash;
        }
        session->storage_requested = true;
        session->download_start = std::move(job);
        session->download_start_error = nullptr;
        ++epoch;
        next_step.store(0);
        return true;
    }
} facade;

bool advanceOfflineRemoval(Session& s)
{
    if (!s.draft_save || !s.draft_save->erase_download || s.draft_save->done) return false;
    auto& job = *s.draft_save;
    if (!s.workspace_owner.acquire(&job)) return false;
    auto finish = [&](bool saved)
    {
        job.done = true;
        job.saved = saved;
        job.removal.reset();
        s.workspace_owner.release(&job);
        if (saved)
        {
            if (s.saved) s.saved->reset();
            ++s.local_map_revision;
            Serial.printf("[Geocaching][LocalDelete] confirmed cache_prefix=%02x%02x%02x%02x\n", job.cache[0], job.cache[1], job.cache[2], job.cache[3]);
        }
        ++epoch;
        return true;
    };
    if (!job.removal)
    {
        job.removal.reset(::platform::memory::createPsram<SdIndexedRemoveSaved>(s.volume));
        if (!job.removal || !job.removal->begin(s.root, s.root_copy, job.cache, job.hash, s.frame, kFrameCapacity, s.roots[1 - s.root_copy]))
            return finish(false);
    }
    const auto result = job.removal->step();
    if (result == IndexedCommitStep::Working) return true;
    if (result == IndexedCommitStep::Verified)
    {
        gc::storage::IndexRootView next;
        if (!job.removal->committed(next)) return finish(false);
        s.root = next;
        s.root_copy = 1 - s.root_copy;
        return finish(true);
    }
    if (result == IndexedCommitStep::IoError || result == IndexedCommitStep::VolumeChanged || result == IndexedCommitStep::RecoveryRequired)
        s.checkpoint_recovery_required = true;
    return finish(false);
}

DispatchResult dispatchForeground(Session& s)
{
    std::array<uint8_t, 48> key{};
    const bool publication = s.publication && s.publication->port && s.publication->attempt &&
                             s.publication->attempt->phase() == gc::PublishAttemptPhase::Waiting && s.publication->port->dispatchKey(key);
    const bool download = !publication && s.download && s.download_port &&
                          s.download->phase() == gc::DownloadPhase::Waiting && s.download_port->dispatchKey(key);
    const auto result = s.dispatcher->dispatchOne(now(nullptr), publication || download ? gc::ByteView{key.data(), key.size()} : gc::ByteView{});
    if (publication && result.status == DispatchStatus::Submitted)
    {
        s.publication->submitted = true;
        s.publication->started = now(nullptr).monotonic_ms;
        reportPublication("submitted", *s.publication->attempt);
    }
    return result;
}

bool closeSession()
{
    if (!session) return true;
    // Closing still persists query cancellation and may finish an active
    // write. Preserve its inputs while USB owns the volume, without touching
    // storage or turning temporary ownership into a recovery fault.
    if (session->storage_requested && storage::sd_external_block_owner_active())
    {
        next_step.store(millis() + 1000);
        return false;
    }
    if (session->checkpoint)
    {
        // Closing may interrupt any maintenance phase. Both checkpoint and
        // journal recovery baselines remain intact; abandon borrowed cursors
        // and force recovery on the next activation instead of delaying exit.
        session->checkpoint_recovery_required = true;
        session->workspace_owner.release(session->checkpoint.get());
        session->checkpoint.reset();
    }
    // A restart may arrive during a write. Preserve its live inputs until the
    // current operation terminates, then let normal recovery reconcile it.
    if (!session->needsRecovery() && session->workspace_owner.holder())
    {
        WorkspaceSlice workspace_slice{*session};
        if (session->draft_save && session->draft_save->erase_download && session->workspace_owner.heldBy(session->draft_save.get()))
        {
            advanceOfflineRemoval(*session);
            return false;
        }
        if (session->dispatch_store && session->workspace_owner.heldBy(session->dispatch_store.get()))
        {
            dispatchForeground(*session);
            return false;
        }
        if (session->receipts && session->workspace_owner.heldBy(session->receipts.get()))
        {
            session->receipts->step();
            return false;
        }
        if (session->store && session->store->commitPending())
        {
            session->store->stepCommit();
            return false;
        }
        if (session->download_store && session->download_store->commitPending())
        {
            session->download_store->stepCommit();
            return false;
        }
    }
    if (session->saved) session->saved->releaseRead();
    // Release projection cursors before destroying their shared workspace.
    if (session->store) session->store->releaseDraftRead();
    if (session->download_store) session->download_store->releaseRead();
    if (session->draft_catalog) session->draft_catalog->reading = false;
    if (!router->bindGeocachingHandlers(nullptr, nullptr, nullptr)) return false;
    // Unbinding joins any router callback before clearing its pending payload.
    ::platform::memory::destroyPsram(pending_announcement.exchange(nullptr, std::memory_order_acq_rel));
    ::platform::memory::destroyPsram(pending_reply.exchange(nullptr, std::memory_order_acq_rel));
    if (session->created_backend && router->backendForProtocol(chat::MeshProtocol::Reticulum) == session->created_backend &&
        router->backendProtocol() != chat::MeshProtocol::Reticulum && router->backendProtocol() != chat::MeshProtocol::RNode)
    {
        auto removed = router->takeServiceBackend(chat::MeshProtocol::Reticulum, session->created_backend);
        if (!removed) return false;
    }
    session.reset();
    active.store(false);
    ++epoch;
    return true;
}
} // namespace

void configure(chat::MeshAdapterRouter& value, LoraBoard& radio)
{
    router = &value;
    board = &radio;
    esp_fill_random(boot.data(), boot.size());
    ::geocaching::ui::shell::bind(&facade);
}
bool publicationContext(uint8_t boot_id[16], uint64_t& monotonic_ms)
{
    if (!boot_id || !router || !board) return false;
    std::memcpy(boot_id, boot.data(), boot.size());
    monotonic_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    return true;
}
bool withIdentityMaintenance(bool (*action)(void*), void* context)
{
    if (!action || !ensureMutex()) return false;
    Guard guard;
    if (!guard.locked || pending_reply.load(std::memory_order_acquire) || pending_announcement.load(std::memory_order_acquire)) return false;
    if (session)
    {
        const auto& s = *session;
        if ((s.phase != Phase::Ready && s.phase != Phase::Failed) || s.needsRecovery() || s.recovery || s.checkpoint ||
            s.workspace_owner.holder() || s.response || s.publication_restore_pending || s.download_restore_pending ||
            downloadActive() || publicationActive() || draftSaveActive() ||
            (s.store && s.store->commitPending()) || (s.dispatch_store && s.dispatch_store->busy()) ||
            (s.receipts && s.receipts->pending()) || (s.saved && s.saved->pending()) ||
            (s.draft_catalog && s.draft_catalog->reading) ||
            (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)) return false;
        if (s.client)
        {
            const auto phase = s.client->phase();
            if (phase != gc::QueryClientPhase::Idle && phase != gc::QueryClientPhase::PageReady && phase != gc::QueryClientPhase::Failed) return false;
        }
    }
    if (action(context))
    {
        wanted.store(0);
        network_requested.store(false);
        local_storage_requested.store(false);
        active.store(false);
        session.reset();
        ++epoch;
    }
    return true;
}

bool webLease(bool enabled, bool require_storage)
{
    if (enabled)
    {
        if (!router || !board || !ensureMutex()) return false;
        const auto previous = wanted.fetch_or(2);
        if (require_storage) local_storage_requested.store(true);
        if (previous & 2) return true;
    }
    else
    {
        if (!(wanted.fetch_and(static_cast<uint8_t>(~2)) & 2)) return true;
        cancel_draft_read.fetch_or(2);
    }
    next_step.store(0);
    return true;
}

WebReadResult readWebDraftCatalog(uint32_t offset, bool has_expected, uint64_t expected,
                                  uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || capacity < 384) return WebReadResult::Invalid;
    if (!webLease(true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (s.phase == Phase::Failed) return WebReadResult::Unavailable;
    if (!s.store || s.needsRecovery()) return WebReadResult::Pending;
    if (has_expected && expected != s.store->catalogGeneration()) return WebReadResult::Conflict;
    s.draft_catalog_wanted = true;
    s.map_metadata_only = false;
    if (!s.draft_catalog)
    {
        next_step.store(0);
        return WebReadResult::Pending;
    }
    auto& catalog = *s.draft_catalog;
    const size_t page_offset = (static_cast<size_t>(offset) / 4) * 4;
    if (catalog.requested_offset != page_offset)
    {
        catalog.requested_offset = page_offset;
        catalog.failed = catalog.ready = false;
        if (catalog.reading) s.draft_io_reset = true;
        ++catalog.generation;
        next_step.store(0);
        return WebReadResult::Pending;
    }
    if (catalog.failed && catalog.sequence == s.store->catalogGeneration()) return WebReadResult::Unavailable;
    if (!draftCatalogReady(s)) return WebReadResult::Pending;
    gc::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !writer.unsignedInteger(catalog.sequence) || !writer.unsignedInteger(offset) ||
        !writer.unsignedInteger(catalog.total)) return WebReadResult::Invalid;
    const size_t index = static_cast<size_t>(offset) - page_offset;
    if (index >= catalog.page.count)
    {
        if (!writer.nil()) return WebReadResult::Invalid;
    }
    else
    {
        const auto& row = catalog.page.rows[index];
        const auto& publication = row.publication;
        const uint8_t flags = (publication.pending ? 1 : 0) | (publication.stopped ? 2 : 0) |
                              (publication.local_changes ? 4 : 0) | (publication.base_retained ? 8 : 0);
        if (!writer.array(9) || !writer.binary({row.id.data(), 16}) || !writer.unsignedInteger(row.generation) ||
            !(row.has_author ? writer.binary({row.author.data(), 64}) : writer.nil()) || !writer.text(row.name.data()) ||
            !writer.unsignedInteger(row.state) ||
            !(row.has_coordinates ? writer.signedInteger(row.latitude_e7) : writer.nil()) ||
            !(row.has_coordinates ? writer.signedInteger(row.longitude_e7) : writer.nil()) ||
            !(row.has_base ? writer.binary({row.base_hash.data(), 32}) : writer.nil()) ||
            !writer.array(3) || !writer.unsignedInteger(publication.latest_revision) ||
            !writer.unsignedInteger(publication.confirmed_revision) || !writer.unsignedInteger(flags)) return WebReadResult::Invalid;
    }
    written = writer.size();
    return WebReadResult::Ready;
}
WebReadResult readWebLatestRevision(const uint8_t id[32], const uint8_t directory[16], uint8_t* output, size_t capacity)
{
    if (!id || !directory || !output || capacity < 36) return WebReadResult::Invalid;
    if (!webLease(true, false)) return WebReadResult::Unavailable;
    if (!network_requested.exchange(true)) next_step.store(0);
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (!s.client || !s.port) return WebReadResult::Pending;
    const gc::DirectoryEntry* target = nullptr;
    for (size_t i = 0; i < s.client->directoryCount(); ++i)
    {
        const auto* entry = s.client->directory(i);
        if (entry && !std::memcmp(entry->delivery.bytes.data(), directory, 16))
        {
            target = entry;
            break;
        }
    }
    if (!target) return s.client->directoryCount() ? WebReadResult::NotFound : WebReadResult::Pending;
    if (!target->capabilities_verified) return WebReadResult::Pending;
    if (s.pending_detail || cancel_detail.load() || publicationActive() || downloadActive()) return WebReadResult::Pending;
    if (!s.detail)
    {
        auto job = ::platform::memory::PsramPtr<CacheDetail>(::platform::memory::createPsram<CacheDetail>());
        if (!job) return WebReadResult::Unavailable;
        std::memcpy(job->id.bytes.data(), id, 32);
        job->remote = target->delivery;
        job->state = CacheDetail::State::LatestNetwork;
        job->started = now(nullptr).monotonic_ms;
        if (!randomId(nullptr, job->request.bytes.data()) ||
            !gc::protocol::encodeGetRequest(job->request, job->id, nullptr, nullptr, 8192,
                                            job->request_bytes.data(), job->request_bytes.size(), job->request_size) ||
            job->request_size > CacheDetail::kLatestEpochOffset) return WebReadResult::Unavailable;
        std::memcpy(job->request_bytes.data() + CacheDetail::kLatestEpochOffset, target->epoch.data(), 16);
        s.detail = std::move(job);
        next_step.store(0);
        return WebReadResult::Pending;
    }
    auto& job = *s.detail;
    if (std::memcmp(job.id.bytes.data(), id, 32) || std::memcmp(job.remote.bytes.data(), directory, 16)) return WebReadResult::Conflict;
    const bool latest_job = job.state == CacheDetail::State::LatestNetwork || job.state == CacheDetail::State::LatestReady ||
                            job.state == CacheDetail::State::LatestFailed || job.state == CacheDetail::State::LatestNotFound;
    if (latest_job && std::memcmp(job.request_bytes.data() + CacheDetail::kLatestEpochOffset, target->epoch.data(), 16))
    {
        s.detail.reset();
        return WebReadResult::Conflict;
    }
    if (job.state == CacheDetail::State::LatestNetwork) return WebReadResult::Pending;
    if (job.state == CacheDetail::State::LatestNotFound)
    {
        std::memset(output, 0, 36);
        s.detail.reset();
        return WebReadResult::Absent;
    }
    if (job.state == CacheDetail::State::LatestFailed)
    {
        s.detail.reset();
        return WebReadResult::Unavailable;
    }
    if (job.state != CacheDetail::State::LatestReady) return WebReadResult::Conflict;
    const auto revision = job.verified_record.record.revision;
    for (unsigned i = 0; i < 4; ++i) output[i] = static_cast<uint8_t>(revision >> (8 * i));
    std::memcpy(output + 4, job.verified_record.hash.bytes.data(), 32);
    s.detail.reset();
    return WebReadResult::Ready;
}
WebReadResult readWebDirectories(uint32_t offset, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || capacity < 320 || offset > gc::QueryClient::kDirectoryCapacity) return WebReadResult::Invalid;
    if (!webLease(true, false)) return WebReadResult::Unavailable;
    if (!network_requested.exchange(true)) next_step.store(0);
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    if (session->phase == Phase::Failed || session->workspace_unavailable) return WebReadResult::Unavailable;
    if (!session->client) return WebReadResult::Pending;
    const auto& client = *session->client;
    const auto* entry = client.directory(offset);
    gc::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(3) || !writer.unsignedInteger(offset) || !writer.unsignedInteger(client.directoryCount())) return WebReadResult::Invalid;
    if (!entry)
    {
        if (!writer.nil()) return WebReadResult::Invalid;
    }
    else
    {
        const auto current = now(nullptr).monotonic_ms;
        const auto age = current >= entry->last_seen ? current - entry->last_seen : 0;
        const uint8_t flags = (entry->capabilities_verified ? 1 : 0) | (entry->in_flight ? 2 : 0);
        if (!writer.array(9) || !writer.binary({entry->delivery.bytes.data(), 16}) || !writer.binary({entry->discovery.bytes.data(), 16}) ||
            !writer.binary({entry->public_key.data(), 64}) || !writer.binary({entry->epoch.data(), 16}) || !writer.unsignedInteger(entry->sequence) ||
            !writer.text(entry->name.data()) || !writer.unsignedInteger(flags) || !writer.unsignedInteger(entry->max_query_items) ||
            !writer.unsignedInteger(age)) return WebReadResult::Invalid;
    }
    written = writer.size();
    return WebReadResult::Ready;
}

static WebReadResult prepareWebDraftRead(Session& s, const uint8_t id[16])
{
    releaseDraftReaders(s, cancel_draft_read.exchange(0));
    if (!s.draft_read || std::memcmp(s.draft_read->id.data(), id, 16))
    {
        if (s.draft_read && (s.draft_read->users & 1)) return WebReadResult::Pending;
        s.draft_io_reset = true;
        s.draft_read.reset(::platform::memory::createPsram<Session::DraftRead>());
        if (!s.draft_read) return WebReadResult::Unavailable;
        std::memcpy(s.draft_read->id.data(), id, 16);
        s.draft_read->users = 2;
        next_step.store(0);
        return WebReadResult::Pending;
    }
    auto& job = *s.draft_read;
    job.users |= 2;
    if (job.status == ::ui::geocaching::DraftReadStatus::Pending) return WebReadResult::Pending;
    if (job.status != ::ui::geocaching::DraftReadStatus::Ready)
    {
        const auto result = job.result == DraftReadResult::NotFound ? WebReadResult::NotFound : WebReadResult::Unavailable;
        releaseDraftReaders(s, 2);
        return result;
    }
    return WebReadResult::Ready;
}
WebReadResult readWebPublicationBinding(const uint8_t id[16], uint64_t generation, const uint8_t directory[16],
                                        const uint8_t expected_author[64], uint8_t* output, size_t capacity)
{
    if (!id || !generation || !directory || !expected_author || !output || capacity < 56) return WebReadResult::Invalid;
    if (!webLease(true)) return WebReadResult::Unavailable;
    if (!network_requested.exchange(true)) next_step.store(0);
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (s.phase == Phase::Failed || s.workspace_unavailable) return WebReadResult::Unavailable;
    if (!s.store || s.needsRecovery() || !s.client) return WebReadResult::Pending;
    const gc::DirectoryEntry* target = nullptr;
    for (size_t i = 0; i < s.client->directoryCount(); ++i)
    {
        const auto* entry = s.client->directory(i);
        if (entry && !std::memcmp(entry->delivery.bytes.data(), directory, 16))
        {
            target = entry;
            break;
        }
    }
    if (!target) return WebReadResult::NotFound;
    if (!target->capabilities_verified) return WebReadResult::Pending;
    if (s.store->commitPending() || downloadActive() || publicationActive() || draftSaveActive()) return WebReadResult::Pending;
    const auto ready = prepareWebDraftRead(s, id);
    if (ready != WebReadResult::Ready) return ready;
    const auto& job = *s.draft_read;
    gc::storage::DraftView draft;
    auto result = WebReadResult::Unavailable;
    if (gc::storage::decodeDraft({id, 16}, {job.bytes, job.size}, draft))
    {
        if (draft.generation != generation) result = WebReadResult::Conflict;
        else if (draft.author.size && std::memcmp(draft.author.data, expected_author, 64)) result = WebReadResult::Forbidden;
        else if (!draft.has_coordinates || !gc::protocol::validRecordText(draft.name, false, true)) result = WebReadResult::Invalid;
        else
        {
            s.draft_catalog_wanted = true;
            s.map_metadata_only = false;
            if (!draftCatalogReady(s))
            {
                s.draft_catalog_wanted = true;
                next_step.store(0);
                return WebReadResult::Pending;
            }
            bool in_window = false;
            for (size_t i = 0; i < s.draft_catalog->page.count; ++i)
                in_window |= !std::memcmp(s.draft_catalog->page.rows[i].id.data(), id, 16);
            if (!in_window)
            {
                // The committed row already proved the ID exists. Seek it in
                // the existing four-row window, including after session reset.
                auto& catalog = *s.draft_catalog;
                const auto next = catalog.page.offset + catalog.page.count;
                catalog.requested_offset = next < catalog.page.total ? next : 0;
                catalog.ready = catalog.failed = false;
                releaseDraftReaders(s, 2);
                next_step.store(0);
                return WebReadResult::Pending;
            }
            std::array<uint8_t, 16> draft_id;
            std::memcpy(draft_id.data(), id, draft_id.size());
            std::array<uint8_t, 64> author;
            uint32_t from = 0, to = 0;
            if (!router->getGeocachingAuthorKey(author.data())) result = WebReadResult::Pending;
            else if (std::memcmp(author.data(), expected_author, author.size())) result = WebReadResult::StaleIdentity;
            else if (!publicationDraftReady(draft_id, generation, author, &from, &to, &target->delivery)) result = WebReadResult::Conflict;
            else
            {
                Digest digest;
                digest.update(job.bytes, job.size);
                if (digest.finalize(output, 32))
                {
                    std::memcpy(output + 32, target->epoch.data(), target->epoch.size());
                    if (capacity >= 88)
                    {
                        std::memset(output + 56, 0, 32);
                        if (draft.base_hash.size) std::memcpy(output + 56, draft.base_hash.data, 32);
                    }
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        output[48 + i] = static_cast<uint8_t>(from >> (8 * i));
                        output[52 + i] = static_cast<uint8_t>(to >> (8 * i));
                    }
                    result = WebReadResult::Ready;
                }
            }
        }
    }
    releaseDraftReaders(s, 2);
    return result;
}
WebReadResult checkWebDraftEditOwner(const uint8_t id[16], uint64_t expected, const uint8_t author[64], const uint8_t* edit_hash)
{
    if (!id || !author || expected == UINT64_MAX) return WebReadResult::Invalid;
    if (!webLease(true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (s.phase == Phase::Failed) return WebReadResult::Unavailable;
    if (!s.store || s.needsRecovery()) return WebReadResult::Pending;
    const auto ready = prepareWebDraftRead(s, id);
    if (ready == WebReadResult::NotFound) return expected ? WebReadResult::Conflict : WebReadResult::Ready;
    if (ready != WebReadResult::Ready) return ready;
    gc::storage::DraftView draft;
    const auto& job = *s.draft_read;
    auto result = WebReadResult::Unavailable;
    if (gc::storage::decodeDraft({id, 16}, {job.bytes, job.size}, draft))
    {
        if (draft.author.size && std::memcmp(draft.author.data, author, 64)) result = WebReadResult::Forbidden;
        else if (draft.generation == expected) result = WebReadResult::Ready;
        else if (edit_hash && draft.generation == expected + 1)
        {
            // Canonical fixed-width author/base values are the only bytes
            // inserted by editDraft. Hash nil metadata plus the borrowed suffix,
            // exactly matching the uploaded edit, without allocating a clone.
            const size_t suffix = 1 + (draft.author.size ? 66 : 1) + (draft.base_hash.size ? 34 : 1);
            constexpr uint8_t prefix[] = {0x94, 0xc0, 0xc0};
            uint8_t hash[32];
            Digest digest;
            digest.update(prefix, sizeof(prefix));
            digest.update(job.bytes + suffix, job.size - suffix);
            result = !digest.finalize(hash, sizeof(hash)) ? WebReadResult::Unavailable
                     : std::memcmp(hash, edit_hash, 32)   ? WebReadResult::Conflict
                                                          : WebReadResult::AlreadySaved;
        }
        else result = WebReadResult::Conflict;
    }
    releaseDraftReaders(s, 2);
    return result;
}
WebReadResult readWebDraftChunk(const uint8_t id[16], uint32_t offset, uint16_t count, uint64_t expected,
                                uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!id || !output || !count || count > 320 || capacity < 384 || (offset && !expected)) return WebReadResult::Invalid;
    if (!webLease(true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (s.phase == Phase::Failed) return WebReadResult::Unavailable;
    if (!s.store || s.needsRecovery()) return WebReadResult::Pending;
    const auto ready = prepareWebDraftRead(s, id);
    if (ready != WebReadResult::Ready) return ready;
    auto& job = *s.draft_read;
    gc::storage::DraftView draft;
    if (!gc::storage::decodeDraft({id, 16}, {job.bytes, job.size}, draft))
    {
        releaseDraftReaders(s, 2);
        return WebReadResult::Unavailable;
    }
    if ((expected && draft.generation != expected) || offset > job.size)
    {
        releaseDraftReaders(s, 2);
        return WebReadResult::Conflict;
    }
    const size_t length = std::min(static_cast<size_t>(count), job.size - static_cast<size_t>(offset));
    gc::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(5) || !writer.binary({job.id.data(), 16}) || !writer.unsignedInteger(draft.generation) ||
        !writer.unsignedInteger(job.size) || !writer.unsignedInteger(offset) || !writer.binary({job.bytes + offset, length}))
        return WebReadResult::Invalid;
    written = writer.size();
    if (offset + length == job.size) releaseDraftReaders(s, 2);
    return WebReadResult::Ready;
}
bool queueDraftSave(const uint8_t id[16], const uint8_t* bytes, size_t size, uint64_t expected, bool editor_fields)
{
    Guard guard;
    gc::storage::DraftView draft;
    if (!guard.locked || !id || !session || !session->store || session->store->commitPending() || session->needsRecovery() ||
        downloadActive() || publicationActive() || draftSaveActive() || session->phase == Phase::ResumeDownloads ||
        !gc::storage::decodeDraft({id, 16}, {bytes, size}, draft) || expected == UINT64_MAX || draft.generation != expected + 1 ||
        (editor_fields && (draft.author.size || draft.base_hash.size))) return false;
    auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
    if (!job) return false;
    // editDraft prepends retained author/base metadata in this same allocation.
    // Two MessagePack binary values replace two nils: at most 100 extra bytes.
    const size_t capacity = size + (editor_fields ? 100 : 0);
    job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.draft", capacity, false));
    if (!job->bytes) return false;
    std::memcpy(job->id.data(), id, 16);
    std::memcpy(job->bytes, bytes, size);
    job->size = size;
    job->capacity = capacity;
    job->editor_fields = editor_fields;
    job->expected = expected;
    session->draft_save = std::move(job);
    next_step.store(0);
    ++epoch;
    return true;
}
WebReadResult queueWebDraftFile(const uint8_t id[16], uint32_t total, uint64_t expected,
                                const uint8_t hash[32], const uint8_t author[64], uint32_t author_generation)
{
    if (!id || !hash || !author || !total || total > 4096 || expected == UINT64_MAX || !author_generation) return WebReadResult::Invalid;
    if (!webLease(true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (s.phase == Phase::Failed) return WebReadResult::Unavailable;
    if (s.draft_save && s.draft_save->web_job && s.draft_save->expected == expected &&
        !std::memcmp(s.draft_save->id.data(), id, 16))
    {
        const auto& job = *s.draft_save;
        if (job.capacity != static_cast<size_t>(total) + 100 || std::memcmp(job.hash.data(), hash, 32)) return WebReadResult::Conflict;
        return !job.done ? WebReadResult::Pending : job.saved ? WebReadResult::AlreadySaved
                                                              : static_cast<WebReadResult>(job.cache[0]);
    }
    if (s.phase != Phase::Ready || !s.store || s.needsRecovery() || s.store->commitPending() ||
        s.draft_read || downloadActive() || publicationActive() || draftSaveActive()) return WebReadResult::Pending;
    auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
    if (!job) return WebReadResult::Unavailable;
    job->capacity = total + 100;
    job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.draft", job->capacity, false));
    if (!job->bytes) return WebReadResult::Unavailable;
    std::memcpy(job->id.data(), id, 16);
    std::memcpy(job->hash.data(), hash, 32);
    // These checks fit in the existing identity-restoration headroom. They are
    // consumed before editDraft can move the encoded suffix into that space.
    std::memcpy(job->bytes + total, author, 64);
    std::memcpy(job->bytes + total + 64, &author_generation, sizeof(author_generation));
    job->expected = expected;
    job->web_job = job->web_file = true;
    s.draft_save = std::move(job);
    next_step.store(0);
    ++epoch;
    return WebReadResult::Pending;
}
bool releaseWebDraftSave()
{
    if (!mutex.load(std::memory_order_acquire)) return true;
    Guard guard;
    if (!guard.locked) return false;
    if (!session || !session->draft_save || !session->draft_save->web_job) return true;
    const auto& job = *session->draft_save;
    if (!job.done || job.started || job.saved) return false;
    session->draft_save.reset();
    ++epoch;
    next_step.store(0);
    return true;
}
static void failWebDraftSave(Session::DraftSave& job, WebReadResult result)
{
    job.done = true;
    job.cache[0] = static_cast<uint8_t>(result);
    heap_caps_free(job.bytes);
    job.bytes = nullptr;
    ++epoch;
}
static bool advanceWebDraftFile(Session& s)
{
    auto& job = *s.draft_save;
    const size_t total = job.capacity - 100;
    storage::SdRuntimeFile file;
    const size_t count = std::min(size_t(320), total - job.size);
    if (!file.open(kWebDraftCandidatePath, "r") || file.size() != total || !file.seek(job.size) ||
        file.read(job.bytes + job.size, count) != static_cast<int>(count))
    {
        failWebDraftSave(job, WebReadResult::Unavailable);
        return true;
    }
    job.size += count;
    if (job.size != total) return true;
    if (!s.crypto.sha256({job.bytes, total}, job.cache.data())) failWebDraftSave(job, WebReadResult::Unavailable);
    else if (std::memcmp(job.cache.data(), job.hash.data(), 32)) failWebDraftSave(job, WebReadResult::HashMismatch);
    else
    {
        gc::storage::DraftView draft;
        if (!gc::storage::decodeDraft({job.id.data(), 16}, {job.bytes, total}, draft) ||
            draft.author.size || draft.base_hash.size || draft.generation != job.expected + 1)
            failWebDraftSave(job, WebReadResult::Invalid);
        else
        {
            job.web_file = false;
            job.cache[0] = static_cast<uint8_t>(WebReadResult::Unavailable);
        }
    }
    return true;
}
WebReadResult requestWebPublicationStop(const uint8_t operation_id[16], const uint8_t expected_author[64])
{
    if (!operation_id || !expected_author) return WebReadResult::Invalid;
    uint8_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) bits |= operation_id[i];
    if (!bits) return WebReadResult::Invalid;
    if (!webLease(true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (s.phase == Phase::Failed) return WebReadResult::Unavailable;
    if (!s.store || s.needsRecovery() || s.phase != Phase::Ready) return WebReadResult::Pending;
    const auto* current_key = s.publication && s.publication->attempt         ? s.publication->attempt->requestId().bytes.data()
                              : s.publication && s.publication->web_operation ? s.publication->operation_id.data()
                                                                              : nullptr;
    const bool matches_current = current_key && !std::memcmp(current_key, operation_id, 16);
    const bool failed_current = matches_current && s.publication->attempt && s.publication->attempt->phase() == gc::PublishAttemptPhase::Failed;
    if (!matches_current || failed_current)
    {
        if (publicationActive() || downloadActive() || draftSaveActive() || s.store->commitPending()) return WebReadResult::Pending;
        std::array<uint8_t, 64> actual;
        if (!router->getGeocachingAuthorKey(actual.data())) return WebReadResult::Pending;
        if (std::memcmp(actual.data(), expected_author, actual.size())) return WebReadResult::StaleIdentity;
        if (s.local_read_only)
        {
            // A readable startup snapshot has not finished the journal audit.
            // Finish it before restoring a writer bound to this root/store.
            s.workspace_owner.release(s.detail.get());
            if (s.detail && s.detail->state == CacheDetail::State::Ready) s.detail->saved_read.reset();
            else s.detail.reset();
            s.pending_detail.reset();
            s.store->releaseDraftRead();
            s.saved.reset();
            if (s.draft_catalog) s.draft_catalog->reading = false;
            s.draft_read.reset();
            s.store.reset();
            s.download_store.reset();
            s.local_read_only = false;
            startRecovery();
            next_step.store(0);
            return WebReadResult::Pending;
        }
        std::array<uint8_t, 16> task;
        std::memcpy(task.data(), operation_id, task.size());
        const auto restored = restorePublication(s, nullptr, nullptr, nullptr, &task, expected_author);
        if (restored == PublicationRestore::Pending) return WebReadResult::Pending;
        if (restored == PublicationRestore::None) return WebReadResult::NotFound;
        if (restored == PublicationRestore::Forbidden) return WebReadResult::Forbidden;
        if (restored != PublicationRestore::Restored) return WebReadResult::Unavailable;
    }
    auto& job = *session->publication;
    const auto* key = job.attempt ? job.attempt->requestId().bytes.data() : job.web_operation ? job.operation_id.data()
                                                                                              : nullptr;
    if (!key || std::memcmp(key, operation_id, 16)) return WebReadResult::NotFound;
    if (std::memcmp(job.author.data(), expected_author, job.author.size())) return WebReadResult::Forbidden;
    std::array<uint8_t, 64> actual;
    if (!router->getGeocachingAuthorKey(actual.data())) return WebReadResult::Pending;
    if (std::memcmp(actual.data(), expected_author, actual.size())) return WebReadResult::StaleIdentity;
    // A signed record still needs a durable attempt before a stopped receipt.
    // Preserve the request across asynchronous historical lookup/admission so
    // the maintenance owner cancels it before transport can dispatch it.
    if (!job.attempt)
    {
        if (job.bytes && job.draft_stage == Session::Publication::DraftStage::None)
        {
            job.stop_requested = true;
            next_step.store(0);
        }
        return WebReadResult::Pending;
    }
    const auto phase = job.attempt->phase();
    if (phase == gc::PublishAttemptPhase::Confirmed || phase == gc::PublishAttemptPhase::Cancelled) return WebReadResult::Ready;
    if (phase == gc::PublishAttemptPhase::Committing) return WebReadResult::Pending;
    if (phase == gc::PublishAttemptPhase::Idle || phase == gc::PublishAttemptPhase::Failed) return WebReadResult::Unavailable;
    if (!job.attempt->cancel()) return WebReadResult::Unavailable;
    ++epoch;
    next_step.store(0);
    return job.attempt->phase() == gc::PublishAttemptPhase::Cancelled ? WebReadResult::Ready : WebReadResult::Pending;
}
WebReadResult readWebPublicationHistory(const uint8_t operation_id[16], const uint8_t expected_author[64],
                                        uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!operation_id || !expected_author || !output || capacity < 192) return WebReadResult::Invalid;
    uint8_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) bits |= operation_id[i];
    if (!bits) return WebReadResult::Invalid;
    if (!webLease(true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    auto& s = *session;
    if (s.phase == Phase::Failed) return WebReadResult::Unavailable;
    if (!s.store || s.needsRecovery()) return WebReadResult::Pending;
    std::array<uint8_t, 64> actual;
    if (!router->getGeocachingAuthorKey(actual.data())) return WebReadResult::Pending;
    if (std::memcmp(actual.data(), expected_author, actual.size())) return WebReadResult::StaleIdentity;
    gc::storage::PublicationRecoveryFilter filter;
    filter.local = s.local;
    filter.has_operation = filter.include_terminal = true;
    std::memcpy(filter.operation.data(), operation_id, 16);
    gc::storage::PublicationRecoveryView selected;
    auto read = DraftReadResult::Pending;
    // Bounded slices on the existing command consumer. No new worker or queue.
    for (unsigned i = 0; i < 8 && read == DraftReadResult::Pending; ++i)
        read = s.store->readPublicationRecovery(filter, selected);
    if (read == DraftReadResult::Pending || read == DraftReadResult::Busy) return WebReadResult::Pending;
    if (read == DraftReadResult::NotFound) return WebReadResult::NotFound;
    if (read != DraftReadResult::Ready) return WebReadResult::Unavailable;
    struct ReadLease
    {
        PublicationStore& store;
        ~ReadLease() { store.releaseDraftRead(); }
    } lease{*s.store};
    auto verified = ::platform::memory::PsramPtr<gc::protocol::VerifiedRecordView>(::platform::memory::createPsram<gc::protocol::VerifiedRecordView>());
    if (!verified) return WebReadResult::Unavailable;
    const auto result = gc::storage::encodePublicationHistoryJob(selected, {operation_id, 16}, {expected_author, 64},
                                                                 s.crypto, s.verification, kVerificationCapacity, *verified,
                                                                 output, capacity, written);
    switch (result)
    {
    case gc::storage::PublicationHistoryJobResult::Ready:
        return WebReadResult::Ready;
    case gc::storage::PublicationHistoryJobResult::Forbidden:
        return WebReadResult::Forbidden;
    case gc::storage::PublicationHistoryJobResult::Invalid:
        return WebReadResult::Invalid;
    default:
        return WebReadResult::Unavailable;
    }
}
WebReadResult readWebPublicationJob(const uint8_t operation_id[16], const uint8_t expected_author[64],
                                    uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!operation_id || !expected_author || !output || capacity < 192) return WebReadResult::Invalid;
    if (!webLease(true, false)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session) return WebReadResult::Pending;
    if (!session->publication) return WebReadResult::NotFound;
    const auto& job = *session->publication;
    const auto* attempt = job.attempt.get();
    const auto* key = attempt ? attempt->requestId().bytes.data() : job.web_operation ? job.operation_id.data()
                                                                                      : nullptr;
    if (!key || std::memcmp(key, operation_id, 16)) return WebReadResult::NotFound;
    if (std::memcmp(job.author.data(), expected_author, job.author.size())) return WebReadResult::Forbidden;
    std::array<uint8_t, 64> actual_author;
    if (!router->getGeocachingAuthorKey(actual_author.data())) return WebReadResult::Pending;
    if (std::memcmp(actual_author.data(), expected_author, actual_author.size())) return WebReadResult::StaleIdentity;
    uint8_t phase = 0;
    int durable = 0, transported = 0; // -1 means not known from this projection.
    if (attempt)
    {
        using Phase = gc::PublishAttemptPhase;
        switch (attempt->phase())
        {
        case Phase::Idle:
            phase = 0;
            break;
        case Phase::Submitting:
            phase = 1;
            break;
        case Phase::Waiting:
            phase = 2;
            durable = 1;
            transported = job.submitted ? 1 : -1;
            break;
        case Phase::Committing:
            phase = 2;
            durable = 1;
            transported = 1;
            break;
        case Phase::Confirmed:
            phase = 3;
            durable = transported = 1;
            break;
        case Phase::Cancelled:
            phase = 4;
            durable = 1;
            transported = job.submitted ? 1 : -1;
            break;
        case Phase::Failed:
            phase = 5;
            durable = transported = -1;
            break;
        case Phase::Cancelling:
            phase = 6;
            durable = -1;
            transported = job.submitted ? 1 : -1;
            break;
        }
    }
    else if (job.stop_requested)
    {
        phase = 6;
        durable = transported = -1;
    }
    const char* error = job.error;
    if (!attempt && (job.error || (job.draft_stage == Session::Publication::DraftStage::None && !job.bytes)))
    {
        phase = 5;
        durable = transported = -1;
        if (!error) error = "Publication could not be admitted";
    }
    gc::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(8) || !writer.binary({operation_id, 16}) || !writer.unsignedInteger(phase) ||
        !(attempt ? writer.binary({attempt->cacheId().bytes.data(), 32}) : writer.nil()) ||
        !(attempt ? writer.binary({attempt->revisionHash().bytes.data(), 32}) : writer.nil()) ||
        !writer.unsignedInteger(attempt ? attempt->revision() : job.expected_revision) ||
        !(durable < 0 ? writer.nil() : writer.unsignedInteger(static_cast<uint8_t>(durable))) ||
        !(transported < 0 ? writer.nil() : writer.unsignedInteger(static_cast<uint8_t>(transported))) ||
        !(error ? writer.text(error) : writer.nil())) return WebReadResult::Invalid;
    written = writer.size();
    return WebReadResult::Ready;
}

bool webPublicationPlan(const uint8_t id[16], uint64_t generation, const uint8_t directory[16],
                        uint8_t author[64], uint32_t& from, uint32_t& to)
{
    Guard guard;
    if (!guard.locked || !id || !directory || !author) return false;
    std::array<uint8_t, 16> draft_id;
    gc::Destination remote;
    std::array<uint8_t, 64> key;
    std::memcpy(draft_id.data(), id, draft_id.size());
    std::memcpy(remote.bytes.data(), directory, remote.bytes.size());
    uint32_t previous = 0, next = 0;
    if (!publicationDraftReady(draft_id, generation, key, &previous, &next, &remote)) return false;
    std::memcpy(author, key.data(), key.size());
    from = previous;
    to = next;
    return true;
}

bool queueWebDraftPublication(const uint8_t id[16], uint64_t generation, const uint8_t directory[16],
                              const uint8_t expected_author[64], uint32_t expected_revision, const uint8_t* expected_binding,
                              const uint8_t* operation_id)
{
    Guard guard;
    if (!guard.locked || !id || !directory || !expected_author) return false;
    if (operation_id)
    {
        uint8_t bits = 0;
        for (unsigned i = 0; i < 16; ++i) bits |= operation_id[i];
        if (!bits) return false;
    }
    std::array<uint8_t, 16> draft_id;
    gc::Destination remote;
    std::array<uint8_t, 64> author;
    std::memcpy(draft_id.data(), id, draft_id.size());
    std::memcpy(remote.bytes.data(), directory, remote.bytes.size());
    uint32_t previous = 0, revision = 0;
    if (!publicationDraftReady(draft_id, generation, author, &previous, &revision, &remote) ||
        std::memcmp(author.data(), expected_author, author.size()) || revision != expected_revision) return false;
    if (expected_binding)
    {
        const gc::DirectoryEntry* target = nullptr;
        for (size_t i = 0; i < session->client->directoryCount(); ++i)
        {
            const auto* entry = session->client->directory(i);
            if (entry && entry->delivery.bytes == remote.bytes)
            {
                target = entry;
                break;
            }
        }
        if (!target || std::memcmp(target->epoch.data(), expected_binding + 32, target->epoch.size())) return false;
        for (unsigned i = 0; i < 4; ++i)
            if (expected_binding[48 + i] != static_cast<uint8_t>(previous >> (8 * i)) ||
                expected_binding[52 + i] != static_cast<uint8_t>(revision >> (8 * i))) return false;
        if (prepareWebDraftRead(*session, id) != WebReadResult::Ready) return false;
        const auto& read = *session->draft_read;
        gc::storage::DraftView draft;
        uint8_t hash[32];
        Digest digest;
        digest.update(read.bytes, read.size);
        const bool matches = gc::storage::decodeDraft({id, 16}, {read.bytes, read.size}, draft) &&
                             draft.generation == generation && digest.finalize(hash, sizeof(hash)) &&
                             !std::memcmp(hash, expected_binding, sizeof(hash));
        releaseDraftReaders(*session, 2);
        if (!matches) return false;
    }
    auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
    if (!job) return false;
    job->remote = remote;
    job->draft_id = draft_id;
    job->draft_generation = generation;
    job->author = author;
    job->expected_revision = revision;
    if (operation_id)
    {
        job->web_operation = true;
        std::memcpy(job->operation_id.data(), operation_id, 16);
    }
    job->issued = now(nullptr);
    job->issued.has_utc = true;
    job->issued.utc_seconds = static_cast<uint64_t>(std::time(nullptr));
    job->draft_stage = Session::Publication::DraftStage::Bind;
    session->draft_save.reset();
    session->publication = std::move(job);
    next_step.store(0);
    ++epoch;
    return true;
}

bool queuePublication(const uint8_t* bytes, size_t size)
{
    Guard guard;
    if (!guard.locked || !bytes || !size || size > 4166 || !session || session->phase != Phase::Ready ||
        !session->port || !session->store || session->store->commitPending() || session->needsRecovery() ||
        downloadActive() || publicationActive() || draftSaveActive()) return false;
    gc::Destination remote;
    if (!session->port->pageSource(remote)) return false;
    auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
    if (!job) return false;
    job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.publish", 2 * size + 26, false));
    if (!job->bytes) return false;
    std::memcpy(job->bytes, bytes, size);
    job->size = size;
    job->remote = remote;
    session->draft_save.reset();
    session->publication = std::move(job);
    next_step.store(0);
    ++epoch;
    return true;
}
WebReadResult queueWebSignature(const uint8_t operation[16], const uint8_t owner[16], const uint8_t transfer[16],
                                const uint8_t hash[32], uint32_t total, const uint8_t author[64], uint32_t generation,
                                const uint8_t* draft, uint64_t draft_generation)
{
    if (!operation || !owner || !transfer || !hash || !author || !generation || !total || total > 4096) return WebReadResult::Invalid;
    if (bool(draft) != bool(draft_generation)) return WebReadResult::Invalid;
    if (!webLease(true, true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session || !router) return WebReadResult::Pending;
    auto& s = *session;
    if (s.web_signature)
    {
        const auto& job = *s.web_signature;
        if (std::memcmp(job.owner.data(), owner, 16)) return WebReadResult::Forbidden;
        return !std::memcmp(job.operation.data(), operation, 16) && !std::memcmp(job.transfer.data(), transfer, 16) &&
                       !std::memcmp(job.hash.data(), hash, 32) && !std::memcmp(job.author.data(), author, 64) &&
                       job.generation == generation && job.total == total && job.draft_generation == draft_generation &&
                       (!draft || !std::memcmp(job.draft.data(), draft, 16))
                   ? WebReadResult::Ready
                   : WebReadResult::Conflict;
    }
    if (s.phase != Phase::Ready || !s.store || s.needsRecovery() || s.store->commitPending() ||
        s.workspace_owner.holder() || downloadActive() || publicationActive() || draftSaveActive()) return WebReadResult::Pending;
    auto job = ::platform::memory::PsramPtr<Session::WebSignature>(::platform::memory::createPsram<Session::WebSignature>(*router, *s.store, s.crypto, now(nullptr), draft, draft_generation));
    if (!job) return WebReadResult::Unavailable;
    job->record = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    job->capacity = total + 70;
    job->work = static_cast<uint8_t*>(heap_caps_malloc(job->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!job->record || !job->work) return WebReadResult::Unavailable;
    std::memcpy(job->operation.data(), operation, 16);
    std::memcpy(job->owner.data(), owner, 16);
    std::memcpy(job->transfer.data(), transfer, 16);
    std::memcpy(job->hash.data(), hash, 32);
    std::memcpy(job->author.data(), author, 64);
    job->generation = generation;
    job->total = total;
    job->media = storage::sd_media_session();
    job->started = now(nullptr).monotonic_ms;
    s.web_signature = std::move(job);
    next_step.store(0);
    return WebReadResult::Ready;
}

bool validWebSignature(Session::WebSignature& job)
{
    if (job.failed || !router || job.media != storage::sd_media_session() || !storage::sd_card_ready() || storage::sd_external_block_owner_active() ||
        now(nullptr).monotonic_ms - job.started >= 120000) return false;
    std::array<uint8_t, 64> author{};
    uint32_t generation = 0;
    return router->readDeviceAuthorKey(author.data(), generation) == chat::DeviceAuthorResult::Ok &&
           generation == job.generation && !std::memcmp(author.data(), job.author.data(), 64);
}
void advanceWebSignature(Session& s)
{
    if (!(wanted.load() & 2))
    {
        s.web_signature.reset();
        return;
    }
    auto& job = *s.web_signature;
    if (!validWebSignature(job))
    {
        job.fail();
        return;
    }
    if (job.offset < job.total)
    {
        if (!job.file.is_open() && (!job.file.open("/trailmate/cache.sign.upload", "r", job.media) || job.file.size() != job.total))
        {
            job.fail();
            return;
        }
        const size_t count = std::min(size_t(320), size_t(job.total - job.offset));
        if (job.file.read(job.record + job.offset, count) != static_cast<int>(count))
        {
            job.fail();
            return;
        }
        job.digest.update(job.record + job.offset, count);
        job.offset += count;
        return;
    }
    if (!job.issuing)
    {
        job.file.close();
        if (!job.digest.finalize(job.work, 32) || std::memcmp(job.work, job.hash.data(), 32) ||
            !job.issue.begin({job.record, job.total}, job.work, job.capacity))
        {
            job.fail();
            return;
        }
        job.issuing = true;
    }
    job.issue.advance();
    if (job.issue.phase() == gc::AuthorIssuePhase::Failed) job.fail();
    else if (job.issue.phase() == gc::AuthorIssuePhase::Signed && job.record)
    {
        heap_caps_free(job.record);
        job.record = nullptr;
    }
}
WebReadResult readWebSignature(const uint8_t operation[16], const uint8_t owner[16], bool status_only,
                               uint32_t offset, uint16_t maximum, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    Guard guard;
    if (!guard.locked) return WebReadResult::Pending;
    if (!operation || !owner || !output || !session || !session->web_signature) return WebReadResult::NotFound;
    auto& job = *session->web_signature;
    if (std::memcmp(job.owner.data(), owner, 16)) return WebReadResult::Forbidden;
    if (std::memcmp(job.operation.data(), operation, 16)) return WebReadResult::NotFound;
    if (!validWebSignature(job)) job.fail();
    const auto signed_record = job.issue.signedRecord();
    const uint8_t phase = job.failed ? 2 : signed_record.size ? 1
                                                              : 0;
    gc::protocol::CmpWriter writer(output, capacity);
    if (status_only)
    {
        if (!writer.array(5) || !writer.binary({job.operation.data(), 16}) || !writer.binary({job.hash.data(), 32}) ||
            !writer.unsignedInteger(job.generation) || !writer.unsignedInteger(phase) || !writer.unsignedInteger(phase == 1 ? signed_record.size : 0)) return WebReadResult::Invalid;
    }
    else
    {
        if (phase != 1) return WebReadResult::Pending;
        if (!maximum || maximum > 320 || offset >= signed_record.size) return WebReadResult::Invalid;
        const size_t count = std::min<size_t>(maximum, signed_record.size - offset);
        if (!writer.array(5) || !writer.binary({job.operation.data(), 16}) || !writer.unsignedInteger(offset) || !writer.unsignedInteger(signed_record.size) ||
            !writer.binary({job.hash.data(), 32}) || !writer.binary({signed_record.data + offset, count})) return WebReadResult::Invalid;
    }
    written = writer.size();
    return WebReadResult::Ready;
}
WebReadResult releaseWebSignature(const uint8_t owner[16], const uint8_t* operation)
{
    Guard guard;
    if (!guard.locked) return WebReadResult::Pending;
    if (!owner || !session || !session->web_signature) return WebReadResult::NotFound;
    const auto& job = *session->web_signature;
    if (std::memcmp(job.owner.data(), owner, 16)) return WebReadResult::Forbidden;
    if (operation && std::memcmp(job.operation.data(), operation, 16)) return WebReadResult::NotFound;
    session->web_signature.reset();
    return WebReadResult::Ready;
}
WebReadResult sharedSignatureUploadIdle()
{
    Guard guard;
    if (!guard.locked || (session && (session->web_signature || session->web_confirmation))) return WebReadResult::Pending;
    return WebReadResult::Ready;
}
bool withSignatureInputRecovery(bool (*action)(void*), void* context)
{
    if (!action || !ensureMutex()) return false;
    Guard guard;
    if (!guard.locked || (session && (session->web_signature || session->web_confirmation))) return false;
    // Staging is separate from the Geocaching index. Serialize read-only input
    // with maintenance steps without requiring RF/network discovery to be idle.
    action(context);
    return true;
}
WebReadResult validWebConfirmation(Session::WebConfirmation& job)
{
    if (!router || job.media != storage::sd_media_session() || !storage::sd_card_ready() ||
        now(nullptr).monotonic_ms - job.started >= 120000) return WebReadResult::StaleIdentity;
    if (storage::sd_external_block_owner_active()) return WebReadResult::Pending;
    std::array<uint8_t, 64> author{};
    uint32_t generation = 0;
    const auto result = router->readDeviceAuthorKey(author.data(), generation);
    // The router uses a nonblocking lock. Unavailable cannot prove that the
    // account changed: retain the original task and retry within its deadline.
    if (result == chat::DeviceAuthorResult::Unavailable) return WebReadResult::Pending;
    if (result == chat::DeviceAuthorResult::StorageError) return WebReadResult::Unavailable;
    return result == chat::DeviceAuthorResult::Ok && generation == job.generation &&
                   !std::memcmp(author.data(), job.author.data(), 64)
               ? WebReadResult::Ready
               : WebReadResult::StaleIdentity;
}
WebReadResult queueWebConfirmation(const uint8_t transfer[16], const uint8_t owner[16], const uint8_t hash[32], uint32_t total,
                                   const uint8_t author[64], uint32_t generation, const uint8_t* proof, size_t proof_size)
{
    if (!transfer || !owner || !hash || !author || !generation || !total || total > 4166 || !proof || !proof_size || proof_size > 384) return WebReadResult::Invalid;
    gc::storage::PublicationConfirmationView decoded;
    if (!gc::storage::decodePublicationConfirmation({proof, proof_size}, decoded)) return WebReadResult::Invalid;
    if (!webLease(true, true)) return WebReadResult::Unavailable;
    Guard guard;
    if (!guard.locked || !session || !router) return WebReadResult::Pending;
    auto& s = *session;
    std::array<uint8_t, 32> proof_hash{};
    if (!s.crypto.sha256({proof, proof_size}, proof_hash.data())) return WebReadResult::Unavailable;
    if (s.web_confirmation)
    {
        const auto& job = *s.web_confirmation;
        if (std::memcmp(job.owner.data(), owner, 16)) return WebReadResult::Forbidden;
        return job.total == total && job.generation == generation && !std::memcmp(job.transfer.data(), transfer, 16) && !std::memcmp(job.hash.data(), hash, 32) &&
                       !std::memcmp(job.author.data(), author, 64) && job.proof_hash == proof_hash
                   ? WebReadResult::Ready
                   : WebReadResult::Conflict;
    }
    if (s.phase != Phase::Ready || !s.store || s.needsRecovery() || s.store->commitPending() || s.workspace_owner.holder() || downloadActive() || publicationActive() || draftSaveActive()) return WebReadResult::Pending;
    auto job = ::platform::memory::PsramPtr<Session::WebConfirmation>(::platform::memory::createPsram<Session::WebConfirmation>(*s.store, s.crypto));
    if (!job) return WebReadResult::Unavailable;
    job->source = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    job->capacity = std::max(size_t(320), size_t(total) + 128);
    job->work = static_cast<uint8_t*>(heap_caps_malloc(job->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    job->proof = static_cast<uint8_t*>(heap_caps_malloc(proof_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!job->source || !job->work || !job->proof) return WebReadResult::Unavailable;
    std::memcpy(job->transfer.data(), transfer, 16);
    std::memcpy(job->owner.data(), owner, 16);
    std::memcpy(job->hash.data(), hash, 32);
    std::memcpy(job->author.data(), author, 64);
    std::memcpy(job->proof, proof, proof_size);
    job->proof_hash = proof_hash;
    job->proof_size = proof_size;
    job->generation = generation;
    job->total = total;
    job->media = storage::sd_media_session();
    job->started = now(nullptr).monotonic_ms;
    s.web_confirmation = std::move(job);
    next_step.store(0);
    return WebReadResult::Ready;
}
void advanceWebConfirmation(Session& s)
{
    if (!(wanted.load() & 2))
    {
        s.web_confirmation.reset();
        return;
    }
    auto& job = *s.web_confirmation;
    const auto validity = validWebConfirmation(job);
    if (validity == WebReadResult::Pending) return;
    if (validity != WebReadResult::Ready)
    {
        Serial.printf("[Geocaching][WebConfirm] failed validity=%u\n", static_cast<unsigned>(validity));
        job.fail();
        return;
    }
    if (job.released) return;
    if (job.offset < job.total)
    {
        if (!job.file.is_open() && (!job.file.open("/trailmate/cache.confirm.upload", "r", job.media) || job.file.size() != job.total))
        {
            Serial.printf("[Geocaching][WebConfirm] failed input-open\n");
            job.fail();
            return;
        }
        const size_t count = std::min(size_t(320), size_t(job.total - job.offset));
        if (job.file.read(job.source + job.offset, count) != static_cast<int>(count))
        {
            Serial.printf("[Geocaching][WebConfirm] failed input-read\n");
            job.fail();
            return;
        }
        job.digest.update(job.source + job.offset, count);
        job.offset += count;
        return;
    }
    if (!job.begun)
    {
        job.file.close();
        if (!job.digest.finalize(job.work, 32) || std::memcmp(job.work, job.hash.data(), 32) ||
            !job.job.begin(job.transfer, {job.source, job.total}, {job.proof, job.proof_size}, {job.author.data(), 64}, now(nullptr), job.work, job.capacity))
        {
            Serial.printf("[Geocaching][WebConfirm] failed input-binding\n");
            job.fail();
            return;
        }
        job.begun = true;
    }
    const auto previous_phase = job.job.phase();
    job.job.advance();
    if (job.job.phase() == WebConfirmationPhase::Failed || job.job.phase() == WebConfirmationPhase::Cancelled)
    {
        Serial.printf("[Geocaching][WebConfirm] failed phase=%u\n", static_cast<unsigned>(previous_phase));
        // Diagnose a real rejected preparation with the existing payload and
        // scratch, after failure only. Never print key, content or proof bytes.
        if (previous_phase == WebConfirmationPhase::Prepare)
        {
            auto content = ::platform::memory::PsramPtr<gc::protocol::VerifiedRecordView>(::platform::memory::createPsram<gc::protocol::VerifiedRecordView>());
            if (content)
            {
                const auto record = gc::protocol::verifyGeocache({job.source, job.total}, s.crypto, job.work, job.capacity, *content);
                Serial.printf("[Geocaching][WebConfirm] record=%u author-match=%u\n", static_cast<unsigned>(record),
                              static_cast<unsigned>(record == gc::protocol::VerificationResult::Valid && !std::memcmp(content->record.author_public_key.data, job.author.data(), 64)));
                gc::storage::PublicationConfirmationView proof;
                if (record == gc::protocol::VerificationResult::Valid && gc::storage::decodePublicationConfirmation({job.proof, job.proof_size}, proof))
                {
                    gc::protocol::CmpReader raw(proof.raw);
                    size_t count = 0;
                    uint64_t version = 0;
                    gc::ByteView directory;
                    if (raw.array(count, 8) && count == 8 && raw.unsignedInteger(version) && version == 1 && raw.binary(directory, 16) && directory.size == 16)
                    {
                        gc::protocol::PublicationReceiptView receipt;
                        const auto verified = gc::storage::verifyPublicationConfirmation({job.proof, job.proof_size}, directory, *content, s.crypto, job.work, job.capacity, receipt);
                        Serial.printf("[Geocaching][WebConfirm] proof=%u\n", static_cast<unsigned>(verified));
                    }
                }
            }
        }
        job.fail();
    }
    else if (job.job.phase() == WebConfirmationPhase::Confirmed) job.releasePayload();
}
WebReadResult readWebConfirmation(const uint8_t transfer[16], const uint8_t owner[16], uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!transfer || !owner || !output || capacity < 192) return WebReadResult::Invalid;
    Guard guard;
    if (!guard.locked || !session || !session->web_confirmation) return WebReadResult::Pending;
    auto& job = *session->web_confirmation;
    if (std::memcmp(job.owner.data(), owner, 16)) return WebReadResult::Forbidden;
    if (std::memcmp(job.transfer.data(), transfer, 16)) return WebReadResult::NotFound;
    const auto validity = validWebConfirmation(job);
    if (validity != WebReadResult::Ready) return validity;
    const uint8_t phase = job.failed ? 2 : job.job.phase() == WebConfirmationPhase::Confirmed ? 1
                                                                                              : 0;
    gc::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(8) || !writer.binary({job.transfer.data(), 16}) || !writer.binary({job.hash.data(), 32}) || !writer.unsignedInteger(job.generation) || !writer.unsignedInteger(phase)) return WebReadResult::Invalid;
    if (phase == 1)
    {
        if (!writer.binary({job.job.cacheId().bytes.data(), 32}) || !writer.unsignedInteger(job.job.revision()) || !writer.binary({job.job.hash().bytes.data(), 32}) || !writer.unsignedInteger(1)) return WebReadResult::Invalid;
    }
    else if (!writer.nil() || !writer.nil() || !writer.nil() || !writer.unsignedInteger(0)) return WebReadResult::Invalid;
    written = writer.size();
    return WebReadResult::Ready;
}
WebReadResult releaseWebConfirmation(const uint8_t owner[16], const uint8_t* transfer)
{
    Guard guard;
    if (!guard.locked) return WebReadResult::Pending;
    if (!owner || !session || !session->web_confirmation) return WebReadResult::NotFound;
    const auto& job = *session->web_confirmation;
    if (std::memcmp(job.owner.data(), owner, 16)) return WebReadResult::Forbidden;
    if (transfer && std::memcmp(job.transfer.data(), transfer, 16)) return WebReadResult::NotFound;
    session->web_confirmation.reset();
    return WebReadResult::Ready;
}
bool workPending()
{
    if (!router || !board || !mutex) return false;
    if (!wanted.load() && !active.load()) return false;
    return static_cast<int32_t>(millis() - next_step.load()) >= 0;
}
void step()
{
    Guard guard;
    if (!guard.locked || !router || !board) return;
    // The shared owner already advances one bounded slice per scheduled tick
    // and yields its batch to chat/contacts. An extra per-slice timer makes
    // workPending() false inside that adapter, prematurely ending every batch.
    // Only genuinely idle/unavailable branches below need a retry deadline.
    next_step.store(millis());
    // Finish the small first-use volume header before closing; abandoning an
    // otherwise healthy initialization would leave an unrecoverable empty ledger.
    const bool finishing_initialization = session && session->phase >= Phase::Directories &&
                                          session->phase <= Phase::VerifyFormat && storage::sd_card_ready() && !storage::sd_external_block_owner_active();
    const bool recovering_download = session && session->phase == Phase::ResumeDownloads;
    if ((!wanted.load() && !finishing_initialization && !downloadActive() && !publicationActive() && !draftSaveActive() && !recovering_download) || restart.load())
    {
        if (closeSession()) restart.store(false);
        return;
    }
    if (!session)
    {
        session.reset(::platform::memory::createPsram<Session>());
        if (!session)
        {
            next_step.store(millis() + 2000);
            return;
        }
        active.store(true);
        ++epoch;
    }
    auto& s = *session;
    const bool close_detail = cancel_detail.exchange(false);
    s.storage_requested |= local_storage_requested.load();
    if (close_detail || s.pending_detail)
    {
        s.workspace_owner.release(s.detail.get());
        s.detail.reset();
        if (close_detail) s.pending_detail.reset();
        else s.detail = std::move(s.pending_detail);
    }
    advanceBrowse(s);
    if (!(wanted.load() & 2) && s.detail &&
        (s.detail->state == CacheDetail::State::LatestNetwork || s.detail->state == CacheDetail::State::LatestReady ||
         s.detail->state == CacheDetail::State::LatestFailed || s.detail->state == CacheDetail::State::LatestNotFound)) s.detail.reset();
    if (s.detail) s.detail->advanceNetwork(*router, s.local, s.crypto, now(nullptr).monotonic_ms);
    if (!s.storage_requested)
    {
        next_step.store(millis() + 100);
        return;
    }
    WorkspaceSlice workspace_slice{s};
    if (s.phase == Phase::Ready && s.web_confirmation)
    {
        advanceWebConfirmation(s);
        return;
    }
    if (s.phase == Phase::Ready && s.web_signature)
    {
        advanceWebSignature(s);
        return;
    }
    if (s.phase == Phase::Ready && s.download_store && s.download_store->missingSavedProjection(s.legacy_projection_cache))
    {
        s.legacy_projection_pending = true;
        if (s.projection_upgrade_admitted)
        {
            s.saved->releaseRead();
            s.local_read_only = false;
            s.phase = Phase::ResumeDownloads;
        }
    }
    if (s.phase == Phase::Ready && (s.draft_catalog_wanted || (wanted.load() & 2)) && !ensurePublicationStore(s))
    {
        next_step.store(millis() + 1000);
        return;
    }
    if (s.local_snapshot_attempted && (s.local_read_only || s.recovery))
    {
        if (!storage::sd_card_ready() || storage::sd_external_block_owner_active())
        {
            next_step.store(millis() + 1000);
            return;
        }
        if (s.local_read_only && (draftSaveActive() || downloadActive() || publicationActive() ||
                                  s.needsRecovery() || (s.saved && s.saved->error()) || (s.draft_catalog && s.draft_catalog->failed)))
        {
            // The audit and local readers share a frame lease. Ready details
            // own their response independently; release only their reader.
            s.workspace_owner.release(s.detail.get());
            if (s.detail && s.detail->state == CacheDetail::State::Ready) s.detail->saved_read.reset();
            else s.detail.reset();
            s.pending_detail.reset();
            if (s.store) s.store->releaseDraftRead();
            s.saved.reset();
            if (s.draft_catalog)
            {
                auto& catalog = *s.draft_catalog;
                catalog.reading = false;
                if (catalog.failed)
                {
                    catalog.ready = catalog.failed = false;
                    catalog.page.metadata_ready = false;
                    ++catalog.generation;
                }
            }
            s.draft_read.reset();
            s.store.reset();
            s.download_store.reset();
            s.local_read_only = false;
            startRecovery();
        }
        if (s.phase == Phase::Recover)
        {
            advanceStorageRecovery(s);
            return;
        }
    }
    if (s.detail && s.detail->state == CacheDetail::State::Saved &&
        (s.phase == Phase::Failed || !storage::sd_card_ready() || storage::sd_external_block_owner_active()))
        advanceSavedDetail(s);
    if (s.phase == Phase::Failed)
    {
        next_step.store(millis() + 2000);
        return;
    }
    if (s.needsRecovery())
    {
        fail("Storage interrupted - reopen to recover");
        return;
    }
    drainReply(s);
    releaseDraftReaders(s, cancel_draft_read.exchange(0));
    if (s.draft_io_reset)
    {
        if (s.store) s.store->releaseDraftRead();
        if (s.draft_catalog) s.draft_catalog->reading = false;
        s.publication_restore_pending = false;
        s.draft_io_reset = false;
    }
    if (s.draft_catalog && s.draft_catalog->reading &&
        (!s.draft_catalog_wanted || s.response || downloadActive() || publicationActive() || draftSaveActive()))
    {
        s.store->releaseDraftRead();
        s.draft_catalog->reading = false;
    }
    if (s.publication_restore_pending && s.publication_restore_background &&
        (s.response || downloadActive() || publicationActive() || draftSaveActive() ||
         (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)))
    {
        s.store->releaseDraftRead();
        s.publication_restore_pending = false;
    }
    if (s.download_restore_pending &&
        (s.response || downloadActive() || publicationActive() || draftSaveActive() ||
         (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)))
    {
        s.download_store->releaseRead();
        s.download_restore_pending = false;
    }
    if (s.saved && (s.response || downloadActive() || publicationActive() || draftSaveActive() || s.draft_catalog_wanted ||
                    (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending) ||
                    !storage::sd_card_ready() || storage::sd_external_block_owner_active()))
    {
        const auto before = s.saved->generation();
        s.saved->releaseRead();
        if (before != s.saved->generation()) ++epoch;
    }
    if (!storage::sd_card_ready() || storage::sd_external_block_owner_active())
    {
        if (!s.notice)
        {
            s.notice = "SD card unavailable";
            if (s.saved) s.saved->reset();
            ++epoch;
        }
        next_step.store(millis() + 1000);
        if (s.draft_read)
        {
            s.draft_read->status = ::ui::geocaching::DraftReadStatus::Failed;
            s.draft_io_reset = true;
        }
        return;
    }
    if (s.notice)
    {
        s.notice = nullptr;
        ++epoch;
    }
    // The active holder must run before new foreground work waiting for it.
    // Saved reply receipts share the lease with persistent operations.
    if (advanceCheckpoint(s)) return;
    if (s.detail && s.workspace_owner.heldBy(s.detail.get()) && advanceSavedDetail(s)) return;
    if (s.dispatcher && s.dispatch_store->busy() && s.workspace_owner.heldBy(s.dispatch_store.get()))
    {
        const auto sent = dispatchForeground(s);
        if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
            fail("Dispatch storage is blocked");
        return;
    }
    if (s.receipts && s.workspace_owner.heldBy(s.receipts.get()))
    {
        s.receipts->step();
        return;
    }
    if (s.phase == Phase::Ready && !s.workspace_owner.holder() && processResponse(s)) return;
    // Continue the current read owner before background catalogs that would
    // otherwise wait on its lease. Foreground jobs cancelled it above.
    if (s.saved && s.saved->reading())
    {
        const auto before = s.saved->generation();
        const bool worked = s.saved->advance();
        if (before != s.saved->generation()) ++epoch;
        if (worked) return;
    }
    if (s.download_restore_pending && resumeWaitingDownload(s)) return;
    if (s.publication_restore_pending && s.publication_restore_background)
    {
        const auto restored = restorePublication(s);
        if (restored == PublicationRestore::Invalid)
        {
            fail("Saved publication could not be recovered");
            return;
        }
        if (restored == PublicationRestore::Pending || restored == PublicationRestore::Restored) return;
    }
    if (s.draft_catalog && s.draft_catalog->reading && advanceDraftCatalog(s)) return;
    if (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)
    {
        auto& job = *s.draft_read;
        gc::ByteView bytes;
        gc::storage::DraftView draft;
        const auto result = s.store ? s.store->readDraft({job.id.data(), job.id.size()}, bytes) : DraftReadResult::Unavailable;
        if (result == DraftReadResult::Pending) return;
        if (result != DraftReadResult::Busy)
        {
            job.result = result;
            const bool valid = result == DraftReadResult::Ready && gc::storage::decodeDraft({job.id.data(), job.id.size()}, bytes, draft);
            if (valid)
            {
                job.bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.draft.read", bytes.size, false));
                if (job.bytes)
                {
                    std::memcpy(job.bytes, bytes.data, bytes.size);
                    job.size = bytes.size;
                }
            }
            if (s.store) s.store->releaseDraftRead();
            job.status = job.bytes ? ::ui::geocaching::DraftReadStatus::Ready : ::ui::geocaching::DraftReadStatus::Failed;
            return;
        }
        // Busy belongs to another job. Let its commit/dispatch advance below.
    }
    if (s.phase == Phase::Ready && draftSaveActive())
    {
        auto& job = *s.draft_save;
        if (job.web_file && advanceWebDraftFile(s)) return;
        if (job.web_job && !job.started)
        {
            uint8_t author[64];
            uint32_t generation = 0, expected_generation = 0;
            const auto result = router->readDeviceAuthorKey(author, generation);
            if (result == chat::DeviceAuthorResult::Unavailable) return;
            std::memcpy(&expected_generation, job.bytes + job.capacity - 100 + 64, sizeof(expected_generation));
            if (result != chat::DeviceAuthorResult::Ok || generation != expected_generation ||
                std::memcmp(author, job.bytes + job.capacity - 100, 64))
            {
                failWebDraftSave(job, result == chat::DeviceAuthorResult::StorageError ? WebReadResult::Unavailable : WebReadResult::StaleIdentity);
                return;
            }
        }
        if (job.erase_download)
        {
            if (advanceOfflineRemoval(s)) return;
        }
        else
        {
            const auto result = job.started ? s.store->stepCommit() : job.erase       ? s.store->deleteDraft({job.id.data(), job.id.size()}, job.expected)
                                                                  : job.editor_fields ? s.store->editDraft({job.id.data(), job.id.size()}, job.bytes, job.size, job.capacity, job.expected)
                                                                                      : s.store->saveDraft({job.id.data(), job.id.size()}, {job.bytes, job.size}, job.expected);
            if (result != JournalWriteResult::Busy)
            {
                job.started = true;
                if (result != JournalWriteResult::InProgress || s.store->inputConsumed())
                {
                    heap_caps_free(job.bytes);
                    job.bytes = nullptr;
                }
                if (result != JournalWriteResult::InProgress)
                {
                    job.done = true;
                    job.saved = result == JournalWriteResult::Verified;
                    if (job.saved) ++s.local_map_revision;
                    ++epoch;
                }
                return;
            }
        }
        // Advance the operation that currently owns storage before retrying.
    }
    if (s.phase == Phase::Ready && s.download_start && advanceDownloadStart(s)) return;
    if (advanceSavedDetail(s)) return;
    if (s.saved && s.saved->pending() && !s.draft_catalog_wanted && !(s.phase == Phase::ResumeDownloads && s.download_port) && !downloadActive() && !publicationActive() && !draftSaveActive() &&
        (!s.store || !s.store->commitPending()) && !s.needsRecovery())
    {
        const auto before = s.saved->generation();
        const bool worked = s.saved->advance();
        if (before != s.saved->generation()) ++epoch;
        if (worked) return;
    }
    if (!downloadActive() && !publicationActive() && !draftSaveActive() &&
        (!s.draft_read || s.draft_read->status != ::ui::geocaching::DraftReadStatus::Pending) &&
        s.store && !s.store->commitPending() && advanceDraftCatalog(s)) return;
    if (s.phase == Phase::Failed)
    {
        next_step.store(millis() + 2000);
        return;
    }
    switch (s.phase)
    {
    case Phase::Inspect:
    {
        const auto result = inspectSdVolume(s.volume);
        if (result == SdVolumeResult::Busy) return;
        if (result == SdVolumeResult::Ready) startRecovery();
        else if (result == SdVolumeResult::Missing) s.phase = Phase::CheckNew;
        else fail("Geocaching storage requires recovery");
        return;
    }
    case Phase::CheckNew:
        if (storage::sd_exists("/trailmate/geocaching/.state"))
        {
            fail("Interrupted storage initialization");
            return;
        }
        esp_fill_random(s.volume.data(), s.volume.size());
        s.phase = Phase::Directories;
        return;
    case Phase::Directories:
    {
        static constexpr const char* paths[] = {"/trailmate", "/trailmate/geocaching", "/trailmate/geocaching/caches",
                                                "/trailmate/geocaching/imports", "/trailmate/geocaching/exports", "/trailmate/geocaching/.state",
                                                "/trailmate/geocaching/.state/history", "/trailmate/geocaching/.state/journal",
                                                "/trailmate/geocaching/.state/checkpoint", "/trailmate/geocaching/.state/staging"};
        if (s.directory == sizeof(paths) / sizeof(paths[0]))
        {
            s.phase = Phase::OpenFormat;
            return;
        }
        if (s.mkdir_pending)
        {
            if (!storage::sd_mkdir(paths[s.directory]))
            {
                fail("Cannot create geocaching storage");
                return;
            }
            s.mkdir_pending = false;
            ++s.directory;
        }
        else if (storage::sd_is_directory(paths[s.directory])) ++s.directory;
        else s.mkdir_pending = true;
        return;
    }
    case Phase::OpenFormat:
        if (!s.format.open("/trailmate/geocaching/.state/format.bin", "w"))
        {
            fail("Cannot initialize storage");
            return;
        }
        s.phase = Phase::WriteFormat;
        return;
    case Phase::WriteFormat:
    {
        const auto header = gc::storage::encodeVolumeHeader(s.volume);
        if (s.format.write(header.data(), header.size()) != header.size())
        {
            fail("Storage write failed");
            return;
        }
        s.phase = Phase::FlushFormat;
        return;
    }
    case Phase::FlushFormat:
        if (!s.format.flush())
        {
            fail("Storage flush failed");
            return;
        }
        s.phase = Phase::CloseFormat;
        return;
    case Phase::CloseFormat:
        s.format.close();
        s.phase = Phase::VerifyFormat;
        return;
    case Phase::VerifyFormat:
    {
        gc::storage::VolumeInstance found;
        const auto result = inspectSdVolume(found);
        if (result == SdVolumeResult::Busy) return;
        if (result != SdVolumeResult::Ready || found != s.volume)
        {
            fail("Storage verification failed");
            return;
        }
        if (!wanted.load())
        {
            s.phase = Phase::Inspect;
            return;
        }
        startRecovery();
        return;
    }
    case Phase::Recover:
    {
        advanceStorageRecovery(s);
        return;
    }
    case Phase::ResumeDownloads:
    {
        if (s.legacy_projection_pending)
        {
            const auto result = s.download_store->upgradeSavedProjection(s.legacy_projection_cache);
            if (result == JournalWriteResult::InProgress || result == JournalWriteResult::Busy) return;
            if (result == JournalWriteResult::Unavailable && !s.download_store->needsRecovery())
            {
                next_step.store(millis() + 1000);
                return;
            }
            if (result != JournalWriteResult::Verified)
            {
                fail("Saved cache index upgrade failed - reopen to retry");
                return;
            }
            s.legacy_projection_pending = false;
            s.saved->reset();
            // Metadata backfill is not GPX cleanup. Resume normal read-only
            // browsing without creating download/network recovery services.
            s.local_snapshot_attempted = s.local_read_only = true;
            s.phase = Phase::Ready;
            s.status = "Saved storage ready";
            ++epoch;
            return;
        }
        if (s.download_port)
        {
            const auto result = s.download_port->poll();
            if (result == gc::DownloadOperationResult::Pending) return;
            if (result != gc::DownloadOperationResult::Complete && (!s.recovering_installed_download || s.download_store->needsRecovery()))
            {
                fail("Downloaded GPX recovery needs attention");
                return;
            }
            // A user-edited installed GPX or missing history remains untouched.
            // Keep its installed metadata available for browsing instead of
            // turning one offline file into a service outage.
            s.recovery_attention |= result != gc::DownloadOperationResult::Complete || s.download_port->historyPending();
            s.download_port.reset();
            s.saved->reset();
            ++epoch;
            return;
        }
        gc::storage::DownloadRecoveryRequest recovered;
        const auto selected = s.download_store->readRecovery(
            s.have_recovered_download ? gc::ByteView{s.recovered_download.data(), s.recovered_download.size()} : gc::ByteView{}, recovered);
        if (selected == DownloadRecoveryRead::Pending || selected == DownloadRecoveryRead::Busy) return;
        if (selected == DownloadRecoveryRead::Unavailable && !s.download_store->needsRecovery())
        {
            next_step.store(millis() + 1000);
            return;
        }
        if (selected != DownloadRecoveryRead::Ready && selected != DownloadRecoveryRead::End)
        {
            fail(selected == DownloadRecoveryRead::WorkspaceTooSmall ? "Insufficient download recovery workspace"
                 : selected == DownloadRecoveryRead::IoError         ? "Cannot read download recovery metadata"
                 : selected == DownloadRecoveryRead::VolumeChanged   ? "Download storage volume changed"
                                                                     : "Downloaded GPX metadata needs recovery");
            return;
        }
        if (selected == DownloadRecoveryRead::Ready)
        {
            gc::Destination local, remote;
            gc::RequestId request;
            std::memcpy(local.bytes.data(), recovered.key.data(), 16);
            std::memcpy(remote.bytes.data(), recovered.key.data() + 16, 16);
            std::memcpy(request.bytes.data(), recovered.key.data() + 32, 16);
            s.recovered_download = recovered.key;
            s.have_recovered_download = true;
            s.recovering_installed_download = recovered.installed;
            s.download_port.reset(::platform::memory::createPsram<SdDownloadPort<Digest>>(*s.download_store, s.crypto,
                                                                                          local, recovered.identity, recovered.task, recovered.created));
            if (!s.download_port || s.download_port->resume(remote, request) != gc::DownloadOperationResult::Pending)
                fail("Cannot resume downloaded GPX");
            return;
        }
        s.phase = Phase::Ready;
        s.status = "Saved storage ready";
        ++epoch;
        return;
    }
    case Phase::Ready:
        if (s.local_read_only)
        {
            next_step.store(millis() + 100);
            return;
        }
        break;
    case Phase::Failed:
        return;
    }
    if (s.needsRecovery())
    {
        fail("Storage interrupted - reopen to recover");
        return;
    }
    // Local lists, details, drafts and interrupted file installation above
    // do not need an online identity or request services. Only resume network
    // work once the real local destination is known.
    if (!s.client)
    {
        if (!startCheckpoint(s)) next_step.store(millis() + 100);
        return;
    }
    if (!s.dispatch_store)
        s.dispatch_store.reset(::platform::memory::createPsram<IndexedDispatchStore>(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                                     s.workspace_owner, s.workspace, s.frame, kFrameCapacity));
    if (s.dispatch_store && !s.dispatcher)
        s.dispatcher.reset(::platform::memory::createPsram<RequestDispatcher>(*router, *s.dispatch_store, 5000, 120000));
    if (!s.receipts)
        s.receipts.reset(::platform::memory::createPsram<StoredReplyReceipt>(s.volume, s.root, s.local, s.workspace_owner, s.crypto));
    if (!s.dispatcher || !s.receipts)
    {
        s.browse_status = "Waiting for network service memory";
        next_step.store(millis() + 1000);
        return;
    }
    if (publicationActive())
    {
        auto& job = *s.publication;
        if (job.draft_stage != Session::Publication::DraftStage::None)
        {
            advanceDraftPublication(s);
            return;
        }
        if (job.bytes)
        {
            auto* scratch = job.bytes + job.size;
            gc::protocol::VerifiedRecordView record;
            std::array<uint8_t, 64> author;
            if (gc::protocol::verifyGeocache({job.bytes, job.size}, s.crypto, scratch, job.size + 26, record) == gc::protocol::VerificationResult::Valid &&
                router->getGeocachingAuthorKey(author.data()) && !std::memcmp(author.data(), record.record.author_public_key.data, 64))
            {
                job.author = author;
                std::array<uint8_t, 16> task;
                if (job.web_operation) task = job.operation_id;
                const bool stop_requested = job.stop_requested;
                const auto restored = restorePublication(s, &record.id, &record.hash, &job.remote, job.web_operation ? &task : nullptr);
                if (restored == PublicationRestore::Restored)
                {
                    if (stop_requested)
                    {
                        s.publication->stop_requested = true;
                        s.publication->attempt->cancel();
                    }
                    return;
                }
                if (restored == PublicationRestore::Pending) return;
                if (restored == PublicationRestore::Invalid)
                {
                    job.error = "Saved publication could not be recovered";
                    heap_caps_free(job.bytes);
                    job.bytes = nullptr;
                    ++epoch;
                    return;
                }
                gc::RequestId request;
                if (job.web_operation) request.bytes = task;
                else
                {
                    esp_fill_random(task.data(), task.size());
                    esp_fill_random(request.bytes.data(), request.bytes.size());
                }
                job.port.reset(::platform::memory::createPsram<SdPublishPort>(*s.store, s.crypto, s.local, record.id, record.hash, task, now(nullptr)));
                if (job.port) job.attempt.reset(::platform::memory::createPsram<gc::PublishAttempt>(*job.port, s.crypto));
                if (job.attempt && !job.attempt->begin(job.remote, request, {job.bytes, job.size}, scratch, job.size + 26)) job.attempt.reset();
                if (job.attempt && stop_requested) job.attempt->cancel();
            }
            heap_caps_free(job.bytes);
            job.bytes = nullptr;
            job.started = now(nullptr).monotonic_ms;
            ++epoch;
            return;
        }
        const auto phase = job.attempt->phase();
        if (phase != gc::PublishAttemptPhase::Waiting)
        {
            job.attempt->advance();
            if (job.attempt->phase() == gc::PublishAttemptPhase::Confirmed)
            {
                ++s.local_map_revision;
                reportPublication("confirmed", *job.attempt);
            }
            ++epoch;
            return;
        }
        // Durable preparation and transport admission have their own bounded
        // wait. They must not consume the directory response budget.
        const auto timeout = job.submitted ? job.wait_ms : 120000;
        if (now(nullptr).monotonic_ms - job.started >= timeout && !s.store->commitPending())
        {
            job.error = job.submitted ? "Directory did not confirm; retry" : "Request could not be sent; retry";
            reportPublication(job.submitted ? "confirmation_timeout" : "submission_timeout", *job.attempt);
            job.attempt->cancel();
            ++epoch;
            return;
        }
    }
    if (!s.publication && !downloadActive() && !s.store->commitPending())
    {
        const auto restored = restorePublication(s);
        if (restored == PublicationRestore::Invalid)
        {
            fail("Saved publication could not be recovered");
            return;
        }
        if (restored == PublicationRestore::Restored || restored == PublicationRestore::Pending) return;
    }
    if (!s.download && !publicationActive() && !s.store->commitPending() && resumeWaitingDownload(s)) return;
    if (s.download && (s.download->phase() == gc::DownloadPhase::Submitting || s.download->phase() == gc::DownloadPhase::Installing ||
                       s.download->phase() == gc::DownloadPhase::Cancelling))
    {
        const auto before = s.download->phase();
        s.download->advance();
        if (before != s.download->phase())
        {
            // Queuing, waiting and cancelling requests do not change saved
            // rows. Installation may have committed even if cleanup failed.
            if (before == gc::DownloadPhase::Installing)
            {
                ++s.local_map_revision;
                if (s.saved) s.saved->reset();
            }
            ++epoch;
        }
        return;
    }
    if (processResponse(s)) return;
    if (s.download && s.download->phase() == gc::DownloadPhase::Waiting &&
        now(nullptr).monotonic_ms - s.download_started >= s.download_wait_ms)
    {
        s.download->cancel();
        ++epoch;
        return;
    }
    if (s.receipts && s.receipts->pending())
    {
        s.receipts->step();
        return;
    }
    if (publicationActive() || (s.download && s.download->phase() == gc::DownloadPhase::Waiting))
    {
        const auto sent = dispatchForeground(s);
        if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
            fail("Download storage is blocked");
        return;
    }

    if (!startCheckpoint(s)) next_step.store(millis() + 100);
}
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
