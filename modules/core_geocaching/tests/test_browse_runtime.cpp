#include "geocaching/protocol/get_request.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/query_response.h"
#include "geocaching/protocol/record_encoder.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/geocaching/indexed_publication_store.h"
#include "platform/esp/arduino_common/geocaching/sd_index_initialize.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_author_reservation.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_draft_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_new_task.h"
#include "platform/esp/arduino_common/geocaching/web_confirmation_job.h"
#include "runtime_environment.h"
#include "storage_owner.h"
#include "ui_presentation/geocaching/local_map_overlay.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rt = platform::esp::arduino_common::geocaching::browse_runtime;
namespace test = runtime_test;
using Section = ui::geocaching::Section;
uint32_t tick_ms = 5;
std::vector<uint8_t> last_reply;

void require(bool result, const char* message)
{
    if (!result)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
void tick()
{
    test::clock_ms += tick_ms;
    test::io_bytes = 0;
    test::in_ui = false;
    test::maintenance::tick(static_cast<uint32_t>(test::clock_ms));
    test::in_ui = true;
    require(test::io_bytes <= 512, "runtime exceeded the per-slice transfer budget");
}
template <class Predicate>
void until(Predicate ready, const char* message, bool force_profile = false)
{
    test::profile_reads = force_profile || std::getenv("TRAIL_MATE_TEST_IO_PROFILE") != nullptr;
    test::read_bytes_by_path.clear();
    const auto started = test::clock_ms;
    const auto begins = test::maintenance::begins;
    const auto operations = test::io_operations;
    unsigned ticks = 0;
    for (; ticks < 100000 && !ready(); ++ticks) tick();
    std::fprintf(stderr, "Runtime wait: %s; ticks=%u simulated_ms=%llu owner_begins=%llu io_operations=%llu\n", message, ticks,
                 static_cast<unsigned long long>(test::clock_ms - started),
                 static_cast<unsigned long long>(test::maintenance::begins - begins),
                 static_cast<unsigned long long>(test::io_operations - operations));
    if (test::profile_reads)
    {
        std::vector<std::pair<uint64_t, std::string>> reads;
        uint64_t total = 0;
        for (const auto& file : test::read_bytes_by_path)
        {
            reads.emplace_back(file.second, file.first);
            total += file.second;
        }
        std::sort(reads.rbegin(), reads.rend());
        std::fprintf(stderr, "Runtime read profile: total=%llu bytes distinct_files=%u\n", static_cast<unsigned long long>(total), static_cast<unsigned>(reads.size()));
        for (size_t i = 0; i < (force_profile ? reads.size() : std::min<size_t>(5, reads.size())); ++i)
            std::fprintf(stderr, "  %llu bytes %s\n", static_cast<unsigned long long>(reads[i].first), reads[i].second.c_str());
    }
    if (!ready())
    {
        ui::geocaching::Snapshot view;
        test::source->snapshot(Section::Discover, view);
        std::fprintf(stderr, "Runtime terminal status: %s\n", view.status.data());
        test::source->snapshot(Section::Published, view);
        std::fprintf(stderr, "Runtime storage status: %s\n", view.status.data());
        for (const auto& file : test::files)
            if (file.first.find("checkpoint/") != std::string::npos || file.first.find("staging/") != std::string::npos)
                std::fprintf(stderr, "Maintenance file: %s (%u bytes)\n", file.first.c_str(), static_cast<unsigned>(file.second.size()));
    }
    require(ready(), message);
}
ui::geocaching::Snapshot snapshot(Section section)
{
    test::in_ui = true;
    ui::geocaching::Snapshot value;
    test::source->snapshot(section, value);
    require(!test::ui_io, "UI callback touched storage");
    return value;
}
std::vector<uint8_t> fixture(const std::string& folder, const char* name)
{
    std::ifstream file(folder + '/' + name, std::ios::binary);
    require(file.good(), "fixture missing");
    return {(std::istreambuf_iterator<char>(file)), {}};
}
void announce(chat::MeshAdapterRouter& router)
{
    std::array<uint8_t, 16> destination{};
    std::array<uint8_t, 64> key{};
    std::vector<uint8_t> app{0x95, 1, 0xc4, 16};
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0xc4, 16});
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0, 0xa1, 'x'});
    router.announcement({{destination.data(), destination.size()}, {destination.data(), destination.size()}, {key.data(), key.size()}, {app.data(), app.size()}}, router.context);
}
void reply(chat::MeshAdapterRouter& router, std::vector<uint8_t> response, unsigned expected_operation)
{
    geocaching::protocol::CmpReader request({router.sent_bytes.data(), router.sent_bytes.size()});
    geocaching::protocol::CmpReader incoming({response.data(), response.size()});
    size_t count = 0;
    uint64_t version = 0, kind = 0, operation = 0;
    geocaching::ByteView id, response_id;
    require(request.array(count, 16) && request.unsignedInteger(version) && request.unsignedInteger(kind) &&
                request.unsignedInteger(operation) && operation == expected_operation && request.binary(id, 16),
            "unexpected outgoing request");
    require(incoming.array(count, 16) && incoming.unsignedInteger(version) && incoming.unsignedInteger(kind) &&
                incoming.unsignedInteger(operation) && operation == expected_operation && incoming.binary(response_id, 16),
            "invalid reply fixture");
    std::memcpy(response.data() + (response_id.data - response.data()), id.data, 16);
    std::array<uint8_t, 16> remote{};
    require(router.delivery != nullptr, "delivery callback missing");
    const auto semaphore = xSemaphoreCreateMutex();
    require(xSemaphoreTake(semaphore, 0) == pdTRUE, "cannot occupy the storage mutex during delivery");
    require(!router.delivery({{remote.data(), remote.size()}, {router.local.data(), router.local.size()}, {}, {response.data(), response.size()}}, router.context),
            "volatile reply acknowledged before persistence");
    // A duplicate while full must not overwrite the first owned payload or
    // acknowledge it early. Destroy borrowed network bytes before SD resumes.
    require(!router.delivery({{remote.data(), remote.size()}, {router.local.data(), router.local.size()}, {}, {response.data(), response.size()}}, router.context),
            "full receive mailbox acknowledged a volatile response");
    last_reply = response;
    std::fill(response.begin(), response.end(), 0xa5);
    xSemaphoreGive(semaphore);
}
std::vector<uint8_t> paginationFixture(const std::string& folder, bool final_page)
{
    auto response = fixture(folder, "query-response-v1.bin");
    geocaching::protocol::CmpReader envelope({response.data(), response.size()});
    size_t count = 0;
    uint64_t value = 0;
    geocaching::ByteView id;
    require(envelope.array(count, 6) && envelope.unsignedInteger(value) && envelope.unsignedInteger(value) &&
                envelope.unsignedInteger(value) && envelope.binary(id, 16),
            "pagination fixture envelope");
    geocaching::RequestId request;
    std::memcpy(request.bytes.data(), id.data, 16);
    geocaching::protocol::QueryPageView page;
    require(geocaching::protocol::decodeQueryPage({response.data(), response.size()}, request, 2048, 20, page), "pagination fixture page");
    if (!final_page)
    {
        const auto offset = page.encoded_items.data + page.encoded_items.size - response.data();
        require(response[offset] == 0xc0, "pagination fixture already has cursor");
        response.erase(response.begin() + offset);
        response.insert(response.begin() + offset, {0xc4, 1, 0x7c});
        return response;
    }
    std::vector<uint8_t> empty(128);
    geocaching::protocol::CmpWriter writer(empty.data(), empty.size());
    require(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(2) &&
                writer.binary({request.bytes.data(), 16}) && writer.unsignedInteger(200) && writer.array(4) &&
                writer.binary(page.snapshot_id) && writer.array(0) && writer.nil() && writer.unsignedInteger(page.remaining_ttl),
            "encode final page");
    empty.resize(writer.size());
    return empty;
}
std::vector<uint8_t> publicationReply(chat::MeshAdapterRouter& router, uint32_t revision)
{
    geocaching::protocol::CmpReader envelope({router.sent_bytes.data(), router.sent_bytes.size()});
    size_t count = 0;
    uint64_t value = 0;
    geocaching::ByteView bytes;
    require(envelope.array(count, 6) && envelope.unsignedInteger(value) && envelope.unsignedInteger(value) &&
                envelope.unsignedInteger(value) && value == 1 && envelope.binary(bytes, 16),
            "publication envelope missing");
    geocaching::RequestId request;
    std::memcpy(request.bytes.data(), bytes.data, 16);
    geocaching::protocol::PublishRequestView publication;
    geocaching::protocol::VerifiedRecordView record;
    std::array<uint8_t, 4166> scratch;
    platform::esp::common::EspGeocachingCrypto crypto;
    require(geocaching::protocol::decodePublishRequest({router.sent_bytes.data(), router.sent_bytes.size()}, request, publication) &&
                geocaching::protocol::verifyGeocache(publication.signed_cache, crypto, scratch.data(), scratch.size(), record) == geocaching::protocol::VerificationResult::Valid &&
                record.record.revision == revision,
            "published record has invalid signature or revision");
    std::vector<uint8_t> response(512);
    geocaching::protocol::CmpWriter writer(response.data(), response.size());
    require(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(1) &&
                writer.binary({request.bytes.data(), 16}) && writer.unsignedInteger(200) && writer.array(7) &&
                writer.binary({record.id.bytes.data(), 32}) && writer.unsignedInteger(revision) && writer.binary({record.hash.bytes.data(), 32}) &&
                writer.unsignedInteger(0) && writer.unsignedInteger(revision) && writer.binary({record.hash.bytes.data(), 32}) &&
                writer.unsignedInteger(static_cast<uint8_t>(record.record.state)),
            "could not encode publication receipt");
    response.resize(writer.size());
    return response;
}
void appendMaintenanceHistory(unsigned count)
{
    namespace sd = platform::esp::arduino_common::geocaching;
    namespace gc = geocaching::storage;
    test::in_ui = false;
    gc::VolumeInstance volume;
    require(sd::inspectSdVolume(volume) == sd::SdVolumeResult::Ready, "history volume missing");
    std::array<gc::IndexRootBytes, 2> roots;
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& bytes = test::files.at("/trailmate/geocaching/.state/index/root.h" + std::to_string(i));
        require(bytes.size() == roots[i].size(), "history root size");
        std::copy(bytes.begin(), bytes.end(), roots[i].begin());
    }
    gc::IndexRootView first, second, root;
    require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, first) &&
                gc::decodeIndexRoot({roots[1].data(), roots[1].size()}, volume, second) && gc::selectIndexRoot(first, second, root),
            "history root selection");
    unsigned copy = second.revision > first.revision ? 1 : 0;
    std::array<uint8_t, 8192> frame;
    std::array<uint8_t, 32> key;
    key.fill(0xcc);
    const uint8_t value[] = {0x92, 0x90, 0x90};
    const gc::MutationView row{11, {key.data(), key.size()}, {value, sizeof(value)}, false};
    for (unsigned transaction = 0; transaction < count; ++transaction)
    {
        sd::SdIndexedCommit commit(volume);
        require(commit.begin(root, copy, &row, 1, frame.data(), frame.size(), roots[1 - copy]), "history commit begin");
        auto status = sd::IndexedCommitStep::Working;
        for (unsigned i = 0; i < 100000 && status == sd::IndexedCommitStep::Working; ++i) status = commit.step();
        require(status == sd::IndexedCommitStep::Verified && commit.committed(root), "history commit failed");
        copy = 1 - copy;
    }
}
struct DetailText
{
    ui::geocaching::DetailStatus status = ui::geocaching::DetailStatus::Pending;
    std::string description, hint, error;
    bool can_archive = false;
};
DetailText detailText(const ui::geocaching::Item& item)
{
    test::in_ui = true;
    DetailText result;
    test::source->readDetail(
        item.id, item.revision_hash, [](const ui::geocaching::DetailView& value, void* context)
        {
        auto& out = *static_cast<DetailText*>(context);
        out.status = value.status;
        out.description = value.description;
        out.hint = value.hint;
        out.can_archive = value.can_archive;
        out.error = value.error; },
        &result);
    require(!test::ui_io, "detail UI callback touched SD");
    return result;
}
void checkMissingConfirmationHistory(const std::vector<uint8_t>& signed_record,
                                     const geocaching::protocol::VerifiedRecordView& content,
                                     geocaching::ByteView proof, geocaching::protocol::RecordCrypto& crypto)
{
    namespace sd = platform::esp::arduino_common::geocaching;
    namespace gc = geocaching::storage;
    // A distinct, genuinely initialized empty volume; restore the caller's
    // simulated card afterwards so its runtime leases remain unchanged.
    struct RestoreCard
    {
        decltype(test::files) files = std::move(test::files);
        decltype(test::directories) directories = std::move(test::directories);
        ~RestoreCard()
        {
            test::files = std::move(files);
            test::directories = std::move(directories);
        }
    } original;
    test::files.clear();
    test::directories.clear();
    test::directories.insert("/");
    gc::VolumeInstance volume{}, confirmed;
    volume.fill(71);
    require(sd::createNewSdVolume(volume, confirmed) == sd::SdVolumeResult::Ready && confirmed == volume, "fresh confirmation volume initialization failed");
    std::array<gc::IndexRootBytes, 2> roots;
    sd::SdIndexInitialize initialize(volume);
    require(initialize.begin(roots[0]), "fresh confirmation index initialization rejected");
    auto initialized = sd::IndexRootWriteStep::Working;
    for (unsigned i = 0; i < 100000 && initialized == sd::IndexRootWriteStep::Working; ++i) initialized = initialize.step();
    require(initialized == sd::IndexRootWriteStep::Verified, "fresh confirmation index initialization failed");
    roots[1] = roots[0];
    gc::IndexRootView root;
    require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root), "fresh confirmation root invalid");
    unsigned copy = 0;
    std::array<uint8_t, 8192> frame{}, verification{};
    std::vector<uint8_t> outgoing(signed_record.size() + 4096), request_bytes(signed_record.size() + 64);
    gc::QueuedRequestWorkspace workspace(outgoing.data(), outgoing.size());
    geocaching::RequestId request;
    request.bytes.fill(42);
    geocaching::Destination local{}, remote{};
    gc::PublicationConfirmationView decoded_proof;
    size_t count = 0;
    require(gc::decodePublicationConfirmation(proof, decoded_proof), "fresh confirmation proof envelope invalid");
    geocaching::protocol::CmpReader receipt(decoded_proof.raw);
    uint64_t version = 0;
    geocaching::ByteView delivery;
    require(receipt.array(count, 8) && receipt.unsignedInteger(version) && receipt.binary(delivery, 16) && delivery.size == 16, "fresh confirmation directory missing");
    std::memcpy(remote.bytes.data(), delivery.data, 16);
    size_t request_size = 0;
    require(geocaching::protocol::encodePublishRequest(request, content.record.encoded, content.signature, 8192, request_bytes.data(), request_bytes.size(), request_size), "fresh confirmation request encoding failed");
    const auto empty_files = test::files;
    std::vector<uint8_t> bad_proof(proof.data, proof.data + proof.size);
    bad_proof.back() ^= 1;
    sd::SdIndexedNewTask invalid(volume);
    require(!invalid.beginConfirmed(root, copy, local, remote, request, request.bytes, {request_bytes.data(), request_size}, {bad_proof.data(), bad_proof.size()}, {}, workspace, frame.data(), frame.size(), roots[1 - copy], crypto, verification.data(), verification.size()) && test::files == empty_files, "invalid history proof reached fresh storage");
    std::array<uint8_t, 48> request_key{};
    std::memcpy(request_key.data() + 16, remote.bytes.data(), 16);
    std::memcpy(request_key.data() + 32, request.bytes.data(), 16);
    gc::OutgoingView completed;
    completed.request = {request_bytes.data(), request_size};
    completed.state = 4;
    completed.task_id = {request.bytes.data(), 16};
    completed.terminal_data = proof;
    size_t compact_size = 0;
    require(gc::encodeOutgoing({request_key.data(), request_key.size()}, completed, outgoing.data(), outgoing.size(), compact_size), "compact confirmation fixture encode failed");
    gc::QueuedRequestWorkspace compact(outgoing.data(), compact_size);
    sd::SdIndexedNewTask bounded(volume);
    require(bounded.beginConfirmed(root, copy, local, remote, request, request.bytes, {request_bytes.data(), request_size}, proof, {}, compact, frame.data(), frame.size(), roots[1 - copy], crypto, verification.data(), verification.size()), "compact confirmation admission failed before recovery");
    auto bounded_status = sd::IndexedCommitStep::Working;
    for (unsigned i = 0; i < 100000 && bounded_status == sd::IndexedCommitStep::Working; ++i) bounded_status = bounded.step();
    require(bounded_status == sd::IndexedCommitStep::Invalid && test::files == empty_files, "insufficient recovery workspace wrote partial history");
    // An authentic network head must not introduce contradictory origin
    // metadata into an existing local author history. No task or draft writes.
    const auto empty_roots = roots;
    for (unsigned conflict = 0; conflict < 3; ++conflict)
    {
        if (conflict == 1 && content.record.revision == 1) continue;
        std::array<uint8_t, 36> prior_key{};
        std::memcpy(prior_key.data(), content.id.bytes.data(), 32);
        const uint32_t prior_revision = conflict == 1 ? 1 : 2;
        for (unsigned i = 0; i < 4; ++i) prior_key[32 + i] = static_cast<uint8_t>(prior_revision >> ((3 - i) * 8));
        std::array<uint8_t, 64> prior_author;
        std::memcpy(prior_author.data(), content.record.author_public_key.data, 64);
        if (conflict == 2) prior_author[0] ^= 1;
        gc::StoredTime prior_time;
        prior_time.has_utc = true;
        prior_time.utc_seconds = content.record.created_at + (conflict == 2 ? 0 : 1);
        geocaching::RevisionHash prior_hash;
        prior_hash.bytes.fill(71);
        std::array<uint8_t, 192> prior_bytes{};
        size_t prior_size = 0;
        const uint64_t origin = conflict == 1 ? UINT64_MAX : content.record.created_at + (conflict == 0 ? 1 : 0);
        require(gc::encodeAuthorIssued(prior_hash, {prior_author.data(), prior_author.size()}, prior_time, prior_bytes.data(), prior_bytes.size(), prior_size, origin), "conflicting origin fixture encoding failed");
        gc::MutationView mutation{3, {prior_key.data(), prior_key.size()}, {prior_bytes.data(), prior_size}, false};
        sd::SdIndexedCommit seed(volume);
        require(seed.begin(root, copy, &mutation, 1, frame.data(), frame.size(), roots[1 - copy], false, 1), "conflicting origin fixture commit rejected");
        auto seeded = sd::IndexedCommitStep::Working;
        for (unsigned i = 0; i < 100000 && seeded == sd::IndexedCommitStep::Working; ++i) seeded = seed.step();
        require(seeded == sd::IndexedCommitStep::Verified && seed.committed(root), "conflicting origin fixture commit failed");
        copy = 1 - copy;
        const auto before_rejection = test::files;
        sd::SdIndexedNewTask blocked(volume);
        require(blocked.beginConfirmed(root, copy, local, remote, request, request.bytes, {request_bytes.data(), request_size}, proof, {}, workspace, frame.data(), frame.size(), roots[1 - copy], crypto, verification.data(), verification.size()), "origin conflict should be checked by incremental storage reads");
        auto rejected = sd::IndexedCommitStep::Working;
        for (unsigned i = 0; i < 100000 && rejected == sd::IndexedCommitStep::Working; ++i) rejected = blocked.step();
        require(rejected == sd::IndexedCommitStep::Invalid && test::files == before_rejection, "conflicting local origin admitted partial recovered history");
        test::files = empty_files;
        roots = empty_roots;
        copy = 0;
        require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root), "empty root restoration failed after conflict test");
    }
    const auto restore = [&]
    {
        sd::SdIndexedNewTask job(volume);
        require(job.beginConfirmed(root, copy, local, remote, request, request.bytes, {request_bytes.data(), request_size}, proof, {}, workspace, frame.data(), frame.size(), roots[1 - copy], crypto, verification.data(), verification.size()), "fresh confirmation admission rejected");
        auto status = sd::IndexedCommitStep::Working;
        for (unsigned i = 0; i < 100000 && status == sd::IndexedCommitStep::Working; ++i) status = job.step();
        const auto revision = root.revision;
        require(status == sd::IndexedCommitStep::Verified && job.committed(root), "fresh confirmation restoration failed");
        if (root.revision != revision) copy = 1 - copy;
    };
    restore();
    const auto restored_files = test::files;
    restore();
    require(test::files == restored_files, "fresh confirmation retry wrote duplicate history");
    gc::DraftCatalogPage catalog_page;
    sd::SdIndexedDraftCatalog catalog(volume, crypto, verification.data(), verification.size());
    require(catalog.begin(root, 0, catalog_page, frame.data(), frame.size()), "restored history catalog read rejected");
    auto catalog_status = sd::IndexGetStep::Working;
    for (unsigned i = 0; i < 100000 && catalog_status == sd::IndexGetStep::Working; ++i) catalog_status = catalog.step();
    require(catalog_status == sd::IndexGetStep::Ready && catalog_page.count == 1 && catalog_page.rows[0].publication.confirmed_revision == content.record.revision,
            "restored history does not project its directory-confirmed version");
    gc::PublicationHistory device_history;
    sd::SdIndexedPublicationHistory history_reader(volume, crypto, verification.data(), verification.size());
    require(history_reader.begin(root, content.record.creation_nonce, 1, content.id, content.record.author_public_key, device_history, frame.data(), frame.size()), "restored device publication history read rejected");
    auto history_status = sd::IndexGetStep::Working;
    for (unsigned i = 0; i < 100000 && history_status == sd::IndexGetStep::Working; ++i) history_status = history_reader.step();
    require(history_status == sd::IndexGetStep::Ready && device_history.latest_revision == content.record.revision && device_history.confirmed_revision == content.record.revision &&
                device_history.created_at == content.record.created_at && device_history.latest_hash.bytes == content.hash.bytes,
            "restored device UI history cannot continue its signed head");
    std::array<uint8_t, 36> issued_key{};
    std::memcpy(issued_key.data(), content.id.bytes.data(), 32);
    for (unsigned i = 0; i < 4; ++i) issued_key[32 + i] = static_cast<uint8_t>(content.record.revision >> ((3 - i) * 8));
    sd::SdIndexGet issued_read(volume);
    require(issued_read.begin(root, 3, {issued_key.data(), issued_key.size()}, frame.data(), frame.size()), "restored issuance read rejected");
    auto read_status = sd::IndexGetStep::Working;
    for (unsigned i = 0; i < 100000 && read_status == sd::IndexGetStep::Working; ++i) read_status = issued_read.step();
    gc::AuthorIssuedView issued;
    require(read_status == sd::IndexGetStep::Ready && gc::decodeAuthorIssued({issued_key.data(), issued_key.size()}, issued_read.value(), issued) &&
                !std::memcmp(issued.revision_hash.data, content.hash.bytes.data(), 32) && issued.has_recovered_creation && issued.recovered_created_at == content.record.created_at,
            "fresh restoration omitted signed history");
    sd::SdIndexGet draft_read(volume);
    require(draft_read.begin(root, 4, content.record.creation_nonce, frame.data(), frame.size()), "restored draft read rejected");
    read_status = sd::IndexGetStep::Working;
    for (unsigned i = 0; i < 100000 && read_status == sd::IndexGetStep::Working; ++i) read_status = draft_read.step();
    gc::DraftView draft;
    require(read_status == sd::IndexGetStep::Ready && gc::decodeDraft(content.record.creation_nonce, draft_read.value(), draft) && draft.generation == 1 &&
                draft.author.size == 64 && !std::memcmp(draft.author.data, content.record.author_public_key.data, 64) && draft.base_hash.size == 32 &&
                !std::memcmp(draft.base_hash.data, content.hash.bytes.data(), 32) && draft.name == content.record.name,
            "fresh restoration omitted editable predecessor");
    auto successor = content.record;
    successor.revision++;
    successor.previous_hash = {content.hash.bytes.data(), 32};
    successor.updated_at++;
    std::vector<uint8_t> successor_bytes(content.record.encoded.size + 64);
    size_t successor_size = 0;
    require(geocaching::protocol::encodeGeocacheRecord(successor, successor_bytes.data(), successor_bytes.size(), successor_size), "restored successor encoding failed");
    gc::StoredTime issued_time;
    issued_time.has_utc = true;
    issued_time.utc_seconds = successor.updated_at;
    sd::SdIndexedAuthorReservation reservation(volume, crypto);
    require(reservation.beginReserve(root, copy, {successor_bytes.data(), successor_size}, issued_time, outgoing.data(), outgoing.size(), frame.data(), frame.size(), roots[1 - copy], content.record.creation_nonce, 1, true), "restored successor reservation rejected");
    auto reserved = sd::IndexedCommitStep::Working;
    for (unsigned i = 0; i < 100000 && reserved == sd::IndexedCommitStep::Working; ++i) reserved = reservation.step();
    if (reserved != sd::IndexedCommitStep::Verified) std::fprintf(stderr, "Restored predecessor revision=%u next reservation status=%u\n", content.record.revision, static_cast<unsigned>(reserved));
    require(reserved == sd::IndexedCommitStep::Verified && reservation.committed(root), "restored history cannot maintain its next version");
    copy = 1 - copy;
    sd::SdIndexGet advanced_draft(volume);
    require(advanced_draft.begin(root, 4, content.record.creation_nonce, frame.data(), frame.size()), "advanced restored draft read rejected");
    read_status = sd::IndexGetStep::Working;
    for (unsigned i = 0; i < 100000 && read_status == sd::IndexGetStep::Working; ++i) read_status = advanced_draft.step();
    require(read_status == sd::IndexGetStep::Ready, "advanced restored draft missing");
    const std::vector<uint8_t> preserved_draft(advanced_draft.value().data, advanced_draft.value().data + advanced_draft.value().size);
    request.bytes.fill(43);
    require(geocaching::protocol::encodePublishRequest(request, content.record.encoded, content.signature, 8192, request_bytes.data(), request_bytes.size(), request_size), "repeat historical confirmation encoding failed");
    restore();
    sd::SdIndexGet retained_draft(volume);
    require(retained_draft.begin(root, 4, content.record.creation_nonce, frame.data(), frame.size()), "retained restored draft read rejected");
    read_status = sd::IndexGetStep::Working;
    for (unsigned i = 0; i < 100000 && read_status == sd::IndexGetStep::Working; ++i) read_status = retained_draft.step();
    require(read_status == sd::IndexGetStep::Ready && retained_draft.value().size == preserved_draft.size() &&
                !std::memcmp(retained_draft.value().data, preserved_draft.data(), preserved_draft.size()),
            "historical confirmation overwrote later draft work");
}
void checkExternalConfirmationProjection(const std::vector<uint8_t>& signed_record, const geocaching::protocol::VerifiedRecordView& content, uint32_t generation)
{
    namespace sd = platform::esp::arduino_common::geocaching;
    namespace gc = geocaching::storage;
    using namespace geocaching;
    test::in_ui = false;
    gc::VolumeInstance volume;
    require(sd::inspectSdVolume(volume) == sd::SdVolumeResult::Ready, "confirmation volume missing");
    std::array<gc::IndexRootBytes, 2> roots;
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& bytes = test::files.at("/trailmate/geocaching/.state/index/root.h" + std::to_string(i));
        std::copy(bytes.begin(), bytes.end(), roots[i].begin());
    }
    gc::IndexRootView a, b, root;
    require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, a) && gc::decodeIndexRoot({roots[1].data(), roots[1].size()}, volume, b) && gc::selectIndexRoot(a, b, root), "confirmation root invalid");
    unsigned copy = b.revision > a.revision ? 1 : 0;
    platform::esp::common::EspGeocachingCrypto crypto;
    chat::MeshAdapterRouter directory;
    std::array<uint8_t, 8192> frame{}, verification{};
    std::array<uint8_t, 32> digest{};
    std::array<uint8_t, 16> delivery{}, epoch{}, operation{};
    epoch.fill(4);
    operation.fill(6);
    crypto.sha256({directory.identity.public_key.data(), 64}, digest.data());
    std::memcpy(verification.data() + 10, digest.data(), 16);
    constexpr char name[] = "lxmf.delivery";
    crypto.sha256({reinterpret_cast<const uint8_t*>(name), sizeof(name) - 1}, digest.data());
    std::memcpy(verification.data(), digest.data(), 10);
    crypto.sha256({verification.data(), 26}, digest.data());
    std::memcpy(delivery.data(), digest.data(), 16);
    std::array<uint8_t, 192> proof_raw{};
    protocol::CmpWriter proof_writer(proof_raw.data(), proof_raw.size());
    require(proof_writer.array(8) && proof_writer.unsignedInteger(1) && proof_writer.binary({delivery.data(), 16}) && proof_writer.binary({epoch.data(), 16}) &&
                proof_writer.binary({content.id.bytes.data(), 32}) && proof_writer.unsignedInteger(content.record.revision) && proof_writer.binary({content.hash.bytes.data(), 32}) &&
                proof_writer.unsignedInteger(static_cast<uint8_t>(content.record.state)) && proof_writer.unsignedInteger(1800000000),
            "confirmation proof encode failed");
    constexpr char domain[] = "trailmate.geocache/receipt/v1";
    std::memcpy(verification.data(), domain, sizeof(domain));
    std::memcpy(verification.data() + sizeof(domain), proof_raw.data(), proof_writer.size());
    std::array<uint8_t, 64> signature{};
    directory.identity.sign(verification.data(), sizeof(domain) + proof_writer.size(), signature.data());
    std::array<uint8_t, 384> proof{};
    size_t proof_size = 0;
    require(gc::encodePublicationConfirmation({{proof_raw.data(), proof_writer.size()}, {signature.data(), 64}, {directory.identity.public_key.data(), 64}}, proof.data(), proof.size(), proof_size), "stored proof encode failed");
    test::files["/trailmate/cache.confirm.upload"] = signed_record;
    std::array<uint8_t, 32> upload_hash{};
    require(crypto.sha256({signed_record.data(), signed_record.size()}, upload_hash.data()), "confirmation upload hash failed");
    std::array<uint8_t, 16> upload_id{}, upload_owner{};
    upload_id.fill(8);
    upload_owner.fill(9);
    until([&]
          { return rt::queueWebConfirmation(upload_id.data(), upload_owner.data(), upload_hash.data(), static_cast<uint32_t>(signed_record.size()), content.record.author_public_key.data, generation, proof.data(), proof_size) == rt::WebReadResult::Ready; },
          "runtime confirmation not admitted");
    size_t status_size = 0;
    test::busy_author_reads = 3;
    require(rt::readWebConfirmation(upload_id.data(), upload_owner.data(), verification.data(), verification.size(), status_size) == rt::WebReadResult::Pending,
            "transient author lock reported stale confirmation identity");
    until([&]
          { return test::busy_author_reads == 0; },
          "confirmation did not retry the unavailable author lock");
    until([&]
          {
        if(rt::readWebConfirmation(upload_id.data(),upload_owner.data(),verification.data(),verification.size(),status_size)!=rt::WebReadResult::Ready)return false;
        protocol::CmpReader status({verification.data(),status_size});size_t count=0;ByteView id,hash;uint64_t version=0,phase=0;
        require(status.array(count,8)&&status.binary(id,16)&&status.binary(hash,32)&&status.unsignedInteger(version)&&status.unsignedInteger(phase),"runtime confirmation status invalid");
        require(version==generation&&!std::memcmp(id.data,upload_id.data(),16)&&!std::memcmp(hash.data,upload_hash.data(),32),"runtime confirmation generation or upload binding changed");
        require(phase!=2,"runtime confirmation failed");
        if(phase!=1)return false;
        ByteView cache,revision_hash;uint64_t revision=0,durable=0;
        require(status.binary(cache,32)&&cache.size==32&&status.unsignedInteger(revision)&&status.binary(revision_hash,32)&&revision_hash.size==32&&status.unsignedInteger(durable)&&status.finished()&&
                !std::memcmp(cache.data,content.id.bytes.data(),32)&&revision==content.record.revision&&
                !std::memcmp(revision_hash.data,content.hash.bytes.data(),32)&&durable==1,"runtime confirmation is not the exact durable publication");
        return true; },
          "runtime confirmation not committed");
    auto stranger = upload_owner;
    stranger[0] ^= 1;
    require(rt::readWebConfirmation(upload_id.data(), stranger.data(), verification.data(), verification.size(), status_size) == rt::WebReadResult::Forbidden, "foreign session read confirmation result");
    require(rt::releaseWebConfirmation(upload_owner.data(), upload_id.data()) == rt::WebReadResult::Ready, "runtime confirmation release failed");
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& bytes = test::files.at("/trailmate/geocaching/.state/index/root.h" + std::to_string(i));
        std::copy(bytes.begin(), bytes.end(), roots[i].begin());
    }
    require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, a) && gc::decodeIndexRoot({roots[1].data(), roots[1].size()}, volume, b) && gc::selectIndexRoot(a, b, root), "runtime confirmation root invalid");
    copy = b.revision > a.revision ? 1 : 0;
    RequestId request;
    request.bytes = operation;
    std::array<uint8_t, 48> request_key{};
    std::memcpy(request_key.data() + 16, delivery.data(), 16);
    std::memcpy(request_key.data() + 32, operation.data(), 16);
    std::vector<uint8_t> request_bytes(signed_record.size() + 64), outgoing_bytes(signed_record.size() + 1024);
    size_t request_size = 0, outgoing_size = 0, task_size = 0;
    require(protocol::encodePublishRequest(request, content.record.encoded, content.signature, 8192, request_bytes.data(), request_bytes.size(), request_size), "confirmation request encode failed");
    gc::OutgoingView outgoing;
    outgoing.request = {request_bytes.data(), request_size};
    outgoing.state = 4;
    outgoing.task_id = {operation.data(), 16};
    outgoing.terminal_data = {proof.data(), proof_size};
    std::array<uint8_t, 256> task_bytes{};
    gc::TaskView task;
    task.kind = 1;
    task.state = 3;
    task.cache_id = {content.id.bytes.data(), 32};
    task.revision_hash = {content.hash.bytes.data(), 32};
    task.requests[0] = {request_key.data(), 48};
    task.request_count = 1;
    require(gc::encodeTask({operation.data(), 16}, task, task_bytes.data(), task_bytes.size(), task_size), "confirmation task encode failed");
    const auto commit = [&]
    {
        require(gc::encodeOutgoing({request_key.data(), 48}, outgoing, outgoing_bytes.data(), outgoing_bytes.size(), outgoing_size), "confirmation outgoing encode failed");
        const gc::MutationView rows[] = {{5, {request_key.data(), 48}, {outgoing_bytes.data(), outgoing_size}, false}, {10, {operation.data(), 16}, {task_bytes.data(), task_size}, false}};
        sd::SdIndexedCommit write(volume);
        require(write.begin(root, copy, rows, 2, frame.data(), frame.size(), roots[1 - copy]), "confirmation transaction rejected");
        auto status = sd::IndexedCommitStep::Working;
        for (unsigned i = 0; i < 100000 && status == sd::IndexedCommitStep::Working; ++i) status = write.step();
        require(status == sd::IndexedCommitStep::Verified && write.committed(root), "confirmation transaction failed");
        copy = 1 - copy;
    };
    Destination local, remote;
    remote.bytes = delivery;
    std::vector<uint8_t> encoded_workspace(8192);
    gc::QueuedRequestWorkspace workspace(encoded_workspace.data(), encoded_workspace.size());
    const auto confirmed_commit = [&]
    {
        sd::SdIndexedNewTask write(volume);
        require(write.beginConfirmed(root, copy, local, remote, request, operation, {request_bytes.data(), request_size}, {proof.data(), proof_size}, {}, workspace,
                                     frame.data(), frame.size(), roots[1 - copy], crypto, verification.data(), verification.size()),
                "verified confirmation admission failed");
        auto status = sd::IndexedCommitStep::Working;
        for (unsigned i = 0; i < 100000 && status == sd::IndexedCommitStep::Working; ++i) status = write.step();
        const auto revision = root.revision;
        require(status == sd::IndexedCommitStep::Verified && write.committed(root), "verified confirmation commit failed");
        if (root.revision != revision) copy = 1 - copy;
    };
    confirmed_commit();
    const auto original_files = test::files;
    confirmed_commit();
    require(test::files == original_files, "duplicate confirmation wrote another transaction");
    sd::IndexWorkspaceOwner owner;
    std::array<uint8_t, 512> response{};
    sd::IndexedPublicationStore store(volume, root, copy, roots[0], roots[1], owner, workspace, frame.data(), frame.size(), response.data(), response.size(), verification.data(), verification.size(), crypto);
    std::vector<uint8_t> job_work(std::max(size_t(320), signed_record.size() + 128));
    auto job_operation = operation;
    job_operation.fill(7);
    static_assert(sizeof(sd::WebConfirmationJob<sd::IndexedPublicationStore>) <= 512, "Confirmation job owns metadata only");
    const auto run_job = [&]
    {
        sd::WebConfirmationJob<sd::IndexedPublicationStore> job(store, crypto);
        require(job.begin(job_operation, {signed_record.data(), signed_record.size()}, {proof.data(), proof_size}, content.record.author_public_key, {}, job_work.data(), job_work.size()), "confirmation job begin rejected");
        for (unsigned i = 0; i < 100000 && job.phase() != sd::WebConfirmationPhase::Confirmed && job.phase() != sd::WebConfirmationPhase::Failed; ++i) job.advance();
        require(job.phase() == sd::WebConfirmationPhase::Confirmed && job.revision() == content.record.revision && job.hash().bytes == content.hash.bytes, "confirmation job failed");
        require(!owner.holder(), "confirmation job retained workspace lease");
    };
    run_job();
    const auto job_files = test::files;
    run_job();
    require(test::files == job_files, "confirmation job retry wrote another transaction");
    const auto read = [&](bool valid)
    {
        gc::DraftCatalogPage page;
        sd::SdIndexedDraftCatalog catalog(volume, crypto, verification.data(), verification.size());
        require(catalog.begin(root, 0, page, frame.data(), frame.size()), "confirmation catalog begin failed");
        auto status = sd::IndexGetStep::Working;
        for (unsigned i = 0; i < 100000 && status == sd::IndexGetStep::Working; ++i) status = catalog.step();
        if (valid) require(status == sd::IndexGetStep::Ready && page.count == 1 && page.rows[0].publication.confirmed_revision == content.record.revision, "signed directory proof did not project confirmed version");
        else require(status == sd::IndexGetStep::Invalid, "tampered directory proof was trusted by catalog");
    };
    read(true);
    checkMissingConfirmationHistory(signed_record, content, {proof.data(), proof_size}, crypto);
    // A later confirmed head must continue without reconstructing fake v1/v2
    // hashes. Its original creation time comes from this signed record/proof.
    auto later = content.record;
    later.revision = 3;
    later.updated_at += 2;
    std::array<uint8_t, 32> unknown_parent{};
    unknown_parent.fill(21);
    later.previous_hash = {unknown_parent.data(), unknown_parent.size()};
    std::array<uint8_t, 4096> later_raw{};
    size_t later_size = 0;
    require(protocol::encodeGeocacheRecord(later, later_raw.data(), later_raw.size(), later_size), "later recovery fixture encode failed");
    constexpr char sign_domain[] = "trailmate.geocache/sign/v1";
    std::memcpy(verification.data(), sign_domain, sizeof(sign_domain));
    std::memcpy(verification.data() + sizeof(sign_domain), later_raw.data(), later_size);
    std::array<uint8_t, 64> later_signature{};
    require(!std::memcmp(directory.identity.public_key.data(), later.author_public_key.data, 64) && directory.identity.sign(verification.data(), sizeof(sign_domain) + later_size, later_signature.data()), "later recovery fixture signer mismatch");
    std::vector<uint8_t> later_signed(later_size + 128);
    protocol::CmpWriter later_writer(later_signed.data(), later_signed.size());
    require(later_writer.array(2) && later_writer.binary({later_raw.data(), later_size}) && later_writer.binary({later_signature.data(), 64}), "later signed fixture encode failed");
    later_signed.resize(later_writer.size());
    protocol::VerifiedRecordView later_verified;
    require(protocol::verifyGeocache({later_signed.data(), later_signed.size()}, crypto, verification.data(), verification.size(), later_verified) == protocol::VerificationResult::Valid, "later recovery fixture signature invalid");
    std::array<uint8_t, 192> later_receipt{};
    protocol::CmpWriter later_proof(later_receipt.data(), later_receipt.size());
    require(later_proof.array(8) && later_proof.unsignedInteger(1) && later_proof.binary({delivery.data(), 16}) && later_proof.binary({epoch.data(), 16}) && later_proof.binary({later_verified.id.bytes.data(), 32}) &&
                later_proof.unsignedInteger(3) && later_proof.binary({later_verified.hash.bytes.data(), 32}) && later_proof.unsignedInteger(static_cast<uint8_t>(later.state)) && later_proof.unsignedInteger(1800000000),
            "later receipt fixture encode failed");
    std::memcpy(verification.data(), domain, sizeof(domain));
    std::memcpy(verification.data() + sizeof(domain), later_receipt.data(), later_proof.size());
    directory.identity.sign(verification.data(), sizeof(domain) + later_proof.size(), later_signature.data());
    std::array<uint8_t, 384> later_envelope{};
    size_t later_envelope_size = 0;
    require(gc::encodePublicationConfirmation({{later_receipt.data(), later_proof.size()}, {later_signature.data(), 64}, {directory.identity.public_key.data(), 64}}, later_envelope.data(), later_envelope.size(), later_envelope_size), "later proof envelope encode failed");
    checkMissingConfirmationHistory(later_signed, later_verified, {later_envelope.data(), later_envelope_size}, crypto);
    proof[proof_size - 1] ^= 1;
    sd::WebConfirmationJob<sd::IndexedPublicationStore> invalid_job(store, crypto);
    require(invalid_job.begin(job_operation, {signed_record.data(), signed_record.size()}, {proof.data(), proof_size}, content.record.author_public_key, {}, job_work.data(), job_work.size()), "invalid proof test admission failed");
    invalid_job.advance();
    require(invalid_job.phase() == sd::WebConfirmationPhase::Failed && test::files == job_files, "invalid proof job reached storage commit");
    commit();
    read(false);
}
int main(int argc, char** argv)
{
    require(argc == 2 || argc == 3, "expected fixture directory and optional runtime scenario");
    const bool close_on_save = argc == 3 && std::strcmp(argv[2], "save-close-detail") == 0;
    const bool web_latest = argc == 3 && std::strcmp(argv[2], "web-latest") == 0;
    const bool web_stop = argc == 3 && std::strcmp(argv[2], "web-stop") == 0;
    const bool web_signed_stop = argc == 3 && std::strcmp(argv[2], "web-signed-stop") == 0;
    const bool bad_legacy_signature = argc == 3 && std::strcmp(argv[2], "legacy-saved-bad-signature") == 0;
    const bool legacy_projected = argc == 3 && std::strcmp(argv[2], "legacy-saved-projected-index") == 0;
    const bool legacy_object_only = argc == 3 && std::strcmp(argv[2], "legacy-saved-object-index") == 0;
    const bool legacy_projection = bad_legacy_signature || legacy_projected || legacy_object_only || (argc == 3 && std::strcmp(argv[2], "legacy-saved-projection") == 0);
    const std::string folder(argv[1]);
    if (argc == 3 && std::strcmp(argv[2], "external-confirmation") == 0)
    {
        // Public capture only: verify the real USB author and existing native
        // directory receipt through the production indexed storage path.
        const auto signed_cache = fixture(folder, "signed-cache-v1.bin");
        const auto confirmation = fixture(folder, "publication-confirmation.bin");
        platform::esp::common::EspGeocachingCrypto crypto;
        std::vector<uint8_t> verification(8192);
        geocaching::protocol::VerifiedRecordView content;
        require(geocaching::protocol::verifyGeocache({signed_cache.data(), signed_cache.size()}, crypto,
                                                     verification.data(), verification.size(), content) == geocaching::protocol::VerificationResult::Valid,
                "captured USB signature invalid");
        checkMissingConfirmationHistory(signed_cache, content, {confirmation.data(), confirmation.size()}, crypto);
        std::fprintf(stderr, "Captured public signature/receipt: restore, duplicate, catalog and successor passed\n");
        return 0;
    }
    const auto encoded_record = fixture(folder, "record-v1.bin");
    geocaching::RecordView expected_record;
    require(geocaching::protocol::decodeGeocacheRecord({encoded_record.data(), encoded_record.size()}, expected_record), "detail record fixture invalid");
    std::fprintf(stderr, "Runtime: discovery/download/publication\n");
    chat::MeshAdapterRouter router;
    LoraBoard board;
    rt::configure(router, board);
    require(test::mutex_creates == 0, "configuration allocated a Geocaching mutex before use");
    if (argc == 3 && std::strcmp(argv[2], "signature-input-recovery") == 0)
    {
        unsigned calls = 0;
        const auto action = +[](void* opaque)
        {++*static_cast<unsigned*>(opaque);return true; };
        test::mutex_available = false;
        require(!rt::withSignatureInputRecovery(action, &calls) && calls == 0, "failed mutex allocation invoked recovery");
        test::mutex_available = true;
        require(rt::withSignatureInputRecovery(action, &calls) && calls == 1, "cold recovery requires a prior RF/SD session");
        bool nested_idle = true;
        require(rt::withSignatureInputRecovery(
                    +[](void* opaque)
                    {*static_cast<bool*>(opaque)=rt::sharedSignatureUploadIdle()==rt::WebReadResult::Ready;return false; },
                    &nested_idle) &&
                    !nested_idle,
                "recovery gate does not hold the production non-recursive mutex");
        require(!rt::workPending() && test::io_operations == 0, "read-only gate started RF/storage work");
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[2], "lazy-mutex") == 0)
    {
        test::mutex_available = false;
        test::source->activate(true);
        require(test::mutex_creates == 1 && !rt::workPending(), "failed lock allocation started work");
        ui::geocaching::Snapshot unavailable;
        test::source->snapshot(Section::Published, unavailable);
        require(std::strstr(unavailable.status.data(), "memory unavailable") != nullptr, "lock allocation failure was hidden");
        test::mutex_available = true;
        test::source->activate(true);
        require(test::mutex_creates == 2 && rt::workPending(), "lock allocation did not recover on explicit reopen");
        test::source->activate(true);
        require(test::mutex_creates == 2, "repeated activation allocated a second lock");
        test::source->activate(false);
        require(!rt::workPending(), "unused closed source retained work demand");
        return 0;
    }
    const bool restored_confirmation = argc == 3 && std::strcmp(argv[2], "web-signature-restored") == 0;
    if (argc == 3 && (std::strcmp(argv[2], "web-signature") == 0 || std::strcmp(argv[2], "web-signature-bound") == 0 || std::strcmp(argv[2], "web-signature-stale") == 0 || restored_confirmation))
    {
        const bool stale_draft = std::strcmp(argv[2], "web-signature-stale") == 0;
        const bool bound_draft = stale_draft || restored_confirmation || std::strcmp(argv[2], "web-signature-bound") == 0;
        router.ready = false;
        require(rt::webLease(true, true), "local signature storage lease unavailable");
        std::array<uint8_t, 16> operation{}, owner{}, transfer{}, nonce{};
        operation.fill(1);
        owner.fill(2);
        transfer.fill(3);
        nonce.fill(55);
        expected_record.author_public_key = {router.identity.public_key.data(), 64};
        expected_record.creation_nonce = {nonce.data(), 16};
        expected_record.revision = 1;
        expected_record.previous_hash = {};
        if (bound_draft)
        {
            test::source->activate(true);
            until([&]
                  { return snapshot(Section::Published).can_create; },
                  "bound signature draft storage unavailable");
            ui::geocaching::DraftInput draft;
            draft.id = nonce;
            draft.name = expected_record.name;
            draft.description = expected_record.description;
            draft.hint = expected_record.hint;
            draft.has_coordinates = true;
            draft.latitude_e7 = expected_record.latitude_e7;
            draft.longitude_e7 = expected_record.longitude_e7;
            draft.state = static_cast<uint8_t>(expected_record.state);
            draft.difficulty_x2 = expected_record.difficulty_x2;
            draft.terrain_x2 = expected_record.terrain_x2;
            draft.container_size = static_cast<uint8_t>(expected_record.container_size);
            require(test::source->saveDraft(draft), "bound signature draft save rejected");
            until([&]
                  { return test::source->draftSaveStatus(nonce, 0) == ui::geocaching::DraftSaveStatus::Saved; },
                  "bound signature draft not saved");
            test::source->activate(false);
        }
        std::array<uint8_t, 4096> record{}, scratch{};
        size_t size = 0;
        require(geocaching::protocol::encodeGeocacheRecord(expected_record, record.data(), record.size(), size), "local signature record encoding failed");
        std::array<uint8_t, 32> hash{};
        platform::esp::common::EspGeocachingCrypto crypto;
        require(crypto.sha256({record.data(), size}, hash.data()), "local signature hash failed");
        test::files["/trailmate/cache.sign.upload"] = std::vector<uint8_t>(record.data(), record.data() + size);
        const auto sends = router.sends;
        rt::WebReadResult admitted = rt::WebReadResult::Pending;
        until([&]
              { admitted = rt::queueWebSignature(operation.data(), owner.data(), transfer.data(), hash.data(), static_cast<uint32_t>(size),
                                                   router.identity.public_key.data(), router.author_generation, bound_draft ? nonce.data() : nullptr, bound_draft ? (stale_draft ? 2 : 1) : 0); return admitted != rt::WebReadResult::Pending; },
              "local signature admission never completed");
        require(admitted == rt::WebReadResult::Ready, "local signature admission rejected");
        require(rt::sharedSignatureUploadIdle() == rt::WebReadResult::Pending, "recovery exposed an active signature input");
        unsigned recovery_calls = 0;
        const auto recovery_action = +[](void* opaque)
        {++*static_cast<unsigned*>(opaque);return true; };
        require(!rt::withSignatureInputRecovery(recovery_action, &recovery_calls) && recovery_calls == 0,
                "protected recovery invoked a callback while signing");
        size_t written = 0, total = 0;
        until([&]
              {
            if (rt::readWebSignature(operation.data(), owner.data(), true, 0, 0, scratch.data(), scratch.size(), written) != rt::WebReadResult::Ready) return false;
            geocaching::protocol::CmpReader reader({scratch.data(), written}); size_t fields = 0; geocaching::ByteView id, bound;
            uint64_t generation = 0, phase = 0, bytes = 0;
            require(reader.array(fields, 5) && reader.binary(id, 16) && reader.binary(bound, 32) && reader.unsignedInteger(generation) && reader.unsignedInteger(phase) && reader.unsignedInteger(bytes), "local signature receipt invalid");
            if (stale_draft) { require(phase != 1, "stale draft unexpectedly signed"); total = static_cast<size_t>(bytes); return phase == 2; }
            require(phase != 2, "local signature failed"); total = static_cast<size_t>(bytes); return phase == 1; },
              "local signature never became ready");
        if (stale_draft)
        {
            require(router.sends == sends && router.identity.signs == 0 && total == 0, "rejected draft produced a signature or network traffic");
            require(rt::releaseWebSignature(owner.data(), operation.data()) == rt::WebReadResult::Ready, "rejected draft task release failed");
            std::puts("Stale draft generation cannot enter IP signature issuance");
            return 0;
        }
        std::vector<uint8_t> signed_record;
        while (signed_record.size() < total)
        {
            require(rt::readWebSignature(operation.data(), owner.data(), false, static_cast<uint32_t>(signed_record.size()), 320, scratch.data(), scratch.size(), written) == rt::WebReadResult::Ready, "signature chunk missing");
            geocaching::protocol::CmpReader reader({scratch.data(), written});
            size_t fields = 0;
            uint64_t offset = 0, bytes = 0;
            geocaching::ByteView id, bound, chunk;
            require(reader.array(fields, 5) && reader.binary(id, 16) && reader.unsignedInteger(offset) && reader.unsignedInteger(bytes) && reader.binary(bound, 32) && reader.binary(chunk, 320) && reader.finished(), "signature chunk invalid");
            require(offset == signed_record.size() && bytes == total, "signature chunk binding changed");
            signed_record.insert(signed_record.end(), chunk.data, chunk.data + chunk.size);
        }
        geocaching::protocol::VerifiedRecordView verified;
        require(geocaching::protocol::verifyGeocache({signed_record.data(), signed_record.size()}, crypto, scratch.data(), scratch.size(), verified) == geocaching::protocol::VerificationResult::Valid, "local signature crypto verification failed");
        require(router.sends == sends && router.identity.signs == 1 && !test::blocked_io, "local signing used network or accessed SD outside maintenance");
        auto stranger = owner;
        stranger[0] ^= 1;
        require(rt::readWebSignature(operation.data(), stranger.data(), false, 0, 320, scratch.data(), scratch.size(), written) == rt::WebReadResult::Forbidden, "foreign USB session read signature");
        if (!bound_draft)
        {
            ++router.author_generation;
            require(rt::readWebSignature(operation.data(), owner.data(), false, 0, 320, scratch.data(), scratch.size(), written) != rt::WebReadResult::Ready, "stale identity read signature");
        }
        require(rt::releaseWebSignature(owner.data(), operation.data()) == rt::WebReadResult::Ready, "local signature release failed");
        require(rt::sharedSignatureUploadIdle() == rt::WebReadResult::Ready, "released signature still blocks read-only recovery");
        bool nested_idle = true;
        require(rt::withSignatureInputRecovery(
                    +[](void* opaque)
                    {*static_cast<bool*>(opaque)=rt::sharedSignatureUploadIdle()==rt::WebReadResult::Ready;return false; },
                    &nested_idle),
                "released signing input requires unrelated RF/network readiness");
        require(!nested_idle, "runtime test mutex must reject nested acquisition like the physical device");
        require(rt::withSignatureInputRecovery(recovery_action, &recovery_calls) && recovery_calls == 1,
                "protected recovery did not invoke its callback after release");
        if (bound_draft)
        {
            until([&]
                  { return rt::queueWebSignature(operation.data(), owner.data(), transfer.data(), hash.data(), static_cast<uint32_t>(size),
                                                 router.identity.public_key.data(), router.author_generation, nonce.data(), 1) == rt::WebReadResult::Ready; },
                  "exact reservation replay not admitted");
            until([&]
                  {
                if (rt::readWebSignature(operation.data(), owner.data(), true, 0, 0, scratch.data(), scratch.size(), written) != rt::WebReadResult::Ready) return false;
                geocaching::protocol::CmpReader response({scratch.data(), written}); size_t count = 0; geocaching::ByteView id, hash;
                uint64_t version = 0, phase = 0, bytes = 0;
                require(response.array(count, 5) && response.binary(id, 16) && response.binary(hash, 32) && response.unsignedInteger(version) && response.unsignedInteger(phase) && response.unsignedInteger(bytes), "replay signature receipt invalid");
                require(phase != 2, "exact reservation replay rejected"); return phase == 1; },
                  "exact reservation replay signature unavailable");
            require(rt::releaseWebSignature(owner.data(), operation.data()) == rt::WebReadResult::Ready, "replay task release failed");
            std::vector<uint8_t> saved_draft;
            size_t draft_total = 0;
            do
            {
                rt::WebReadResult read = rt::WebReadResult::Pending;
                until([&]
                      { if (read == rt::WebReadResult::Pending) read = rt::readWebDraftChunk(nonce.data(), static_cast<uint32_t>(saved_draft.size()), 320, saved_draft.empty() ? 0 : 2, scratch.data(), scratch.size(), written); return read != rt::WebReadResult::Pending; },
                      "reserved draft cannot be read");
                if (read != rt::WebReadResult::Ready) std::fprintf(stderr, "Reserved draft read result=%u offset=%zu\n", static_cast<unsigned>(read), saved_draft.size());
                require(read == rt::WebReadResult::Ready, "reserved draft read failed");
                geocaching::protocol::CmpReader response({scratch.data(), written});
                size_t count = 0;
                uint64_t version = 0, bytes = 0, offset = 0;
                geocaching::ByteView id, chunk;
                require(response.array(count, 5) && response.binary(id, 16) && response.unsignedInteger(version) && response.unsignedInteger(bytes) && response.unsignedInteger(offset) && response.binary(chunk, 320) && response.finished(), "reserved draft receipt invalid");
                require(version == 2 && offset == saved_draft.size(), "reservation failed to advance draft generation");
                draft_total = static_cast<size_t>(bytes);
                saved_draft.insert(saved_draft.end(), chunk.data, chunk.data + chunk.size);
            } while (saved_draft.size() < draft_total);
            geocaching::storage::DraftView draft;
            require(geocaching::storage::decodeDraft({nonce.data(), 16}, {saved_draft.data(), saved_draft.size()}, draft) &&
                        draft.author.size == 64 && !std::memcmp(draft.author.data, router.identity.public_key.data(), 64) &&
                        draft.base_hash.size == 32 && !std::memcmp(draft.base_hash.data, verified.hash.bytes.data(), 32),
                    "author and reserved version were not persisted together");
            const auto signs_before_confirmation = router.identity.signs;
            if (restored_confirmation)
            {
                ++router.author_generation;
                tick();
            }
            checkExternalConfirmationProjection(signed_record, verified, router.author_generation);
            require(router.identity.signs == signs_before_confirmation && router.sends == sends,
                    "confirmed history restoration signed again or used RF");
        }
        require(rt::webLease(false), "local signature web lease release failed");
        tick();
        std::puts("Local IP-author signature reservation/export without RF passed");
        return 0;
    }
    if (argc == 3 && !close_on_save && !legacy_projection && !web_latest && !web_stop && !web_signed_stop)
    {
        const bool legacy_upgrade = std::strcmp(argv[2], "legacy-index-upgrade") == 0;
        const bool web_read = std::strcmp(argv[2], "web-draft-read") == 0;
        require(legacy_upgrade || web_read || std::strcmp(argv[2], "local-draft-only") == 0, "unknown runtime scenario");
        router.ready = false;
        test::in_ui = true;
        test::source->activate(true);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "empty offline card did not permit a local draft");
        ui::geocaching::DraftInput local;
        local.name = "Unpublished local draft";
        local.description = "Saved without any download or directory connection";
        local.has_coordinates = true;
        local.latitude_e7 = 310000000;
        local.longitude_e7 = 1210000000;
        require(test::source->saveDraft(local), "offline draft was not queued");
        until([&]
              { return test::source->draftSaveStatus(local.id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
              "offline draft was not saved");
        if (web_read)
        {
            require(rt::webLease(true), "web lease was not admitted");
            test::source->activate(false);
            std::array<uint8_t, 384> response;
            std::vector<uint8_t> encoded;
            size_t written = 0, total = 0;
            uint64_t generation = 0;
            require(rt::readWebDraftChunk(local.id.data(), 1, 16, 0, response.data(), response.size(), written) == rt::WebReadResult::Invalid,
                    "continuation without generation was accepted");
            do
            {
                bool ready = false;
                until([&]
                      { if (!ready) ready = rt::readWebDraftChunk(local.id.data(), static_cast<uint32_t>(encoded.size()), 16, generation, response.data(), response.size(), written) == rt::WebReadResult::Ready; return ready; },
                      "web draft read did not become ready");
                geocaching::protocol::CmpReader reader({response.data(), written});
                size_t fields = 0;
                uint64_t size = 0, offset = 0;
                geocaching::ByteView id, chunk;
                require(reader.array(fields, 5) && fields == 5 && reader.binary(id, 16) && reader.unsignedInteger(generation) &&
                            reader.unsignedInteger(size) && reader.unsignedInteger(offset) && reader.binary(chunk, 320) && reader.finished(),
                        "invalid web draft chunk shape");
                require(generation == 1 && offset == encoded.size() && !std::memcmp(id.data, local.id.data(), 16), "web draft correlation lost");
                total = static_cast<size_t>(size);
                encoded.insert(encoded.end(), chunk.data, chunk.data + chunk.size);
                if (encoded.size() == 16)
                {
                    // Consuming the same row in the UI must leave the web lease.
                    test::source->activate(true);
                    bool delivered = false;
                    require(test::source->readDraft(
                                local.id, [](const ui::geocaching::DraftInput& value, void* context)
                                { *static_cast<bool*>(context) = value.generation == 1; },
                                &delivered) == ui::geocaching::DraftReadStatus::Ready &&
                                delivered,
                            "UI could not share a web draft snapshot");
                    test::source->cancelDraftRead(local.id);
                    test::source->activate(false);
                }
            } while (encoded.size() < total);
            geocaching::storage::DraftView draft;
            require(geocaching::storage::decodeDraft({local.id.data(), local.id.size()}, {encoded.data(), encoded.size()}, draft) &&
                        draft.name == local.name && draft.description == local.description && draft.generation == 1,
                    "web chunks changed the persisted draft");
            bool conflicted = false;
            until([&]
                  { if (!conflicted) conflicted = rt::readWebDraftChunk(local.id.data(), 0, 16, 2, response.data(), response.size(), written) == rt::WebReadResult::Conflict; return conflicted; },
                  "stale web draft generation was accepted");
            until([&]
                  { return rt::readWebDraftCatalog(0, false, 0, response.data(), response.size(), written) == rt::WebReadResult::Ready; },
                  "web draft catalog did not become ready");
            geocaching::protocol::CmpReader catalog({response.data(), written});
            size_t fields = 0;
            uint64_t catalog_generation = 0, catalog_offset = 0, known_count = 0, row_generation = 0;
            geocaching::ByteView row_id;
            std::string_view row_name;
            require(catalog.array(fields, 4) && fields == 4 && catalog.unsignedInteger(catalog_generation) &&
                        catalog.unsignedInteger(catalog_offset) && catalog.unsignedInteger(known_count) &&
                        catalog.array(fields, 9) && fields == 9 && catalog.binary(row_id, 16) &&
                        catalog.unsignedInteger(row_generation) && catalog.nil() && catalog.text(row_name, 96),
                    "invalid web catalog projection");
            require(catalog_offset == 0 && known_count == 1 && row_generation == 1 && row_name == local.name &&
                        row_id.size == 16 && !std::memcmp(row_id.data, local.id.data(), 16),
                    "web catalog changed the saved draft");
            require(rt::readWebDraftCatalog(0, true, catalog_generation + 1, response.data(), response.size(), written) == rt::WebReadResult::Conflict,
                    "web catalog accepted a stale storage generation");
            require(rt::readWebDraftCatalog(1, true, catalog_generation, response.data(), response.size(), written) == rt::WebReadResult::Ready,
                    "web catalog end marker was unavailable");
            geocaching::protocol::CmpReader end({response.data(), written});
            require(end.array(fields, 4) && end.unsignedInteger(generation) && end.unsignedInteger(catalog_offset) &&
                        end.unsignedInteger(known_count) && end.nil() && end.finished() && catalog_offset == 1 && known_count == 1,
                    "web catalog fabricated an extra row");
            draft.name = "Edited through shared transaction";
            draft.generation = 2;
            std::array<uint8_t, 1024> edit;
            size_t edit_size = 0;
            std::array<uint8_t, 64> forged_author{};
            draft.author = {forged_author.data(), forged_author.size()};
            require(geocaching::storage::encodeDraft({local.id.data(), 16}, draft, edit.data(), edit.size(), edit_size), "forged author test encoding failed");
            require(!rt::queueDraftSave(local.id.data(), edit.data(), edit_size, 1, true), "editor command accepted caller author metadata");
            draft.author = {};
            require(geocaching::storage::encodeDraft({local.id.data(), 16}, draft, edit.data(), edit.size(), edit_size), "edit encoding failed");
            require(!rt::queueDraftSave(local.id.data(), edit.data(), edit_size, 0, true), "editor command accepted inconsistent generation");
            require(rt::queueDraftSave(local.id.data(), edit.data(), edit_size, 1, true), "shared editor transaction not queued");
            require(!rt::queueDraftSave(local.id.data(), edit.data(), edit_size, 1, true), "parallel edit replaced the active save job");
            until([&]
                  { return test::source->draftSaveStatus(local.id, 1) == ui::geocaching::DraftSaveStatus::Saved; },
                  "shared editor transaction was not persisted");
            bool edited = false;
            until([&]
                  { if (!edited) edited = rt::readWebDraftChunk(local.id.data(), 0, 320, 2, response.data(), response.size(), written) == rt::WebReadResult::Ready; return edited; },
                  "edited draft was not readable at the next generation");
            auto permission = [&](const uint8_t* key, uint64_t expected, const uint8_t* author)
            {
                auto result = rt::WebReadResult::Pending;
                until([&]
                      { if (result == rt::WebReadResult::Pending) result = rt::checkWebDraftEditOwner(key, expected, author); return result != rt::WebReadResult::Pending; },
                      "stored draft ownership did not become available");
                return result;
            };
            require(permission(local.id.data(), 2, forged_author.data()) == rt::WebReadResult::Ready,
                    "unbound draft was not editable");
            std::array<uint8_t, 16> bound_id{};
            bound_id.fill(5);
            std::array<uint8_t, 32> retained_base{};
            retained_base.fill(6);
            forged_author.fill(7);
            require(permission(bound_id.data(), 0, forged_author.data()) == rt::WebReadResult::Ready,
                    "new draft generation zero was not admitted");
            require(permission(bound_id.data(), 1, forged_author.data()) == rt::WebReadResult::Conflict,
                    "missing draft with a nonzero generation was admitted");
            draft.generation = 1;
            draft.author = {forged_author.data(), forged_author.size()};
            draft.base_hash = {retained_base.data(), retained_base.size()};
            require(geocaching::storage::encodeDraft({bound_id.data(), 16}, draft, edit.data(), edit.size(), edit_size) &&
                        rt::queueDraftSave(bound_id.data(), edit.data(), edit_size, 0),
                    "bound test draft was not queued");
            until([&]
                  { return test::source->draftSaveStatus(bound_id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
                  "bound test draft was not stored");
            require(permission(bound_id.data(), 1, forged_author.data()) == rt::WebReadResult::Ready, "original author was denied");
            auto wrong_author = forged_author;
            wrong_author[0] ^= 1;
            require(permission(bound_id.data(), 1, wrong_author.data()) == rt::WebReadResult::Forbidden, "different author was admitted");
            require(permission(bound_id.data(), 0, forged_author.data()) == rt::WebReadResult::Conflict, "stale generation was admitted");
            draft.generation = 2;
            draft.name = "Bound metadata survives browser fields";
            draft.author = draft.base_hash = {};
            require(geocaching::storage::encodeDraft({bound_id.data(), 16}, draft, edit.data(), edit.size(), edit_size) &&
                        rt::queueDraftSave(bound_id.data(), edit.data(), edit_size, 1, true),
                    "bound editor save was not queued");
            until([&]
                  { return test::source->draftSaveStatus(bound_id, 1) == ui::geocaching::DraftSaveStatus::Saved; },
                  "bound editor save was not persisted");
            bool bound_read = false;
            until([&]
                  { if (!bound_read) bound_read = rt::readWebDraftChunk(bound_id.data(), 0, 320, 2, response.data(), response.size(), written) == rt::WebReadResult::Ready; return bound_read; },
                  "bound edit could not be reread");
            geocaching::protocol::CmpReader bound_response({response.data(), written});
            geocaching::ByteView returned_id, retained_chunk;
            uint64_t retained_total = 0, retained_offset = 0;
            geocaching::storage::DraftView stored;
            require(bound_response.array(fields, 5) && bound_response.binary(returned_id, 16) && bound_response.unsignedInteger(generation) &&
                        bound_response.unsignedInteger(retained_total) && bound_response.unsignedInteger(retained_offset) &&
                        bound_response.binary(retained_chunk, 320) && bound_response.finished() && retained_total == retained_chunk.size &&
                        geocaching::storage::decodeDraft({bound_id.data(), 16}, retained_chunk, stored) && stored.generation == 2 &&
                        stored.name == draft.name && stored.author.size == 64 && stored.base_hash.size == 32 &&
                        !std::memcmp(stored.author.data, forged_author.data(), 64) && !std::memcmp(stored.base_hash.data, retained_base.data(), 32),
                    "browser field editing lost committed author or base hash");
            std::array<uint8_t, 32> edit_hash{};
            chat::reticulum::fullHash(edit.data(), edit_size, edit_hash.data());
            auto replay = [&](const uint8_t* author, const uint8_t* hash)
            {
                auto result = rt::WebReadResult::Pending;
                until([&]
                      { if (result == rt::WebReadResult::Pending) result = rt::checkWebDraftEditOwner(bound_id.data(), 1, author, hash); return result != rt::WebReadResult::Pending; },
                      "saved edit retry did not become available");
                return result;
            };
            require(replay(forged_author.data(), edit_hash.data()) == rt::WebReadResult::AlreadySaved,
                    "exact saved edit was not recognised after identity metadata restoration");
            edit_hash[0] ^= 1;
            require(replay(forged_author.data(), edit_hash.data()) == rt::WebReadResult::Conflict,
                    "different content at the same generation was accepted as a saved retry");
            edit_hash[0] ^= 1;
            require(replay(wrong_author.data(), edit_hash.data()) == rt::WebReadResult::Forbidden,
                    "saved retry bypassed author ownership");
            rt::webLease(false);
            until([&]
                  { return test::allocations.empty(); },
                  "closed web lease retained session payloads");
            require(replay(forged_author.data(), edit_hash.data()) == rt::WebReadResult::AlreadySaved,
                    "reopened storage forgot a previously saved edit");
            std::string web_description(650, 'w');
            draft.generation = 1;
            draft.name = "Staged web edit";
            draft.description = web_description;
            draft.author = draft.base_hash = {};
            std::array<uint8_t, 16> staged_id{};
            staged_id.fill(9);
            require(geocaching::storage::encodeDraft({staged_id.data(), 16}, draft, edit.data(), edit.size(), edit_size), "web file encoding failed");
            chat::reticulum::fullHash(edit.data(), edit_size, edit_hash.data());
            test::files[rt::kWebDraftCandidatePath] = std::vector<uint8_t>(edit.data(), edit.data() + edit_size);
            auto staged = [&](const std::array<uint8_t, 16>& id, uint32_t author_generation)
            {
                auto result = rt::WebReadResult::Pending;
                until([&]
                      { if (result == rt::WebReadResult::Pending) result = rt::queueWebDraftFile(id.data(), static_cast<uint32_t>(edit_size), 0, edit_hash.data(), router.identity.public_key.data(), author_generation); return result != rt::WebReadResult::Pending; },
                      "web file save did not reach a terminal result");
                return result;
            };
            require(staged(staged_id, 7) == rt::WebReadResult::AlreadySaved, "web file was not saved through the existing editor transaction");
            require(permission(staged_id.data(), 1, router.identity.public_key.data()) == rt::WebReadResult::Ready, "web file save generation was not persisted");
            staged_id.fill(10);
            test::files[rt::kWebDraftCandidatePath][0] ^= 1;
            require(staged(staged_id, 7) == rt::WebReadResult::HashMismatch, "corrupted web candidate was committed");
            require(permission(staged_id.data(), 0, router.identity.public_key.data()) == rt::WebReadResult::Ready, "hash failure created a draft");
            test::files[rt::kWebDraftCandidatePath][0] ^= 1;
            staged_id.fill(11);
            require(rt::queueWebDraftFile(staged_id.data(), static_cast<uint32_t>(edit_size), 0, edit_hash.data(), router.identity.public_key.data(), 7) == rt::WebReadResult::Pending,
                    "identity-change test could not stage its candidate");
            router.author_generation = 8;
            require(staged(staged_id, 7) == rt::WebReadResult::StaleIdentity, "queued web save ignored an author generation change");
            router.author_generation = 7;
            require(permission(staged_id.data(), 0, router.identity.public_key.data()) == rt::WebReadResult::Ready, "stale identity created a draft");
            staged_id.fill(12);
            draft.author = {forged_author.data(), forged_author.size()};
            require(geocaching::storage::encodeDraft({staged_id.data(), 16}, draft, edit.data(), edit.size(), edit_size), "invalid metadata file encoding failed");
            chat::reticulum::fullHash(edit.data(), edit_size, edit_hash.data());
            test::files[rt::kWebDraftCandidatePath] = std::vector<uint8_t>(edit.data(), edit.data() + edit_size);
            require(staged(staged_id, 7) == rt::WebReadResult::Invalid, "web file accepted caller author metadata");
            require(permission(staged_id.data(), 0, router.identity.public_key.data()) == rt::WebReadResult::Ready, "invalid metadata created a draft");
            rt::webLease(false);
            until([&]
                  { return test::allocations.empty(); },
                  "reopened retry retained session payloads");
            // Match HostLink draft_put: every Busy retry first rechecks the
            // committed owner, then (only if Ready) advances the same intent.
            // Separate queue/owner tests miss read jobs starving this commit.
            staged_id.fill(13);
            draft.author = draft.base_hash = {};
            draft.description = std::string_view(web_description);
            require(geocaching::storage::encodeDraft({staged_id.data(), 16}, draft, edit.data(), edit.size(), edit_size), "interleaved web file encoding failed");
            chat::reticulum::fullHash(edit.data(), edit_size, edit_hash.data());
            test::files[rt::kWebDraftCandidatePath] = std::vector<uint8_t>(edit.data(), edit.data() + edit_size);
            bool interleaved_saved = false;
            until([&]
                  {
                      if (interleaved_saved) return true;
                      const auto owner = rt::checkWebDraftEditOwner(staged_id.data(), 0, router.identity.public_key.data(), edit_hash.data());
                      if (owner == rt::WebReadResult::AlreadySaved) return interleaved_saved = true;
                      require(owner == rt::WebReadResult::Pending || owner == rt::WebReadResult::Ready, "interleaved owner check rejected its own edit");
                      if (owner == rt::WebReadResult::Ready)
                      {
                          const auto queued = rt::queueWebDraftFile(staged_id.data(), static_cast<uint32_t>(edit_size), 0, edit_hash.data(), router.identity.public_key.data(), 7);
                          require(queued == rt::WebReadResult::Pending || queued == rt::WebReadResult::AlreadySaved, "interleaved save rejected its own intent");
                      }
                      return false; },
                  "interleaved HostLink save did not confirm the committed row");
            rt::webLease(false);
            until([&]
                  { return test::allocations.empty(); },
                  "interleaved save retained session payloads");
            require(test::ui_io == 0, "web/UI projection performed direct storage I/O");
            return 0;
        }
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "draft editor session did not close");
        namespace gc = geocaching::storage;
        namespace sd = platform::esp::arduino_common::geocaching;
        gc::VolumeInstance volume;
        const auto& format = test::files.at("/trailmate/geocaching/.state/format.bin");
        require(gc::decodeVolumeHeader({format.data(), format.size()}, volume) == gc::VolumeFormatResult::Supported, "legacy fixture volume");
        const auto selected_root = [&]
        {
            gc::IndexRootView first, second, selected;
            const auto& a = test::files.at("/trailmate/geocaching/.state/index/root.h0");
            const auto& b = test::files.at("/trailmate/geocaching/.state/index/root.h1");
            // Maintenance publishes each metadata copy in bounded writes.
            // A partially written copy is not a completed rotation yet.
            if (!gc::decodeIndexRoot({a.data(), a.size()}, volume, first) || !gc::decodeIndexRoot({b.data(), b.size()}, volume, second) ||
                !gc::selectIndexRoot(first, second, selected)) return gc::IndexRootView{};
            return selected;
        };
        if (legacy_upgrade)
        {
            const auto root = selected_root();
            require(gc::validIndexRoot(root), "invalid legacy upgrade roots");
            for (unsigned bucket = 0; bucket < 256; ++bucket)
            {
                if (!gc::indexHasShard(root, 4, static_cast<uint8_t>(bucket))) continue;
                char paths[2][96];
                gc::IndexShardHead heads[2], head;
                for (unsigned copy = 0; copy < 2; ++copy)
                {
                    require(sd::indexShardHeadPathForBucket(root.slot, 4, static_cast<uint8_t>(bucket), copy, paths[copy], sizeof(paths[copy])), "legacy fixture head path");
                    const auto& bytes = test::files.at(paths[copy]);
                    require(gc::decodeIndexShardHead({bytes.data(), bytes.size()}, volume, root.epoch, 4, static_cast<uint8_t>(bucket), heads[copy]), "legacy fixture head");
                }
                require(gc::selectIndexShardHead(heads[0], heads[1], root.sequence, head) && head.current_only, "legacy fixture selected head");
                char current[96], legacy[96];
                require(sd::indexShardDataPath(root.slot, head, current, sizeof(current)), "legacy fixture current path");
                head.current_only = false;
                require(sd::indexShardDataPath(root.slot, head, legacy, sizeof(legacy)), "legacy fixture append path");
                test::files[legacy] = test::files.at(current);
                gc::IndexShardHeadBytes bytes;
                require(gc::encodeIndexShardHead(volume, head, bytes), "legacy fixture encoding");
                for (const auto& path : paths) test::files[path] = {bytes.begin(), bytes.end()};
            }
        }
        const auto saved_files = test::files;
        // Reproduce the L2 heap snapshot: PSRAM is available, internal heap is
        // below the unrelated 40 KiB reserve used by network admissions.
        test::internal_free = 38904;
        test::io_delay_ms = std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS")
                                ? static_cast<uint32_t>(std::strtoul(std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS"), nullptr, 10))
                                : 5;
        test::source->activate(true);
        until([&]
              { return std::strstr(snapshot(Section::Downloaded).status.data(), "Restoring geocaching tasks"); },
              "map reopen did not start storage");
        // A map tile read can contend with the newly opened local catalogue.
        // One failed read must not permanently disable the map's local layer.
        test::fail_read = true;
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        const auto started = test::clock_ms;
        for (unsigned frame = 0; frame < 12 && !map->item_count; ++frame)
        {
            const auto due = test::clock_ms + 750;
            overlays->update(*test::source, 31, 121, 15);
            map = std::make_unique<ui::map::MapOverlaySnapshot>();
            overlays->append(*map);
            if (map->item_count) break;
            while (test::clock_ms < due) tick();
        }
        require(map->item_count == 1 && map->header.valid, "main Map lost the only unpublished local draft");
        require(map->items[0].point.valid && map->items[0].point.lat == 31 && map->items[0].point.lon == 121,
                "local draft coordinates did not reach Map");
        require(map->items[0].style == ui::map::MapOverlayStyle::Warning, "local draft marker state lost");
        std::fprintf(stderr, "Draft-only Map elapsed_ms=%llu\n", static_cast<unsigned long long>(test::clock_ms - started));
        require(test::clock_ms - started <= 3000, "draft-only Map exceeded three seconds");
        require(test::files == saved_files && !router.sends && !router.service, "local Map wrote storage or started networking");
        bool draft_loaded = false;
        const auto read_started = test::clock_ms;
        until([&]
              { return draft_loaded || test::source->readDraft(
                                           local.id, [](const ui::geocaching::DraftInput& value, void* context)
                                           {
                          require(value.name == "Unpublished local draft" && value.has_coordinates, "low-heap draft lost its content");
                          *static_cast<bool*>(context) = true; },
                                           &draft_loaded) == ui::geocaching::DraftReadStatus::Ready; },
              "low internal heap blocked the draft editor");
        require(draft_loaded && test::clock_ms - read_started <= 3000, "draft editor exceeded three seconds with PSRAM available");
        if (legacy_upgrade)
        {
            const auto before_epoch = selected_root().epoch;
            require(selected_root().sequence < 256, "legacy fixture exceeded ordinary checkpoint interval");
            ui::geocaching::DraftInput another;
            another.name = "Second local draft";
            another.has_coordinates = true;
            another.latitude_e7 = local.latitude_e7;
            another.longitude_e7 = local.longitude_e7;
            require(test::source->saveDraft(another), "legacy card did not accept writable operation");
            until([&]
                  { return test::source->draftSaveStatus(another.id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
                  "legacy card draft save failed");
            until([&]
                  { return selected_root().epoch > before_epoch; },
                  "writable legacy card did not automatically upgrade current indexes");
            // Wait for mirror/cleanup to finish and verify that idle ticks
            // do not start another conversion or allocate online transport.
            const auto upgraded_epoch = selected_root().epoch;
            for (unsigned i = 0; i < 10000; ++i) tick();
            require(selected_root().epoch == upgraded_epoch && !router.sends && !router.service, "legacy upgrade repeated or started networking");
            const auto upgraded = selected_root();
            for (const uint8_t table : {uint8_t(1), uint8_t(2), uint8_t(4)})
                for (unsigned bucket = 0; bucket < 256; ++bucket)
                {
                    if (!gc::indexHasShard(upgraded, table, static_cast<uint8_t>(bucket))) continue;
                    gc::IndexShardHead heads[2], head;
                    for (unsigned copy = 0; copy < 2; ++copy)
                    {
                        char path[96];
                        require(sd::indexShardHeadPathForBucket(upgraded.slot, table, static_cast<uint8_t>(bucket), copy, path, sizeof(path)), "upgraded head path");
                        const auto& bytes = test::files.at(path);
                        require(gc::decodeIndexShardHead({bytes.data(), bytes.size()}, volume, upgraded.epoch, table, static_cast<uint8_t>(bucket), heads[copy]), "upgraded head encoding");
                    }
                    require(gc::selectIndexShardHead(heads[0], heads[1], upgraded.sequence, head) && head.current_only, "upgrade retained legacy bucket");
                }
            test::source->activate(false);
            until([&]
                  { return test::allocations.empty(); },
                  "upgraded session did not close");
            const auto upgraded_files = test::files;
            test::source->activate(true);
            const auto reopened = test::clock_ms;
            until([&]
                  { return snapshot(Section::Published).count == 2; },
                  "upgraded local drafts did not reopen");
            require(test::clock_ms - reopened <= 3000 && test::files == upgraded_files && !router.sends && !router.service,
                    "upgraded local reopen was slow, wrote storage or started networking");
        }
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "draft-only Map did not release storage");
        return 0;
    }
    // All interface and transport callbacks below must stay free of SD I/O.
    // Only tick() enters the storage maintenance owner.
    test::in_ui = true;
    require(test::source != nullptr, "UI source not bound");
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "new volume did not reach discovery");
    require(router.selected_chat == chat::MeshProtocol::Meshtastic, "Geocaching changed chat protocol");
    const auto semaphore = xSemaphoreCreateMutex();
    require(xSemaphoreTake(semaphore, 0) == pdTRUE, "cannot simulate a busy storage owner");
    const auto busy_view = snapshot(Section::Discover);
    // The network task can receive the only directory announcement while the
    // storage owner holds the session mutex. It must survive that contention.
    announce(router);
    xSemaphoreGive(semaphore);
    require(busy_view.busy, "storage contention was reported as an empty current snapshot");
    require(!snapshot(Section::Discover).busy, "snapshot did not recover after storage lock release");
    until([&]
          { return router.sends == 1; },
          "capabilities request not dispatched");
    // Browsing must remain disposable, even without an SD card or while USB
    // owns it. Closing abandons the request without any journal transaction.
    require(test::files.empty(), "browse startup wrote storage");
    const auto obsolete_request = router.sent_bytes;
    reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
    test::source->activate(false);
    test::external_owner = true;
    until([&]
          { return !router.service; },
          "RAM query did not close while USB owned SD");
    require(!test::allocated("geocaching.rx") && !test::blocked_io, "close touched SD or leaked reply");
    test::external_owner = false;
    test::card_ready = false;
    router.sends = 0;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "SD-free browse did not start");
    announce(router);
    until([&]
          { return router.sends == 1; },
          "SD-free capabilities missing");
    require(router.sent_bytes != obsolete_request, "restart reused the abandoned query");
    // Real SD operations in the remote capture take tens of milliseconds.
    // Exercise the receive handoff and response scheduling at that cadence.
    tick_ms = 40;
    router.busy_sends_remaining = 3;
    reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
    until([&]
          { return router.sends == 2; },
          "query request not dispatched");
    require(router.busy_sends_remaining == 0, "query did not exercise router contention");

    reply(router, fixture(folder, "query-response-v1.bin"), 2);
    until([&]
          { const auto view = snapshot(Section::Discover); return view.count == 1 && view.can_refresh; },
          "query page not displayed");
    std::array<uint8_t, 320> directory_response{};
    size_t directory_size = 0, directory_fields = 0;
    require(rt::readWebDirectories(0, directory_response.data(), directory_response.size(), directory_size) == rt::WebReadResult::Ready,
            "web directory projection was not available");
    geocaching::protocol::CmpReader directory_reader({directory_response.data(), directory_size});
    uint64_t directory_offset = 0, directory_count = 0, directory_sequence = 0, directory_flags = 0, directory_limit = 0, directory_age = 0;
    geocaching::ByteView directory_delivery, directory_discovery, directory_key, directory_epoch;
    std::string_view directory_name;
    require(directory_reader.array(directory_fields, 3) && directory_fields == 3 && directory_reader.unsignedInteger(directory_offset) &&
                directory_reader.unsignedInteger(directory_count) && directory_reader.array(directory_fields, 9) && directory_fields == 9 &&
                directory_reader.binary(directory_delivery, 16) && directory_reader.binary(directory_discovery, 16) &&
                directory_reader.binary(directory_key, 64) && directory_reader.binary(directory_epoch, 16) &&
                directory_reader.unsignedInteger(directory_sequence) && directory_reader.text(directory_name, 40) &&
                directory_reader.unsignedInteger(directory_flags) && directory_reader.unsignedInteger(directory_limit) &&
                directory_reader.unsignedInteger(directory_age) && directory_reader.finished() && directory_offset == 0 && directory_count == 1 &&
                directory_key.size == 64 && (directory_flags & 1) && directory_limit > 0,
            "web directory projection changed discovery metadata");
    require(rt::readWebDirectories(1, directory_response.data(), directory_response.size(), directory_size) == rt::WebReadResult::Ready,
            "web directory end marker missing");
    require(rt::readWebDirectories(5, directory_response.data(), directory_response.size(), directory_size) == rt::WebReadResult::Invalid,
            "web directory projection accepted an out-of-budget index");
    rt::webLease(false);
    require(test::files.empty() && !test::blocked_io && !test::allocated("geocaching.index.read") &&
                !test::allocated("geocaching.index.encode"),
            "browsing depends on SD or its workspaces");
    if (web_latest)
    {
        ui::geocaching::Item latest_item;
        const auto page = snapshot(Section::Discover);
        require(test::source->item(Section::Discover, 0, page.generation, latest_item), "latest test cache missing");
        std::array<uint8_t, 16> target{};
        std::array<uint8_t, 36> latest_result{};
        const auto baseline_allocations = test::allocations.size();
        require(rt::readWebLatestRevision(latest_item.id.data(), target.data(), latest_result.data(), 35) == rt::WebReadResult::Invalid,
                "latest query accepted short buffer");
        const auto before_get = router.sends;
        require(rt::readWebLatestRevision(latest_item.id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Pending,
                "latest query did not start");
        until([&]
              { return router.sends == before_get + 1; },
              "latest GET not sent");
        geocaching::protocol::CmpReader envelope({router.sent_bytes.data(), router.sent_bytes.size()});
        size_t fields = 0;
        uint64_t value = 0;
        geocaching::ByteView request_id;
        require(envelope.array(fields, 6) && fields == 6 && envelope.unsignedInteger(value) && value == 1 &&
                    envelope.unsignedInteger(value) && value == 0 && envelope.unsignedInteger(value) && value == 3 &&
                    envelope.binary(request_id, 16) && request_id.size == 16,
                "latest GET envelope invalid");
        geocaching::RequestId expected_request;
        std::memcpy(expected_request.bytes.data(), request_id.data, 16);
        geocaching::protocol::GetRequestView request;
        require(geocaching::protocol::decodeGetRequest({router.sent_bytes.data(), router.sent_bytes.size()}, expected_request, request) &&
                    !request.wanted_hash.size && !request.known_hash.size && !std::memcmp(request.cache_id.data, latest_item.id.data(), 32),
                "latest GET pinned an old revision");
        auto noncurrent = fixture(folder, "get-response-v1.bin");
        noncurrent[noncurrent.size() - 2] = 0;
        reply(router, noncurrent, 3);
        for (unsigned i = 0; i < 30; ++i) tick();
        require(rt::readWebLatestRevision(latest_item.id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Pending,
                "latest query accepted noncurrent record");
        auto invalid = fixture(folder, "get-response-v1.bin");
        invalid[invalid.size() - 4] ^= 1;
        reply(router, invalid, 3);
        for (unsigned i = 0; i < 30; ++i) tick();
        require(rt::readWebLatestRevision(latest_item.id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Pending,
                "latest query accepted invalid author signature");
        target[0] = 99;
        require(rt::readWebLatestRevision(latest_item.id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::NotFound,
                "latest query accepted unknown directory");
        target[0] = 0;
        reply(router, fixture(folder, "get-response-v1.bin"), 3);
        bool latest_ready = false;
        until([&]
              { if (latest_ready) return true;
                    latest_ready = rt::readWebLatestRevision(latest_item.id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Ready;
                    return latest_ready; },
              "verified latest record not returned");
        require(latest_result[0] == expected_record.revision && latest_result[1] == 0 && latest_result[2] == 0 && latest_result[3] == 0 &&
                    !std::memcmp(latest_result.data() + 4, latest_item.revision_hash.data(), 32),
                "latest revision result mismatch");
        require(test::allocations.size() == baseline_allocations && !test::allocated("geocaching.detail.rx") &&
                    !test::allocated("geocaching.detail.verify"),
                "latest query retained its detail buffers");
        require(test::files.empty() && !test::blocked_io, "latest query opened SD");
        auto missing_id = latest_item.id;
        missing_id[0] ^= 1;
        const auto before_missing = router.sends;
        require(rt::readWebLatestRevision(missing_id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Pending,
                "missing record query did not start");
        until([&]
              { return router.sends == before_missing + 1; },
              "missing record GET not sent");
        std::array<uint8_t, 96> error_bytes{};
        geocaching::protocol::CmpWriter error(error_bytes.data(), error_bytes.size());
        error.array(6);
        error.unsignedInteger(1);
        error.unsignedInteger(1);
        error.unsignedInteger(3);
        error.binary({expected_request.bytes.data(), 16});
        error.unsignedInteger(404);
        error.array(3);
        error.text("revision_unavailable");
        error.nil();
        error.nil();
        require(error.good(), "missing revision fixture encoding failed");
        reply(router, {error_bytes.data(), error_bytes.data() + error.size()}, 3);
        for (unsigned i = 0; i < 30; ++i) tick();
        require(rt::readWebLatestRevision(missing_id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Pending,
                "missing revision was treated as missing cache");
        geocaching::protocol::CmpWriter absent(error_bytes.data(), error_bytes.size());
        absent.array(6);
        absent.unsignedInteger(1);
        absent.unsignedInteger(1);
        absent.unsignedInteger(3);
        absent.binary({expected_request.bytes.data(), 16});
        absent.unsignedInteger(404);
        absent.array(3);
        absent.text("not_found");
        absent.nil();
        absent.nil();
        require(absent.good(), "missing cache fixture encoding failed");
        auto malformed_absence = std::vector<uint8_t>(error_bytes.data(), error_bytes.data() + absent.size());
        malformed_absence.back() = 0;
        reply(router, malformed_absence, 3);
        for (unsigned i = 0; i < 30; ++i) tick();
        require(rt::readWebLatestRevision(missing_id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Pending,
                "malformed absence response was accepted");
        reply(router, {error_bytes.data(), error_bytes.data() + absent.size()}, 3);
        auto absence = rt::WebReadResult::Pending;
        until([&]
              { if (absence != rt::WebReadResult::Pending) return true;
                    absence = rt::readWebLatestRevision(missing_id.data(), target.data(), latest_result.data(), latest_result.size());
                    return absence != rt::WebReadResult::Pending; },
              "explicit absence not returned");
        require(absence == rt::WebReadResult::Absent && latest_result == std::array<uint8_t, 36>{},
                "explicit absence retained the previous version result");
        require(test::allocations.size() == baseline_allocations && test::files.empty(), "absence query retained memory or touched SD");
        require(rt::readWebLatestRevision(latest_item.id.data(), target.data(), latest_result.data(), latest_result.size()) == rt::WebReadResult::Pending,
                "second latest query did not start");
        rt::webLease(false);
        until([&]
              { return test::allocations.size() == baseline_allocations; },
              "cancelled latest query retained its detail");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "latest query session did not close");
        return 0;
    }
    test::card_ready = true;
    tick_ms = 5;
    std::array<uint8_t, 16> reply_remote{};
    require(router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {last_reply.data(), last_reply.size()}}, router.context),
            "accepted RAM reply was not acknowledged on retry");
    auto changed_reply = last_reply;
    changed_reply.back() ^= 1;
    require(!router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {changed_reply.data(), changed_reply.size()}}, router.context),
            "modified duplicate reply was acknowledged");
    reply_remote[0] = 1;
    require(!router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {last_reply.data(), last_reply.size()}}, router.context),
            "reply from another directory was acknowledged");
    reply_remote[0] = 0;
    // Protocol switching can remove a background backend between UI ticks.
    // Recreate it without losing the RAM page or opening storage.
    router.service.reset();
    until([&]
          { return router.service != nullptr; },
          "browse transport did not reconnect");
    require(snapshot(Section::Discover).count == 1 && test::files.empty(), "transport reconnection lost the RAM page");
    test::profile_reads = true;
    test::read_bytes_by_path.clear();
    for (unsigned i = 0; i < 500; ++i) tick();
    require(test::read_bytes_by_path.empty(), "completed browsing kept scanning recovery or dispatch history");
    test::profile_reads = false;
    ui::geocaching::Item item;
    uint64_t generation = 0;
    test::source->requestWindow(Section::Discover, 0, 1);
    until([&]
          {
              const auto view = snapshot(Section::Discover);
              generation = view.generation;
              return test::source->item(Section::Discover, 0, generation, item) && item.can_download; },
          "discovered row did not become downloadable");
    test::card_ready = false;
    test::source->open(item, generation);
    until([&]
          { return router.sends == 3; },
          "online detail request missing");
    auto invalid_detail = fixture(folder, "get-response-v1.bin");
    invalid_detail[invalid_detail.size() - 4] ^= 1;
    reply(router, invalid_detail, 3);
    for (unsigned i = 0; i < 30; ++i) tick();
    require(detailText(item).status == ui::geocaching::DetailStatus::Pending, "invalid detail signature was displayed");
    reply(router, fixture(folder, "get-response-v1.bin"), 3);
    until([&]
          { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
          "online detail not displayed");
    require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
            "online detail omitted full description or hint");
    ui::geocaching::Item discover_probe;
    require(snapshot(Section::Discover).generation == generation && test::source->item(Section::Discover, 0, generation, discover_probe),
            "detail response invalidated unchanged discovery rows");
    require(test::files.empty() && !test::blocked_io, "online detail required SD");
    require(router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {last_reply.data(), last_reply.size()}}, router.context),
            "verified detail duplicate was not acknowledged");
    require(xSemaphoreTake(semaphore, 0) == pdTRUE, "cannot lock during detail close");
    test::source->closeDetail();
    xSemaphoreGive(semaphore);
    tick();
    require(detailText(item).status == ui::geocaching::DetailStatus::Failed, "busy close lost the cancellation");
    require(!test::allocated("geocaching.detail.rx") && !test::allocated("geocaching.detail.verify"), "closed detail retained temporary buffers");
    test::card_ready = true;
    test::source->open(item, generation);
    until([&]
          { return router.sends == 4; },
          "reopened detail request not dispatched");
    reply(router, fixture(folder, "get-response-v1.bin"), 3);
    until([&]
          { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
          "reopened detail not verified");
    test::source->requestWindow(Section::Published, 0, 4);
    until([&]
          { return std::strstr(snapshot(Section::Published).status.data(), "No local drafts"); },
          "initial publication projection did not load");
    const auto publication_rows_before_download = snapshot(Section::Published).generation;
    test::source->requestWindow(Section::Discover, 0, 1);
    generation = snapshot(Section::Discover).generation;
    void* open_response = nullptr;
    for (const auto& allocation : test::allocations)
        if (allocation.second.owner == "geocaching.rx") open_response = allocation.first;
    require(open_response != nullptr, "open detail response missing before save");
    test::memory_available = false;
    require(!test::source->download(item, generation), "save ignored unavailable PSRAM");
    test::memory_available = true;
    require(detailText(item).status == ui::geocaching::DetailStatus::Ready,
            "failed save invalidated the open verified detail");
    require(test::source->download(item, generation), "download not queued");
    require(test::allocated("geocaching.download.detail"), "verified download did not own a response copy");
    if (close_on_save)
    {
        // Release the original response before the installation's first tick.
        // Every verified view must belong to the queued download's copy.
        test::source->closeDetail();
        until([&]
              {
                  const auto allocation = test::allocations.find(open_response);
                  return allocation == test::allocations.end() || allocation->second.owner != "geocaching.rx"; },
              "closing pending download retained the detail response");
    }
    until([&]
          {
              if (!close_on_save)
                  require(detailText(item).status == ui::geocaching::DetailStatus::Ready,
                          "saving invalidated the open verified detail");
              return std::strstr(snapshot(Section::Discover).status.data(), "Shared caches"); },
          "download did not finish");
    if (!close_on_save)
    {
        require(test::allocations.count(open_response), "saving released the open detail response");
        require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
                "saved open detail lost verified text");
        require(!test::source->archiveCache(item.id, item.revision_hash), "saved foreign detail became archivable");
        test::source->closeDetail();
        tick();
    }
    require(router.sends == 4, "saving verified detail sent a redundant network request");
    require(snapshot(Section::Discover).generation != generation && !test::source->item(Section::Discover, 0, generation, discover_probe),
            "downloaded membership retained stale discovery rows");
    const auto released_response = test::allocations.find(open_response);
    require(released_response == test::allocations.end() || released_response->second.owner != "geocaching.rx",
            "saved detail retained its response buffer");
    // A complete download writes requests, attempts, objects and installed
    // heads. None changes the publication projection we already loaded.
    require(std::strstr(snapshot(Section::Published).status.data(), "No local drafts"),
            "unrelated download invalidated the loaded publication projection");
    require(snapshot(Section::Published).generation == publication_rows_before_download,
            "unrelated download changed the publication row token");
    test::source->requestWindow(Section::Downloaded, 0, 4);
    until([&]
          {
              const auto view = snapshot(Section::Downloaded);
              return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.downloaded; },
          "installed GPX missing from directory");
    const auto saved_id = item.id;
    if (legacy_projection)
    {
        namespace gc = geocaching::storage;
        namespace sd = platform::esp::arduino_common::geocaching;
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "legacy fixture session did not close");
        gc::VolumeInstance volume;
        const auto& format = test::files.at("/trailmate/geocaching/.state/format.bin");
        require(gc::decodeVolumeHeader({format.data(), format.size()}, volume) == gc::VolumeFormatResult::Supported, "legacy saved volume");
        auto roots = std::make_unique<std::array<gc::IndexRootBytes, 2>>();
        gc::IndexRootView root, decoded[2];
        for (unsigned copy = 0; copy < 2; ++copy)
        {
            const auto& bytes = test::files.at(copy ? "/trailmate/geocaching/.state/index/root.h1" : "/trailmate/geocaching/.state/index/root.h0");
            require(bytes.size() == (*roots)[copy].size(), "legacy saved root size");
            std::memcpy((*roots)[copy].data(), bytes.data(), bytes.size());
            require(gc::decodeIndexRoot({(*roots)[copy].data(), bytes.size()}, volume, decoded[copy]), "legacy saved root decode");
        }
        require(gc::selectIndexRoot(decoded[0], decoded[1], root), "legacy saved selected root");
        unsigned copy = root.shards.data == decoded[1].shards.data ? 1 : 0;
        auto frame = std::make_unique<std::array<uint8_t, 8192>>();
        const auto read_index = [&](uint8_t table, geocaching::ByteView key)
        {
            auto get = std::make_unique<sd::SdIndexGet>(volume);
            require(get->begin(root, table, key, frame->data(), frame->size()), "legacy fixture get begin");
            sd::IndexGetStep result;
            do
            {
                test::in_ui = false;
                test::io_bytes = 0;
                result = get->step();
            } while (result == sd::IndexGetStep::Working);
            require(result == sd::IndexGetStep::Ready, "legacy fixture get result");
            const auto value = get->value();
            return std::vector<uint8_t>(value.data, value.data + value.size);
        };
        auto object_bytes = read_index(1, {item.revision_hash.data(), 32});
        gc::ObjectRefView object;
        require(gc::decodeObjectRef({item.revision_hash.data(), 32}, {object_bytes.data(), object_bytes.size()}, object) && object.saved_request.size, "legacy saved projected object");
        std::array<uint8_t, 16> installed_task{};
        std::array<uint8_t, 48> installed_request{};
        std::memcpy(installed_task.data(), object.saved_task.data, 16);
        std::memcpy(installed_request.data(), object.saved_request.data, 48);
        const auto install_before = read_index(12, {installed_task.data(), 16});
        if (!legacy_projected && !legacy_object_only)
        {
            object.name = {};
            object.saved_task = object.saved_request = {};
        }
        auto encoded = std::make_unique<std::array<uint8_t, 320>>();
        size_t encoded_size = 0;
        require(gc::encodeObjectRef({item.revision_hash.data(), 32}, object, encoded->data(), encoded->size(), encoded_size), "legacy saved object encoding");
        gc::MutationView mutation{1, {item.revision_hash.data(), 32}, {encoded->data(), encoded_size}, false};
        auto commit = std::make_unique<sd::SdIndexedCommit>(volume);
        require(commit->begin(root, copy, &mutation, 1, frame->data(), frame->size(), (*roots)[1 - copy]), "legacy fixture commit begin");
        sd::IndexedCommitStep committed;
        do
        {
            test::in_ui = false;
            test::io_bytes = 0;
            committed = commit->step();
        } while (committed == sd::IndexedCommitStep::Working);
        require(committed == sd::IndexedCommitStep::Verified && commit->committed(root), "legacy fixture object commit");
        copy = 1 - copy;
        if (bad_legacy_signature)
        {
            auto outgoing_bytes = read_index(5, {installed_request.data(), 48});
            gc::OutgoingView outgoing;
            geocaching::RequestId request;
            std::memcpy(request.bytes.data(), installed_request.data() + 32, 16);
            geocaching::protocol::GetResponseView response;
            require(gc::decodeOutgoing({installed_request.data(), 48}, {outgoing_bytes.data(), outgoing_bytes.size()}, outgoing) &&
                        geocaching::protocol::decodeGetResponse(outgoing.terminal_data, request, 8192, response),
                    "legacy corrupt receipt");
            outgoing_bytes[response.signed_cache.data - outgoing_bytes.data() + response.signed_cache.size - 1] ^= 1;
            mutation = {5, {installed_request.data(), 48}, {outgoing_bytes.data(), outgoing_bytes.size()}, false};
            commit = std::make_unique<sd::SdIndexedCommit>(volume);
            require(commit->begin(root, copy, &mutation, 1, frame->data(), frame->size(), (*roots)[1 - copy]), "legacy corrupt receipt commit begin");
            do
            {
                test::in_ui = false;
                test::io_bytes = 0;
                committed = commit->step();
            } while (committed == sd::IndexedCommitStep::Working);
            require(committed == sd::IndexedCommitStep::Verified && commit->committed(root), "legacy corrupt receipt commit");
            copy = 1 - copy;
        }
        // Also cover already projected objects in append-era object shards,
        // including when cache heads already use current generations.
        for (const uint8_t table : {uint8_t(1), uint8_t(2)})
            for (unsigned bucket = 0; bucket < 256; ++bucket)
            {
                if (legacy_object_only && table == 2) continue;
                if (!gc::indexHasShard(root, table, static_cast<uint8_t>(bucket))) continue;
                char paths[2][96];
                gc::IndexShardHead heads[2], head;
                for (unsigned n = 0; n < 2; ++n)
                {
                    require(sd::indexShardHeadPathForBucket(root.slot, table, static_cast<uint8_t>(bucket), n, paths[n], sizeof(paths[n])), "legacy saved head path");
                    const auto& bytes = test::files.at(paths[n]);
                    require(gc::decodeIndexShardHead({bytes.data(), bytes.size()}, volume, root.epoch, table, static_cast<uint8_t>(bucket), heads[n]), "legacy saved head decode");
                }
                require(gc::selectIndexShardHead(heads[0], heads[1], root.sequence, head) && head.current_only, "legacy saved head selection");
                char current[96], legacy[96];
                require(sd::indexShardDataPath(root.slot, head, current, sizeof(current)), "legacy saved current path");
                head.current_only = false;
                require(sd::indexShardDataPath(root.slot, head, legacy, sizeof(legacy)), "legacy saved old path");
                test::files[legacy] = test::files.at(current);
                gc::IndexShardHeadBytes bytes;
                require(gc::encodeIndexShardHead(volume, head, bytes), "legacy saved head encode");
                for (const auto& path : paths) test::files[path] = {bytes.begin(), bytes.end()};
            }
        // Metadata upgrade must not hash, rewrite, or reject an edited GPX.
        std::map<std::string, std::vector<uint8_t>> gpx_files;
        for (auto& file : test::files)
            if (file.first.find("/caches/") != std::string::npos && file.first.find(".gpx") != std::string::npos)
            {
                file.second.push_back('\n');
                gpx_files.emplace(file.first, file.second);
            }
        require(!gpx_files.empty(), "legacy saved GPX absent");
        router.ready = false;
        const auto sends = router.sends;
        test::internal_free = 38904;
        test::io_delay_ms = 25;
        const auto before_upgrade = test::files;
        test::source->activate(true);
        test::source->requestWindow(Section::Downloaded, 0, 4);
        if (bad_legacy_signature)
        {
            until([&]
                  { return std::strstr(snapshot(Section::Downloaded).status.data(), "index upgrade failed"); },
                  "invalid legacy signature did not stop upgrade", true);
            for (const auto& file : test::read_bytes_by_path) require(file.first.find("/caches/") == std::string::npos, "failed legacy upgrade read GPX");
            for (unsigned i = 0; i < 2000; ++i) tick();
            require(test::files == before_upgrade && router.sends == sends && !router.service, "invalid signature wrote projection or started network");
            test::source->activate(false);
            until([&]
                  { return test::allocations.empty(); },
                  "failed legacy upgrade leaked PSRAM");
            return 0;
        }
        const auto upgrade_started = test::clock_ms;
        until([&]
              { const auto view = snapshot(Section::Downloaded); return view.ready && view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item); },
              "legacy saved projection upgrade did not finish", true);
        std::fprintf(stderr, "Legacy saved upgrade elapsed_ms=%llu\n", static_cast<unsigned long long>(test::clock_ms - upgrade_started));
        require(test::clock_ms - upgrade_started < 10000, "legacy saved one-time upgrade exceeded ten seconds");
        require(item.id == saved_id && item.name.data() == expected_record.name && item.latitude_e7 == expected_record.latitude_e7 && item.longitude_e7 == expected_record.longitude_e7,
                "legacy saved upgrade lost display metadata");
        for (const auto& file : test::read_bytes_by_path) require(file.first.find("/caches/") == std::string::npos, "legacy metadata upgrade read GPX");
        for (const auto& file : gpx_files) require(test::files.at(file.first) == file.second, "legacy metadata upgrade changed GPX");
        require(router.sends == sends && !router.service, "legacy metadata upgrade started networking");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "legacy saved upgrade did not close");
        // Re-select published roots before checking that cleanup was untouched.
        for (unsigned n = 0; n < 2; ++n)
        {
            const auto& bytes = test::files.at(n ? "/trailmate/geocaching/.state/index/root.h1" : "/trailmate/geocaching/.state/index/root.h0");
            std::memcpy((*roots)[n].data(), bytes.data(), bytes.size());
            require(gc::decodeIndexRoot({(*roots)[n].data(), bytes.size()}, volume, decoded[n]), "upgraded root decode");
        }
        require(gc::selectIndexRoot(decoded[0], decoded[1], root), "upgraded root selection");
        require(read_index(12, {installed_task.data(), 16}) == install_before, "metadata upgrade changed installation or cleanup proof");
        for (const uint8_t table : {uint8_t(1), uint8_t(2)})
            for (unsigned bucket = 0; bucket < 256; ++bucket)
            {
                if (!gc::indexHasShard(root, table, static_cast<uint8_t>(bucket))) continue;
                gc::IndexShardHead heads[2], head;
                for (unsigned n = 0; n < 2; ++n)
                {
                    char path[96];
                    require(sd::indexShardHeadPathForBucket(root.slot, table, static_cast<uint8_t>(bucket), n, path, sizeof(path)), "converted saved shard path");
                    const auto& bytes = test::files.at(path);
                    require(gc::decodeIndexShardHead({bytes.data(), bytes.size()}, volume, root.epoch, table, static_cast<uint8_t>(bucket), heads[n]), "converted saved shard decode");
                }
                require(gc::selectIndexShardHead(heads[0], heads[1], root.sequence, head) && head.current_only, "saved upgrade retained legacy generation");
            }
        const auto upgraded_files = test::files;
        test::source->activate(true);
        test::source->requestWindow(Section::Downloaded, 0, 4);
        const auto reopened = test::clock_ms;
        until([&]
              { require(!test::allocated("geocaching.index.verify") && !test::allocated("geocaching.index.payload"), "current saved page allocated signed-record buffers"); const auto view = snapshot(Section::Downloaded); return view.ready && view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item); },
              "upgraded saved page did not reopen", true);
        require(test::clock_ms - reopened <= 3000 && test::files == upgraded_files, "upgraded saved page repeated migration or exceeded three seconds");
        for (const auto& file : test::read_bytes_by_path)
            require(file.first.find("/caches/") == std::string::npos && file.first.find("/05/") == std::string::npos && file.first.find("/0a/") == std::string::npos && file.first.find("/0c/") == std::string::npos,
                    "upgraded saved page scanned requests, tasks, installations, or GPX");
        test::source->open(item, snapshot(Section::Downloaded).generation);
        until([&]
              { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
              "upgraded saved details unavailable");
        require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint && router.sends == sends,
                "upgraded saved detail lost signed text or required network");
        test::source->closeDetail();
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "upgraded saved details leaked PSRAM");
        return 0;
    }
    if (close_on_save)
    {
        test::source->open(item, snapshot(Section::Downloaded).generation);
        until([&]
              { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
              "closed detail download lost installed details");
        require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
                "closed detail download corrupted installed text");
        require(router.sends == 4, "installed detail required another network request");
        test::source->closeDetail();
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "closed detail installation leaked PSRAM");
        return 0;
    }
    ui::geocaching::DraftInput draft;
    draft.name = "Published through device runtime";
    draft.description = "A local draft with a durable signed revision";
    draft.has_coordinates = true;
    draft.latitude_e7 = 310000000;
    draft.longitude_e7 = 1210000000;
    require(test::source->saveDraft(draft), "draft was not queued");
    until([&]
          { return test::source->draftSaveStatus(draft.id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
          "draft was not saved");
    test::source->requestWindow(Section::Published, 0, 4);
    uint64_t draft_generation = 0;
    const auto draft_list_started = test::clock_ms;
    until([&]
          {
              const auto view = snapshot(Section::Published);
              if (!view.count || !test::source->item(Section::Published, 0, view.generation, item)) return false;
              draft_generation = item.edit_generation;
              return item.is_draft; },
          "saved draft missing from publication list");
    require((test::clock_ms - draft_list_started) / tick_ms < 40,
            "unbound draft listing regressed into publication-history work");
    {
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        until([&]
              {
                  overlays->update(*test::source, 31, 121, 15);
                  map = std::make_unique<ui::map::MapOverlaySnapshot>();
                  overlays->append(*map);
                  return map->item_count == 2; },
              "map did not include unpublished draft and downloaded cache");
        require(map->items[0].point.lat == 31 && map->items[0].point.lon == 121 &&
                    map->items[0].style == ui::map::MapOverlayStyle::Warning,
                "unpublished map position or state lost");
        test::source->requestWindow(Section::Published, 0, 4);
    }
    std::array<uint8_t, 64> author;
    uint32_t from = 0, to = 0;
    until([&]
          { return test::source->publicationAuthor(draft.id, draft_generation, author, &from, &to); },
          "draft not ready for publication confirmation");
    const bool dispatch_profile = test::profile_reads;
    std::vector<std::string> prior_attempt_shards;
    for (const auto& file : test::files)
        if (file.first.find("/0d/") != std::string::npos && file.first.size() >= 4 && file.first.substr(file.first.size() - 4) == ".gci")
            prior_attempt_shards.push_back(file.first);
    test::profile_reads = true;
    test::read_bytes_by_path.clear();
    namespace web_publish = platform::esp::arduino_common::geocaching::browse_runtime;
    std::array<uint8_t, 16> selected_directory{};
    std::array<uint8_t, 64> web_author{};
    uint32_t web_from = 0, web_to = 0;
    const auto before_preview = test::files;
    require(web_publish::webPublicationPlan(draft.id.data(), draft_generation, selected_directory.data(), web_author.data(), web_from, web_to),
            "web publication preview rejected verified directory");
    require(web_author == author && web_from == from && web_to == to && test::files == before_preview,
            "web preview changed storage or publication tuple");
    std::array<uint8_t, 56> publication_binding{};
    selected_directory[0] = 99;
    require(web_publish::readWebPublicationBinding(draft.id.data(), draft_generation, selected_directory.data(), author.data(),
                                                   publication_binding.data(), publication_binding.size()) == rt::WebReadResult::NotFound,
            "confirmation binding accepted unknown directory");
    selected_directory[0] = 0;
    require(web_publish::readWebPublicationBinding(draft.id.data(), draft_generation, selected_directory.data(), author.data(),
                                                   publication_binding.data(), 55) == rt::WebReadResult::Invalid,
            "confirmation binding accepted short output");
    std::array<uint8_t, 384> another_window{};
    size_t another_window_size = 0;
    until([&]
          { return rt::readWebDraftCatalog(4, false, 0, another_window.data(), another_window.size(), another_window_size) == rt::WebReadResult::Ready; },
          "alternate publication window unavailable");
    auto binding_status = rt::WebReadResult::Pending;
    until([&]
          { if (binding_status == rt::WebReadResult::Ready) return true;
            binding_status = web_publish::readWebPublicationBinding(draft.id.data(), draft_generation, selected_directory.data(), author.data(),
                                                                     publication_binding.data(), publication_binding.size());
            return binding_status == rt::WebReadResult::Ready; },
          "committed draft confirmation binding not ready");
    require(test::files == before_preview && router.identity.signs == 0,
            "confirmation binding mutated or signed the draft");
    require(publication_binding[48] == 0 && publication_binding[52] == 1,
            "confirmation binding changed local revision plan");
    for (size_t i = 32; i < 48; ++i) require(publication_binding[i] == 0, "confirmation binding changed directory epoch");
    std::vector<uint8_t> committed_draft;
    uint64_t committed_size = 0;
    do
    {
        std::array<uint8_t, 384> frame{};
        size_t written = 0;
        bool chunk_ready = false;
        until([&]
              { if (chunk_ready) return true;
                chunk_ready = rt::readWebDraftChunk(draft.id.data(), static_cast<uint32_t>(committed_draft.size()), 320, draft_generation,
                                                   frame.data(), frame.size(), written) == rt::WebReadResult::Ready;
                return chunk_ready; },
              "confirmation draft bytes unavailable");
        geocaching::protocol::CmpReader reader({frame.data(), written});
        size_t count = 0;
        uint64_t read_generation = 0, offset = 0;
        geocaching::ByteView read_id, chunk;
        require(reader.array(count, 5) && count == 5 && reader.binary(read_id, 16) && read_id.size == 16 &&
                    !std::memcmp(read_id.data, draft.id.data(), 16) && reader.unsignedInteger(read_generation) &&
                    read_generation == draft_generation && reader.unsignedInteger(committed_size) && reader.unsignedInteger(offset) &&
                    offset == committed_draft.size() && reader.binary(chunk, 320) && chunk.size && reader.finished(),
                "confirmation draft chunk correlation failed");
        committed_draft.insert(committed_draft.end(), chunk.data, chunk.data + chunk.size);
    } while (committed_draft.size() < committed_size);
    std::array<uint8_t, 32> committed_hash{};
    chat::reticulum::fullHash(committed_draft.data(), committed_draft.size(), committed_hash.data());
    require(!std::memcmp(committed_hash.data(), publication_binding.data(), committed_hash.size()),
            "confirmation hash does not cover committed draft bytes");
    rt::WebReadResult stale_binding = rt::WebReadResult::Pending;
    until([&]
          { if (stale_binding != rt::WebReadResult::Pending) return true;
            stale_binding = rt::readWebPublicationBinding(draft.id.data(), draft_generation + 1, selected_directory.data(), author.data(),
                                                           publication_binding.data(), publication_binding.size()); return stale_binding != rt::WebReadResult::Pending; },
          "stale confirmation binding did not finish");
    require(stale_binding == rt::WebReadResult::Conflict, "confirmation binding accepted wrong draft generation");
    selected_directory[0] = 99;
    require(!web_publish::queueWebDraftPublication(draft.id.data(), draft_generation, selected_directory.data(), author.data(), to),
            "web publication accepted unknown directory");
    selected_directory[0] = 0;
    web_author[0] ^= 1;
    require(!web_publish::queueWebDraftPublication(draft.id.data(), draft_generation, selected_directory.data(), web_author.data(), to),
            "web publication accepted another author");
    require(!web_publish::queueWebDraftPublication(draft.id.data(), draft_generation, selected_directory.data(), author.data(), to + 1),
            "web publication accepted stale revision");
    bool read_for_confirmation = false;
    std::array<uint8_t, 384> confirmation_chunk{};
    size_t confirmation_chunk_size = 0;
    until([&]
          { if (read_for_confirmation) return true;
            read_for_confirmation = rt::readWebDraftChunk(draft.id.data(), 0, 1, draft_generation,
                                                           confirmation_chunk.data(), confirmation_chunk.size(), confirmation_chunk_size) == rt::WebReadResult::Ready;
            return read_for_confirmation; },
          "confirmation reader did not retain the committed draft");
    publication_binding[0] ^= 1;
    require(!web_publish::queueWebDraftPublication(draft.id.data(), draft_generation, selected_directory.data(), author.data(), to,
                                                   publication_binding.data()),
            "publication accepted another confirmed content hash");
    publication_binding[0] ^= 1;
    publication_binding[32] ^= 1;
    require(!web_publish::queueWebDraftPublication(draft.id.data(), draft_generation, selected_directory.data(), author.data(), to,
                                                   publication_binding.data()),
            "publication accepted another directory incarnation");
    publication_binding[32] ^= 1;
    require(from == 0 && to == 1, "web confirmation version plan changed");
    std::array<uint8_t, 16> web_operation_id{};
    require(!web_publish::queueWebDraftPublication(draft.id.data(), draft_generation, selected_directory.data(), author.data(), to,
                                                   publication_binding.data(), web_operation_id.data()),
            "web publication accepted an empty operation ID");
    web_operation_id.fill(0x6a);
    bool publication_queued = false;
    until([&]
          { if (publication_queued) return true;
            publication_queued = web_publish::queueWebDraftPublication(draft.id.data(), draft_generation, selected_directory.data(), author.data(), to,
                                                                       publication_binding.data(), web_operation_id.data());
            return publication_queued; },
          "web publication confirmation rejected");
    std::array<uint8_t, 256> job_response{};
    size_t job_written = 0;
    auto check_web_job = [&](uint64_t expected_phase, bool metadata, int expected_durable, int expected_transport)
    {
        require(rt::readWebPublicationJob(web_operation_id.data(), author.data(), job_response.data(), job_response.size(), job_written) == rt::WebReadResult::Ready,
                "web operation projection unavailable");
        geocaching::protocol::CmpReader projection({job_response.data(), job_written});
        size_t fields = 0;
        uint64_t phase = 0, revision = 0, durable = 0, transported = 0;
        geocaching::ByteView operation, cache, hash;
        require(projection.array(fields, 8) && fields == 8 && projection.binary(operation, 16) && operation.size == 16 &&
                    !std::memcmp(operation.data, web_operation_id.data(), 16) && projection.unsignedInteger(phase) && phase == expected_phase &&
                    (metadata ? projection.binary(cache, 32) && cache.size == 32 : projection.nil()) &&
                    (metadata ? projection.binary(hash, 32) && hash.size == 32 : projection.nil()) && projection.unsignedInteger(revision) && revision == 1 &&
                    (expected_durable < 0 ? projection.nil() : projection.unsignedInteger(durable) && durable == static_cast<uint64_t>(expected_durable)),
                "web operation projection changed phase or durability");
        if (expected_transport == -2)
        {
            auto optional = projection;
            if (optional.nil()) projection = optional;
            else require(projection.unsignedInteger(transported) && transported == 1, "unobserved transport was reported false");
        }
        else require(expected_transport < 0 ? projection.nil() : projection.unsignedInteger(transported) && transported == static_cast<uint64_t>(expected_transport),
                     "web operation transport state mismatch");
        require(projection.nil() && projection.finished(), "successful web operation included an error");
    };
    check_web_job(0, false, 0, 0);
    auto other_operation = web_operation_id;
    other_operation[0] ^= 1;
    require(rt::readWebPublicationJob(other_operation.data(), author.data(), job_response.data(), job_response.size(), job_written) == rt::WebReadResult::NotFound,
            "operation query aliased another ID");
    auto other_author = author;
    other_author[0] ^= 1;
    require(rt::readWebPublicationJob(web_operation_id.data(), other_author.data(), job_response.data(), job_response.size(), job_written) == rt::WebReadResult::Forbidden,
            "operation query exposed another author's job");
    router.identity.public_key[0] ^= 1;
    require(rt::readWebPublicationJob(web_operation_id.data(), author.data(), job_response.data(), job_response.size(), job_written) == rt::WebReadResult::StaleIdentity,
            "operation query ignored the connected device's changed identity");
    router.identity.public_key[0] ^= 1;
    require(rt::webLease(false), "confirmed publication did not release the browser lease");
    if (web_signed_stop)
    {
        const auto sends_before_stop = router.sends;
        until([&]
              { return router.identity.signs == 1; },
              "draft never reached the signed pre-attempt window");
        require(router.sends == sends_before_stop, "signed window already sent the publication");
        const bool previous_ui = test::in_ui;
        test::in_ui = false;
        require(rt::requestWebPublicationStop(web_operation_id.data(), author.data()) == rt::WebReadResult::Pending,
                "signed stop invented a durable completion");
        test::in_ui = previous_ui;
        check_web_job(6, false, -1, -1);
        // Deliberately do not retry the command while the owner creates and
        // persists the attempt. Its retained stop request must prevent sends.
        until([&]
              {
                  const auto result = rt::readWebPublicationJob(web_operation_id.data(), author.data(),
                                                               job_response.data(), job_response.size(), job_written);
                  if (result != rt::WebReadResult::Ready) return false;
                  geocaching::protocol::CmpReader reader({job_response.data(), job_written});
                  size_t fields = 0;
                  geocaching::ByteView operation;
                  uint64_t phase = 0;
                  require(reader.array(fields, 8) && fields == 8 && reader.binary(operation, 16) && reader.unsignedInteger(phase),
                          "signed stop projection invalid");
                  return phase == 4; },
              "signed stop did not persist without command retries");
        check_web_job(4, true, 1, -2);
        require(router.sends == sends_before_stop && router.identity.signs == 1, "signed stop sent or signed again");
        require(rt::webLease(false), "signed stop retained web lease");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "signed stop leaked PSRAM");
        return 0;
    }
    until([&]
          { return router.sends == 5; },
          "publication request not dispatched", true);
    geocaching::RequestId expected_web_request;
    expected_web_request.bytes = web_operation_id;
    geocaching::protocol::PublishRequestView web_request;
    require(geocaching::protocol::decodePublishRequest({router.sent_bytes.data(), router.sent_bytes.size()}, expected_web_request, web_request),
            "durable web publication changed the caller operation ID");
    check_web_job(2, true, 1, -2);
    if (web_stop)
    {
        auto stop_as_command = [&](const uint8_t* operation, const uint8_t* identity)
        {
            const bool previous_ui = test::in_ui;
            test::in_ui = false;
            const auto result = rt::requestWebPublicationStop(operation, identity);
            test::in_ui = previous_ui;
            return result;
        };
        std::array<uint8_t, 16> empty_operation{};
        require(stop_as_command(empty_operation.data(), author.data()) == rt::WebReadResult::Invalid, "empty stop ID accepted");
        require(stop_as_command(other_operation.data(), author.data()) == rt::WebReadResult::Pending, "stop replaced another active operation");
        require(stop_as_command(web_operation_id.data(), other_author.data()) == rt::WebReadResult::Forbidden, "stop accepted another author");
        const auto sends_before_stop = router.sends;
        const auto saved_waiting_files = test::files;
        auto stopped = rt::WebReadResult::Pending;
        until([&]
              { if (stopped == rt::WebReadResult::Ready) return true; stopped = stop_as_command(web_operation_id.data(), author.data()); return stopped == rt::WebReadResult::Ready; },
              "web publication stop not committed");
        check_web_job(4, true, 1, -2);
        require(stop_as_command(web_operation_id.data(), author.data()) == rt::WebReadResult::Ready, "stop retry changed its result");
        require(router.sends == sends_before_stop, "stopping caused another publication send");
        require(rt::webLease(false), "stop retained web lease");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "stopped publication leaked PSRAM");
        const auto saved_stopped_files = test::files;
        auto historical_stop = rt::WebReadResult::Pending;
        until([&]
              { if (historical_stop == rt::WebReadResult::Ready) return true; historical_stop = stop_as_command(web_operation_id.data(), author.data());
                if (historical_stop != rt::WebReadResult::Ready && historical_stop != rt::WebReadResult::Pending) std::fprintf(stderr, "historical stop status=%u\n", static_cast<unsigned>(historical_stop));
                require(historical_stop == rt::WebReadResult::Ready || historical_stop == rt::WebReadResult::Pending, "historical stop failed");return historical_stop == rt::WebReadResult::Ready; },
              "stopped operation did not survive session reconstruction");
        check_web_job(4, true, 1, -2);
        require(router.sends == sends_before_stop && router.identity.signs == 1 && test::files == saved_stopped_files,
                "historical stop resubmitted, resigned or changed the stopped journal");
        require(rt::webLease(false), "historical stop retained web lease");
        until([&]
              { return test::allocations.empty(); },
              "historical stop leaked PSRAM");
        test::files = saved_waiting_files;
        auto recovered_stop = rt::WebReadResult::Pending;
        until([&]
              {
                  if (recovered_stop == rt::WebReadResult::Ready) return true;
                  recovered_stop = stop_as_command(web_operation_id.data(), author.data());
                  require(recovered_stop == rt::WebReadResult::Ready || recovered_stop == rt::WebReadResult::Pending,
                          "cold waiting stop failed");
                  return recovered_stop == rt::WebReadResult::Ready; },
              "cold waiting publication was not stopped");
        check_web_job(4, true, 1, -2);
        require(router.sends == sends_before_stop && router.identity.signs == 1,
                "cold waiting stop resubmitted or resigned");
        require(rt::webLease(false), "cold waiting stop retained web lease");
        until([&]
              { return test::allocations.empty(); },
              "cold waiting stop leaked PSRAM");
        return 0;
    }
    require(rt::webLease(false), "operation query retained its browser lease");
    require(!test::read_bytes_by_path.empty(), "foreground dispatch read profile was not captured");
    for (const auto& read : test::read_bytes_by_path)
        require(std::find(prior_attempt_shards.begin(), prior_attempt_shards.end(), read.first) == prior_attempt_shards.end() || !read.second,
                "foreground publication scanned historical attempts before sending");
    test::profile_reads = dispatch_profile;
    reply(router, publicationReply(router, 1), 1);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return view.count == 1 && test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 1; },
          "publication receipt not reflected in draft list");
    require(router.identity.signs == 1, "publication signed more than once");
    check_web_job(3, true, 1, 1);
    require(rt::requestWebPublicationStop(web_operation_id.data(), author.data()) == rt::WebReadResult::Ready, "confirmed result rejected a late stop");
    check_web_job(3, true, 1, 1);
    require(rt::webLease(false), "confirmed operation query retained its browser lease");
    rt::WebReadResult historical_result = rt::WebReadResult::Pending;
    until([&]
          {
              if (historical_result != rt::WebReadResult::Pending) return true;
              const bool was_ui = test::in_ui;
              test::in_ui = false;
              historical_result = rt::readWebPublicationHistory(web_operation_id.data(), author.data(), job_response.data(), job_response.size(), job_written);
              test::in_ui = was_ui;
              return historical_result != rt::WebReadResult::Pending; },
          "verified historical operation read stalled");
    require(historical_result == rt::WebReadResult::Ready, "durable publication history unavailable");
    geocaching::protocol::CmpReader historical({job_response.data(), job_written});
    size_t historical_fields = 0;
    uint64_t historical_phase = 0;
    geocaching::ByteView historical_id;
    require(historical.array(historical_fields, 8) && historical_fields == 8 && historical.binary(historical_id, 16) &&
                !std::memcmp(historical_id.data, web_operation_id.data(), 16) && historical.unsignedInteger(historical_phase) && historical_phase == 3,
            "historical operation did not preserve its directory-confirmed phase");
    require(rt::webLease(false), "historical operation query retained its browser lease");
    const auto discovery_before_edit = snapshot(Section::Discover).generation;
    // Exercise the UI field limits and the confirmed-version edit path through
    // the same device owner, not a direct store call.
    const std::string long_name(96, 'N'), long_description(2048, 'D'), long_hint(512, 'H');
    draft.generation = item.edit_generation;
    draft.name = long_name;
    draft.description = long_description;
    draft.hint = long_hint;
    require(test::source->saveDraft(draft), "confirmed draft edit was not queued");
    until([&]
          { return test::source->draftSaveStatus(draft.id, draft.generation) == ui::geocaching::DraftSaveStatus::Saved; },
          "maximum-size draft edit was not saved");
    until([&]
          {
              const auto view = snapshot(Section::Published);
              if (!test::source->item(Section::Published, 0, view.generation, item)) return false;
              draft_generation = item.edit_generation;
              return test::source->publicationAuthor(draft.id, draft_generation, author, &from, &to); },
          "edited draft not ready for publication");
    require(from == 1 && to == 2 && test::source->publishDraft(draft.id, draft_generation, author, to), "second publication confirmation rejected");
    until([&]
          { return router.sends == 6; },
          "second publication request not dispatched");
    reply(router, publicationReply(router, 2), 1);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
          "second publication receipt not reflected in list");
    require(router.identity.signs == 2, "version update signed more than once");
    require(snapshot(Section::Discover).generation == discovery_before_edit &&
                test::source->item(Section::Discover, 0, discovery_before_edit, discover_probe),
            "unrelated publication invalidated unchanged discovery rows");
    // Isolate archive conformance from the following restart fixture, which
    // intentionally retains an active revision-2 draft for its map assertions.
    const auto active_fixture = test::files;
    const auto active_directories = test::directories;
    geocaching::protocol::CmpReader sent({router.sent_bytes.data(), router.sent_bytes.size()});
    size_t sent_count = 0;
    uint64_t sent_value = 0;
    geocaching::ByteView sent_id;
    require(sent.array(sent_count, 6) && sent.unsignedInteger(sent_value) && sent.unsignedInteger(sent_value) &&
                sent.unsignedInteger(sent_value) && sent.binary(sent_id, 16),
            "archive source request invalid");
    geocaching::RequestId prior_request;
    std::memcpy(prior_request.bytes.data(), sent_id.data, 16);
    geocaching::protocol::PublishRequestView published;
    geocaching::protocol::VerifiedRecordView public_record;
    std::vector<uint8_t> detail_scratch(8192), own_detail_response(8192);
    platform::esp::common::EspGeocachingCrypto public_crypto;
    require(geocaching::protocol::decodePublishRequest({router.sent_bytes.data(), router.sent_bytes.size()}, prior_request, published) &&
                geocaching::protocol::verifyGeocache(published.signed_cache, public_crypto, detail_scratch.data(), detail_scratch.size(), public_record) == geocaching::protocol::VerificationResult::Valid,
            "archive base not verified");
    ui::geocaching::Item public_item;
    public_item.id = public_record.id.bytes;
    public_item.revision_hash = public_record.hash.bytes;
    geocaching::protocol::CmpReader signed_reader(published.signed_cache);
    geocaching::ByteView archive_record_bytes, signature;
    require(signed_reader.array(sent_count, 2) && signed_reader.binary(archive_record_bytes, 4096) && signed_reader.binary(signature, 64), "archive signed pair invalid");
    geocaching::protocol::CmpWriter detail_writer(own_detail_response.data(), own_detail_response.size());
    require(detail_writer.array(6) && detail_writer.unsignedInteger(1) && detail_writer.unsignedInteger(1) && detail_writer.unsignedInteger(3) &&
                detail_writer.binary({prior_request.bytes.data(), 16}) && detail_writer.unsignedInteger(200) && detail_writer.array(3) &&
                detail_writer.array(2) && detail_writer.binary(archive_record_bytes) && detail_writer.binary(signature) && detail_writer.unsignedInteger(1) && detail_writer.unsignedInteger(0),
            "archive detail response failed");
    own_detail_response.resize(detail_writer.size());
    test::source->open(public_item, 0);
    until([&]
          { return router.sends == 7; },
          "archive detail not requested");
    reply(router, own_detail_response, 3);
    until([&]
          { return detailText(public_item).status == ui::geocaching::DetailStatus::Ready; },
          "archive detail not verified");
    require(detailText(public_item).can_archive, "owner cannot manage verified public detail");
    auto stale_archive = public_item.revision_hash;
    stale_archive[0] ^= 1;
    require(!test::source->archiveCache(public_item.id, stale_archive), "archive accepted a stale detail");
    // Transport admission may take longer than the response budget. A queued
    // request must still receive a full budget after its eventual submission.
    router.busy_sends_remaining = 8;
    require(test::source->archiveCache(public_item.id, public_item.revision_hash), "public detail archive not queued");
    until([&]
          { return router.sends == 8; },
          "archive request not dispatched");
    require(test::source->archiveStatus(public_item.id) == ui::geocaching::DraftSaveStatus::Pending, "archive confirmed without directory reply");
    reply(router, publicationReply(router, 3), 1);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return test::source->item(Section::Published, 0, view.generation, item) && item.state == 2 &&
                     item.publication_confirmed && item.publication_revision == 3; },
          "archive directory confirmation not reflected in local record");
    require(test::source->archiveStatus(public_item.id) == ui::geocaching::DraftSaveStatus::Saved, "archive detail did not show confirmation");
    // Let wall time advance: a retry must reuse the reserved timestamp rather
    // than construct different content for the same successor version.
#ifdef _WIN32
    Sleep(1100);
#else
    usleep(1100000);
#endif
    require(test::source->archiveCache(public_item.id, public_item.revision_hash), "archive retry not queued");
    const auto retry_sends = router.sends;
    until([&]
          { return router.sends > retry_sends || test::source->archiveStatus(public_item.id) != ui::geocaching::DraftSaveStatus::Pending; },
          "archive retry stuck");
    require(test::source->archiveStatus(public_item.id) != ui::geocaching::DraftSaveStatus::Failed, "timestamp change broke archive retry");
    if (router.sends > retry_sends)
    {
        reply(router, publicationReply(router, 3), 1);
        until([&]
              { return test::source->archiveStatus(public_item.id) == ui::geocaching::DraftSaveStatus::Saved; },
              "archive retry not confirmed");
    }
    auto archive_overlay = std::make_unique<ui::geocaching::LocalMapOverlay>();
    auto archive_map = std::make_unique<ui::map::MapOverlaySnapshot>();
    until([&]
          {
              archive_overlay->update(*test::source, 31, 121, 15);
              archive_map = std::make_unique<ui::map::MapOverlaySnapshot>();
              archive_overlay->append(*archive_map);
              return archive_overlay->finished(); },
          "archive map did not finish local metadata");
    require(archive_map->item_count == 1, "archived local cache remained on Map or hid the downloaded cache");
    test::source->requestWindow(Section::Downloaded, 0, 4);
    ui::geocaching::Item offline_copy;
    until([&]
          { const auto view = snapshot(Section::Downloaded); return view.ready && test::source->item(Section::Downloaded, 0, view.generation, offline_copy); },
          "offline removal row missing");
    require(test::source->removeDownloaded(offline_copy), "offline removal not queued");
    until([&]
          { return test::source->downloadedRemovalStatus(offline_copy.id, offline_copy.revision_hash) != ui::geocaching::DraftSaveStatus::Pending; },
          "offline removal stuck");
    require(test::source->downloadedRemovalStatus(offline_copy.id, offline_copy.revision_hash) == ui::geocaching::DraftSaveStatus::Saved, "offline removal failed");
    until([&]
          { const auto view = snapshot(Section::Downloaded); return view.ready && !view.count; },
          "deleted offline copy remained in Downloaded");
    until([&]
          { archive_overlay->update(*test::source, 31, 121, 15); archive_map->item_count = 0; archive_overlay->append(*archive_map); return archive_overlay->finished() && !archive_map->item_count; },
          "same-viewport map retained deleted offline marker");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "session did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "session leaked buffers or file handles");
    require(!router.service && !router.announcement && !router.delivery, "session did not release background transport");
    test::files = active_fixture;
    test::directories = active_directories;
    const auto disk = test::files;
    const auto disk_directories = test::directories;
    std::fprintf(stderr, "Runtime: healthy restart\n");
    // Cold offline startup has no dispatch destination or QueryClient yet.
    // Saved rows and full details must not wait for either to become available.
    router.ready = false;
    // Open the main map directly, without warming either Geocaching list.
    // Include a synthetic per-operation storage cost; UI refresh deadlines stay
    // at 750 ms instead of adding a fresh 750 ms after background work finishes.
    test::io_delay_ms = std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS")
                            ? static_cast<uint32_t>(std::strtoul(std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS"), nullptr, 10))
                            : 5;
    {
        test::source->activate(true);
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        const auto map_started = test::clock_ms;
        const auto map_io_started = test::io_operations;
        const bool previous_profile = test::profile_reads;
        test::profile_reads = true;
        // Diagnostic override only. The default models the production Map
        // timer; faster polling must not silently relax the acceptance test.
        const auto refresh_ms = std::getenv("TRAIL_MATE_TEST_MAP_REFRESH_MS")
                                    ? std::max(1UL, std::strtoul(std::getenv("TRAIL_MATE_TEST_MAP_REFRESH_MS"), nullptr, 10))
                                    : ui::geocaching::LocalMapOverlay::kPollIntervalMs;
        test::read_bytes_by_path.clear();
        for (unsigned frame = 0; frame < 100 && map->item_count != 2; ++frame)
        {
            const auto refresh_due = test::clock_ms + refresh_ms;
            overlays->update(*test::source, 31, 121, 15);
            map = std::make_unique<ui::map::MapOverlaySnapshot>();
            overlays->append(*map);
            if (test::profile_reads)
                std::fprintf(stderr, "Map refresh: elapsed_ms=%llu markers=%u io_operations=%llu\n",
                             static_cast<unsigned long long>(test::clock_ms - map_started), static_cast<unsigned>(map->item_count),
                             static_cast<unsigned long long>(test::io_operations - map_io_started));
            if (map->item_count == 2) break;
            while (test::clock_ms < refresh_due) tick();
        }
        require(map->item_count == 2 && map->header.valid, "direct main map did not load local markers");
        for (const auto& read : test::read_bytes_by_path)
            require(read.first.find("/05/") == std::string::npos && read.first.find("/0a/") == std::string::npos,
                    "main Map audited publication requests or parent tasks");
        test::profile_reads = previous_profile;
        std::fprintf(stderr, "Direct Map cold-start acceptance: elapsed_ms=%llu budget_ms=3000 io_delay_ms=%u\n",
                     static_cast<unsigned long long>(test::clock_ms - map_started), test::io_delay_ms);
        std::fprintf(stderr, "Direct Map I/O operations=%llu\n", static_cast<unsigned long long>(test::io_operations - map_io_started));
        if (test::profile_reads)
            for (const auto& read : test::read_bytes_by_path)
                std::fprintf(stderr, "  %llu bytes %s\n", static_cast<unsigned long long>(read.second), read.first.c_str());
        require(test::clock_ms - map_started <= 3000, "main map exceeded the three-second local marker budget");
        test::source->activate(false);
        tick();
        until([&]
              { return std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
              "direct map session did not close");
    }
    const auto installed_gpx = std::find_if(disk.begin(), disk.end(), [](const auto& file)
                                            { return file.first.find("/.state/") == std::string::npos && file.first.size() >= 4 && file.first.substr(file.first.size() - 4) == ".gpx"; });
    require(installed_gpx != disk.end(), "restart test requires an installed GPX");
    // Visiting Discover must not make subsequent offline catalog reads depend
    // on auditing unrelated network history.
    {
        test::source->activate(true);
        tick();
        (void)snapshot(Section::Discover);
        tick();
        const auto started = test::clock_ms;
        const auto payload_allocations = test::payload_allocations;
        test::source->requestWindow(Section::Downloaded, 0, 4);
        until([&]
              { const auto view = snapshot(Section::Downloaded); return view.ready && view.count == 1 &&
                       test::source->item(Section::Downloaded, 0, view.generation, item); },
              "Downloaded did not load after visiting Discover");
        require(test::payload_allocations == payload_allocations, "Downloaded allocated a download response buffer");
        test::source->requestWindow(Section::Published, 0, 4);
        until([&]
              { const auto view = snapshot(Section::Published); return view.ready && view.count == 1 &&
                       test::source->item(Section::Published, 0, view.generation, item) && item.has_coordinates; },
              "local draft did not load after visiting Discover");
        std::fprintf(stderr, "Discover to local acceptance: elapsed_ms=%llu budget_ms=3000 io_delay_ms=%u\n",
                     static_cast<unsigned long long>(test::clock_ms - started), test::io_delay_ms);
        require(test::clock_ms - started <= 3000, "visiting Discover made local catalogs wait for full audit");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty() && !router.service &&
                       std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
              "Discover to local session did not close");
    }
    test::fail_read_path = installed_gpx->first;
    test::busy_reads = 12;
    test::busy_read_path = "/trailmate/geocaching/.state/index/root.h0";
    test::source->activate(true);
    tick();
    test::source->requestWindow(Section::Downloaded, 0, 4);
    const auto local_list_started = test::clock_ms;
    until([&]
          {
              const auto view = snapshot(Section::Downloaded);
              return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.id == saved_id && item.downloaded; },
          "restart lost downloaded map row");
    require(!test::busy_reads, "startup did not wait through contended volume reads");
    test::busy_read_path.clear();
    require((test::clock_ms - local_list_started) / tick_ms < 850,
            "local startup regressed into duplicate index scans or blocking download recovery");
    {
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        test::fail_read = true;
        test::busy_reads = 12;
        until([&]
              {
                  overlays->update(*test::source, 31, 121, 15);
                  require(!std::strstr(snapshot(Section::Published).status.data(), "Restoring geocaching tasks"),
                          "one local metadata read failure restarted full storage recovery");
                  map = std::make_unique<ui::map::MapOverlaySnapshot>();
                  overlays->append(*map);
                  return map->item_count == 2; },
              "cold offline map lost local cache markers");
        require(!test::fail_read, "map did not encounter the metadata read failure");
        require(!test::busy_reads, "map did not encounter all contended metadata reads");
        ui::geocaching::Item local_draft;
        const auto local_view = snapshot(Section::Published);
        require(local_view.ready && test::source->item(Section::Published, 0, local_view.generation, local_draft) &&
                    local_draft.has_coordinates && std::strstr(local_draft.detail.data(), "Checking publication status"),
                "map waited for publication history instead of showing committed draft metadata");
        require(!router.service && !router.announcement && !router.delivery,
                "local map started network discovery");
    }
    require(test::fail_read_path == installed_gpx->first, "completed download reopened GPX during startup or listing");
    {
        const auto elapsed = test::clock_ms - local_list_started;
        std::fprintf(stderr, "Local cold-start acceptance: elapsed_ms=%llu budget_ms=3000 io_delay_ms=%u\n",
                     static_cast<unsigned long long>(elapsed), test::io_delay_ms);
        require(elapsed <= 3000, "two offline local markers exceeded the three-second startup budget");
    }
    test::io_delay_ms = 0;
    test::fail_read_path.clear();
    require(test::files == disk, "read-only restart changed persisted files");
    require(!router.service && !router.announcement && !router.delivery, "local list started a network service");
    const auto offline_sends = router.sends;
    test::source->open(item, snapshot(Section::Downloaded).generation);
    until([&]
          { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
          "saved detail unavailable offline after restart");
    require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
            "saved detail lost description or hint");
    require(router.sends == offline_sends && test::files == disk, "offline detail used network or rewrote storage");
    require(!router.service, "local detail started a network service");
    test::source->closeDetail();
    tick();
    test::source->requestWindow(Section::Published, 0, 4);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return view.count == 1 && test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
          "restart lost publication confirmation");
    require(!router.service, "local publication list started a network service");
    router.ready = true;
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "offline session did not start discovery on demand");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "restored session did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "restored session leaked resources");
    // External USB ownership can arrive during an asynchronous query write.
    // Closing the UI must neither touch the card nor busy-spin during it.
    // Browse refresh is independent of USB ownership and SD workspace allocation.
    test::external_owner = true;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "USB browse did not open");
    const auto no_sd = test::files;
    const auto before_sends = router.sends;
    announce(router);
    // The received announcement itself now lives in PSRAM. Once copied,
    // processing it must not acquire any SD workspace or further allocations.
    test::memory_available = false;
    until([&]
          { return router.sends == before_sends + 1; },
          "browse allocated SD workspace");
    require(test::files == no_sd && !test::blocked_io, "USB browse accessed SD");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "USB browse did not close");
    test::memory_available = true;
    test::external_owner = false;
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "browse resources leaked");
    ui::geocaching::Snapshot item_snapshot;
    // Corrupt derived metadata must be rebuilt from authoritative facts. A
    // transient read failure must instead preserve the tree and report I/O.
    {
        auto shard = disk.end();
        for (auto file = disk.begin(); file != disk.end(); ++file)
            if (file->first.find("/index/a/04/") != std::string::npos && file->first.find(".gci.c") != std::string::npos &&
                (shard == disk.end() || file->first > shard->first)) shard = file;
        require(shard != disk.end(), "local recovery test requires a draft shard");
        test::files = disk;
        test::directories = disk_directories;
        test::files.at(shard->first).back() ^= 1;
        const auto sends_before = router.sends;
        test::source->activate(true);
        tick();
        until([&]
              { const auto view = snapshot(Section::Published); return view.ready && view.count == 1 &&
                       test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed; },
              "offline corrupt draft did not fall back to verified index repair");
        require(router.sends == sends_before && !router.service, "offline index repair started networking");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty() && std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
              "offline repaired session did not close");
    }
    unsigned damaged_files = 0;
    std::fprintf(stderr, "Runtime: startup fault injection\n");
    for (unsigned fault = 0; fault < 3; ++fault)
        for (const auto& entry : disk)
        {
            if (entry.first.find("/.state/index/") == std::string::npos || entry.second.empty()) continue;
            if (fault == 2 && entry.first.substr(entry.first.size() - 4) != ".gci") continue;
            test::files = disk;
            test::directories = disk_directories;
            if (fault == 0) test::files[entry.first].back() ^= 1;
            else if (fault == 1) test::files[entry.first].pop_back();
            else test::fail_read_path = entry.first;
            const char* expected = fault == 2 ? "Cannot read cached storage" : "";
            const auto damaged = test::files;
            std::fprintf(stderr, "Startup fault %u: %s\n", fault, entry.first.c_str());
            test::source->activate(true);
            tick();
            test::source->snapshot(Section::Discover, item_snapshot);
            test::source->snapshot(Section::Published, item_snapshot);
            until([&]
                  {
                  const auto view = snapshot(Section::Published);
                      return view.can_create || std::strstr(view.status.data(), "Cannot read cached storage") || std::strstr(view.status.data(), "Cached index needs recovery"); },
                  "damaged-index startup did not reach a terminal status");
            if (snapshot(Section::Published).can_create)
            {
                // An actual write request requires full audit. Deliberately
                // stale deletion exercises that boundary without changing data.
                const uint64_t stale_generation = UINT64_MAX - 1;
                require(test::source->deleteDraft(draft.id, stale_generation), "audit probe was not queued");
                until([&]
                      { const auto view = snapshot(Section::Published); return
                               test::source->draftSaveStatus(draft.id, stale_generation) == ui::geocaching::DraftSaveStatus::Failed ||
                               std::strstr(view.status.data(), "Cannot read cached storage") ||
                               std::strstr(view.status.data(), "Cached index needs recovery"); },
                      "write request did not complete index audit");
            }
            require(fault == 2 ? std::strstr(snapshot(Section::Published).status.data(), expected) != nullptr : snapshot(Section::Published).can_create,
                    "index repair did not reach the expected result");
            require(test::fail_read_path.empty(), "startup did not reach the injected read failure");
            if (fault == 2) require(test::files == damaged, "I/O failure modified persistent evidence");
            else
            {
                for (const auto& original : disk)
                    if (original.first.find("/.state/index/") == std::string::npos)
                        require(test::files.at(original.first) == original.second, "index repair changed authoritative data");
                require(!test::directories.count("/trailmate/geocaching/.state/index.repair"), "verified repair retained its temporary archive");
                test::source->requestWindow(Section::Downloaded, 0, 4);
                until([&]
                      { const auto view = snapshot(Section::Downloaded); return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.id == saved_id && item.downloaded; },
                      "rebuild lost the downloaded map row");
                test::source->requestWindow(Section::Published, 0, 4);
                until([&]
                      { const auto view = snapshot(Section::Published); return view.count == 1 && test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
                      "rebuild lost the confirmed publication");
            }
            test::source->activate(false);
            tick();
            until([&]
                  { return !router.service && test::allocations.empty() &&
                           std::strstr(snapshot(Section::Discover).status.data(), "Starting Geocaching"); },
                  "damaged-index session did not close");
            require(test::allocations.empty() && !test::open_files && !test::open_dirs, "damaged-index recovery leaked resources");
            ++damaged_files;
        }
    require(damaged_files > 3, "damaged-index cases did not cover persisted shards");
    test::files = disk;
    test::directories = disk_directories;
    test::source->activate(true);
    tick();
    test::source->snapshot(Section::Published, item_snapshot);
    until([&]
          { return snapshot(Section::Published).can_create; },
          "healthy storage did not recover after failed startups");
    require(test::files == disk, "healthy recovery after faults changed persistent data");
    test::source->activate(false);
    until([&]
          { return !router.service && std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
          "healthy session after faults did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "healthy recovery after faults leaked resources");
    // Capture actual disk states from the device startup, then recreate the
    // Session from each interrupted state. Closing the old owner is only host
    // cleanup; its post-close files are replaced by the captured crash image.
    struct CrashImage
    {
        decltype(test::files) files;
        decltype(test::directories) directories;
        bool captured = false;
    };
    std::array<CrashImage, 8> cuts;
    const std::string index = "/trailmate/geocaching/.state/index/";
    const std::string archive = "/trailmate/geocaching/.state/index.repair/";
    auto rootSequence = [&](const std::string& path)
    {
        const auto found = test::files.find(path);
        uint64_t sequence = 0;
        if (found != test::files.end() && found->second.size() == 468)
            for (unsigned i = 28; i < 36; ++i) sequence = (sequence << 8) | found->second[i];
        return sequence;
    };
    auto closeRuntime = [&]
    {
        test::source->activate(false);
        tick();
        until([&]
              { return !router.service && test::allocations.empty() &&
                       std::strstr(snapshot(Section::Discover).status.data(), "Starting Geocaching"); },
              "interrupted session did not release resources");
        require(!test::open_files && !test::open_dirs, "interrupted session leaked file handles");
    };
    test::files = disk;
    test::directories = disk_directories;
    const auto committed = std::max(rootSequence(index + "root.h0"), rootSequence(index + "root.h1"));
    size_t original_index_files = 0;
    for (const auto& file : disk)
        if (!file.first.compare(0, index.size(), index)) ++original_index_files;
    test::files[index + "root.h0"].back() ^= 1;
    test::source->activate(true);
    tick();
    test::source->snapshot(Section::Published, item_snapshot);
    until([&]
          {
              const bool archived = test::directories.count("/trailmate/geocaching/.state/index.repair");
              const bool primary = test::directories.count("/trailmate/geocaching/.state/index");
              size_t archive_files = 0;
              bool marker = false, empty_shard = false, unheaded_shard = false;
              for (const auto& file : test::files)
              {
                  if (!file.first.compare(0, archive.size(), archive)) ++archive_files;
                  marker |= !file.first.compare(0, archive.size() + 5, archive + "gcf1-");
                  if (!file.first.compare(0, index.size(), index) && file.first.find(".gci.h0") != std::string::npos)
                      unheaded_shard |= !test::files.count(file.first.substr(0, file.first.size() - 1) + '1');
                  if (file.first.compare(0, index.size(), index) || file.first.substr(file.first.size() - 4) != ".gci") continue;
                  empty_shard |= file.second.empty();
              }
              const auto first = test::files.find(index + "root.h0");
              const auto sequence = std::max(rootSequence(index + "root.h0"), rootSequence(index + "root.h1"));
              const bool points[] = {archived && !primary && !marker, archived && marker && !primary,
                                     primary && first != test::files.end() && first->second.empty(),
                                     primary && first != test::files.end() && first->second.size() == 468 && !test::files.count(index + "root.h1"),
                                     archived && empty_shard, archived && unheaded_shard,
                                     archived && sequence > 0 && sequence < committed,
                                     archived && sequence >= committed && archive_files < original_index_files};
              for (size_t i = 0; i < cuts.size(); ++i)
                  if (points[i] && !cuts[i].captured) cuts[i] = {test::files, test::directories, true};
              return snapshot(Section::Published).can_create; },
          "repair baseline for interruption testing failed");
    closeRuntime();
    for (size_t cut = 0; cut < cuts.size(); ++cut)
    {
        require(cuts[cut].captured, "repair interruption point was not observed");
        std::fprintf(stderr, "Runtime: resume repair cut %u\n", static_cast<unsigned>(cut));
        test::files = cuts[cut].files;
        test::directories = cuts[cut].directories;
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "interrupted repair did not resume");
        require(!test::directories.count("/trailmate/geocaching/.state/index.repair"), "resumed repair retained archive");
        for (const auto& original : disk)
            if (original.first.compare(0, index.size(), index))
                require(test::files.at(original.first) == original.second, "resumed repair changed authoritative data");
        closeRuntime();
    }
    // Remove the last complete journal transaction. Rebuilding may produce a
    // valid older root, but must not admit it below the durable repair floor.
    test::files = disk;
    test::directories = disk_directories;
    std::string journal;
    for (const auto& file : disk)
        if (file.first.find("/.state/journal/") != std::string::npos) journal = file.first;
    require(!journal.empty(), "journal fixture missing");
    auto& journal_bytes = test::files[journal];
    size_t offset = 0, last_frame = 0;
    while (offset + 24 <= journal_bytes.size())
    {
        uint32_t payload = 0;
        for (unsigned i = 8; i < 12; ++i) payload = (payload << 8) | journal_bytes[offset + i];
        last_frame = offset;
        offset += 24 + payload;
    }
    require(offset == journal_bytes.size() && offset, "journal framing did not match the fixture");
    journal_bytes.resize(last_frame);
    const auto incomplete_journal = journal_bytes;
    if (!last_frame) test::files.erase(journal);
    // Keep both committed root witnesses intact while damaging one shard.
    for (auto& file : test::files)
        if (!file.first.compare(0, index.size(), index) && file.first.substr(file.first.size() - 4) == ".gci")
        {
            file.second.back() ^= 1;
            break;
        }
    for (unsigned restart = 0; restart < 2; ++restart)
    {
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return std::strstr(snapshot(Section::Published).status.data(), "Cached index needs recovery"); },
              "missing committed journal tail was accepted");
        require(last_frame ? test::files.at(journal) == incomplete_journal : !test::files.count(journal), "failed repair modified incomplete journal");
        require(test::directories.count("/trailmate/geocaching/.state/index.repair"), "failed repair lost its archive");
        closeRuntime();
        // The durable floor must survive even if the old root copies are lost.
        test::files.erase(archive + "root.h0");
        test::files.erase(archive + "root.h1");
    }
    test::files[journal] = disk.at(journal);
    test::source->activate(true);
    tick();
    test::source->snapshot(Section::Published, item_snapshot);
    until([&]
          { return snapshot(Section::Published).can_create; },
          "restored authoritative journal did not finish repair");
    closeRuntime();
    std::puts("Interrupted repair resumed at 8 disk states; missing committed journal tail rejected across restarts");
    std::printf("Startup handled %u index corruption/truncation/read-failure cases\n", damaged_files);
    require(!test::ui_io, "interface callback touched storage");
    // Populate genuine journal/index history, then let the production session
    // trigger maintenance through its normal public storage owner scheduling.
    {
        namespace sd = platform::esp::arduino_common::geocaching;
        namespace gc = geocaching::storage;
        test::in_ui = false;
        gc::VolumeInstance volume;
        require(sd::inspectSdVolume(volume) == sd::SdVolumeResult::Ready, "maintenance volume missing");
        std::array<gc::IndexRootBytes, 2> roots;
        gc::IndexRootView first, second, root;
        for (unsigned i = 0; i < 2; ++i)
        {
            const auto& bytes = test::files.at(index + "root.h" + std::to_string(i));
            require(bytes.size() == roots[i].size(), "maintenance root size mismatch");
            std::copy(bytes.begin(), bytes.end(), roots[i].begin());
        }
        require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, first) &&
                    gc::decodeIndexRoot({roots[1].data(), roots[1].size()}, volume, second) && gc::selectIndexRoot(first, second, root),
                "maintenance root selection failed");
        unsigned copy = second.revision > first.revision ? 1 : 0;
        std::array<uint8_t, 8192> frame;
        std::array<uint8_t, 32> key{};
        key.fill(0xcc);
        const uint8_t value[] = {0x92, 0x90, 0x90};
        const gc::MutationView row{11, {key.data(), key.size()}, {value, sizeof(value)}, false};
        while (root.sequence < 260)
        {
            sd::SdIndexedCommit commit(volume);
            require(commit.begin(root, copy, &row, 1, frame.data(), frame.size(), roots[1 - copy]), "maintenance fixture commit rejected");
            auto status = sd::IndexedCommitStep::Working;
            for (unsigned i = 0; i < 100000 && status == sd::IndexedCommitStep::Working; ++i) status = commit.step();
            require(status == sd::IndexedCommitStep::Verified && commit.committed(root), "maintenance fixture commit failed");
            copy = 1 - copy;
        }
        const char old_slot = root.slot;
        const auto requireWritableStorage = [&]
        {
            // Read-only directory browsing must not start checkpoint work.
            // A stale write admits maintenance without changing saved content.
            const uint64_t stale_generation = UINT64_MAX - 1;
            require(test::source->deleteDraft(draft.id, stale_generation), "maintenance audit request rejected");
            until([&]
                  { return test::source->draftSaveStatus(draft.id, stale_generation) == ui::geocaching::DraftSaveStatus::Failed; },
                  "maintenance audit request did not finish");
        };
        const auto initial_sends = router.sends;
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "maintenance fixture did not open");
        requireWritableStorage();
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "maintenance network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == initial_sends + 1; },
              "maintenance capabilities request missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == initial_sends + 2; },
              "maintenance query request missing");
        reply(router, fixture(folder, "query-response-v1.bin"), 2);
        until([&]
              { return snapshot(Section::Discover).can_refresh; },
              "maintenance query did not finish");
        until([&]
              { return test::files.count("/trailmate/geocaching/.state/checkpoint/a.gcs") &&
                       !test::directories.count(index + old_slot) && !test::allocated("geocaching.index.read"); },
              "production session did not rotate checkpoint and release workspace");
        require(test::files.at(index + "root.h0") == test::files.at(index + "root.h1"), "session maintenance left different roots");
        require(snapshot(Section::Discover).count == 1, "maintenance lost current query page");
        closeRuntime();
        // A recently completed checkpoint is no longer eligible on restart.
        // Add real committed history before testing foreground interruption.
        appendMaintenanceHistory(256);
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "session checkpoint failed restart recovery");
        requireWritableStorage();
        const auto resumed_sends = router.sends;
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "resumed network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == resumed_sends + 1; },
              "yield test capabilities missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == resumed_sends + 2; },
              "yield test query missing");
        reply(router, fixture(folder, "query-response-v1.bin"), 2);
        until([&]
              { return snapshot(Section::Discover).can_refresh && test::files.count("/trailmate/geocaching/.state/staging/checkpoint.gcs"); },
              "yield test did not enter checkpoint build");
        ui::geocaching::DraftInput foreground;
        foreground.name = "Saved while maintenance was running";
        require(test::source->saveDraft(foreground), "maintenance rejected foreground draft");
        const auto queued_at = test::clock_ms;
        until([&]
              { return test::source->draftSaveStatus(foreground.id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
              "maintenance did not yield to draft save");
        require(test::clock_ms - queued_at < 10000, "foreground waited for the full checkpoint rotation");
        require(!test::files.count("/trailmate/geocaching/.state/checkpoint/b.gcs"), "foreground unnecessarily waited for checkpoint publication");
        closeRuntime();
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "pagination restart failed");
        requireWritableStorage();
        const auto paging_sends = router.sends;
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "pagination network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == paging_sends + 1; },
              "pagination capabilities missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == paging_sends + 2; },
              "pagination query missing");
        reply(router, paginationFixture(folder, false), 2);
        // The previous interrupted builder left a staging file. Wait for the
        // next builder to remove and recreate it, proving maintenance is
        // running now instead of mistaking that old file for new progress.
        bool removed_staging = false;
        until([&]
              {
            const auto found = test::files.find("/trailmate/geocaching/.state/staging/checkpoint.gcs");
            if (found == test::files.end()) removed_staging = true;
            return removed_staging && snapshot(Section::Discover).has_more && found != test::files.end() && found->second.empty(); },
              "pagination test did not enter maintenance");
        const auto rows_before_pagination = snapshot(Section::Discover).generation;
        require(test::source->loadMore(), "maintenance rejected pagination intent");
        require(!test::source->loadMore() && !snapshot(Section::Discover).has_more, "duplicate pagination was accepted");
        require(snapshot(Section::Discover).generation == rows_before_pagination,
                "pagination status invalidated the retained query page");
        require(!test::ui_io, "pagination callback touched storage");
        until([&]
              { return router.sends == paging_sends + 3; },
              "queued pagination was not dispatched");
        require(!test::files.count("/trailmate/geocaching/.state/checkpoint/b.gcs"), "pagination waited for checkpoint publication");
        reply(router, paginationFixture(folder, true), 2);
        until([&]
              { const auto view = snapshot(Section::Discover); return view.can_refresh && !view.has_more && view.count == 0; },
              "final pagination response was not displayed");
        require(router.sends == paging_sends + 3, "pagination request sent twice");
        require(snapshot(Section::Discover).generation != rows_before_pagination &&
                    !test::source->item(Section::Discover, 0, rows_before_pagination, discover_probe),
                "replacement query page retained a stale selection");
        closeRuntime();
        const auto journalCount = []
        {
            size_t count = 0;
            for (const auto& file : test::files)
                if (file.first.find("/journal/") != std::string::npos) ++count;
            return count;
        };
        const auto journals_before = journalCount();
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "reclamation session did not recover");
        requireWritableStorage();
        const auto reclaim_sends = router.sends;
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "reclamation network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == reclaim_sends + 1; },
              "reclamation capabilities missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == reclaim_sends + 2; },
              "reclamation query missing");
        reply(router, fixture(folder, "query-response-v1.bin"), 2);
        until([&]
              { return test::files.count("/trailmate/geocaching/.state/checkpoint/b.gcs") &&
                       journalCount() < journals_before && !test::allocated("geocaching.index.read") &&
                       test::files.at(index + "root.h0") == test::files.at(index + "root.h1"); },
              "production session did not reclaim covered journals");
        std::fprintf(stderr, "Runtime reclaimed journals: %u -> %u\n", static_cast<unsigned>(journals_before), static_cast<unsigned>(journalCount()));
        closeRuntime();
        // Force startup to use the older checkpoint and retained log suffix.
        // Verify business state through the same UI source the device uses.
        test::files.at("/trailmate/geocaching/.state/checkpoint/b.gcs").back() ^= 1;
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "reclaimed journal fallback could not recover session");
        test::source->requestWindow(Section::Downloaded, 0, 4);
        until([&]
              { const auto view = snapshot(Section::Downloaded); return view.count == 1 &&
                       test::source->item(Section::Downloaded, 0, view.generation, item) && item.id == saved_id && item.downloaded; },
              "reclaimed journal fallback lost downloaded cache");
        test::source->requestWindow(Section::Published, 0, 4);
        until([&]
              {
                  const auto view = snapshot(Section::Published);
                  if (view.count != 2) return false;
                  bool confirmed = false, foreground_saved = false;
                  for (size_t i = 0; i < view.count; ++i)
                  {
                      if (!test::source->item(Section::Published, i, view.generation, item)) return false;
                      if (std::equal(draft.id.begin(), draft.id.end(), item.id.begin()))
                          confirmed = item.publication_confirmed && item.publication_revision == 2;
                      if (std::equal(foreground.id.begin(), foreground.id.end(), item.id.begin())) foreground_saved = item.is_draft;
                  }
                  return confirmed && foreground_saved; },
              "reclaimed journal fallback lost publication or foreground draft");
        size_t preserved_gpx = 0;
        for (const auto& file : disk)
            if (file.first.size() >= 4 && file.first.substr(file.first.size() - 4) == ".gpx")
            {
                require(test::files.at(file.first) == file.second, "reclamation modified installed GPX");
                ++preserved_gpx;
            }
        require(preserved_gpx > 0, "reclamation test had no installed GPX to verify");
        closeRuntime();
    }
    // Deleting a local draft is an offline transaction and survives restart.
    test::source->activate(true);
    tick();
    until([&]
          { return snapshot(Section::Published).can_create; },
          "delete session did not open");
    test::source->requestWindow(Section::Published, 0, 4);
    ui::geocaching::Item deleting;
    until([&]
          { const auto view = snapshot(Section::Published); return view.ready && test::source->item(Section::Published, 0, view.generation, deleting); },
          "delete row missing");
    std::array<uint8_t, 16> delete_id;
    std::copy_n(deleting.id.data(), delete_id.size(), delete_id.data());
    const auto count_before_delete = snapshot(Section::Published).count;
    const auto delete_sends = router.sends;
    require(test::source->deleteDraft(delete_id, deleting.edit_generation + 1), "stale delete was not queued");
    until([&]
          { return test::source->draftSaveStatus(delete_id, deleting.edit_generation + 1) != ui::geocaching::DraftSaveStatus::Pending; },
          "stale delete did not finish");
    require(test::source->draftSaveStatus(delete_id, deleting.edit_generation + 1) == ui::geocaching::DraftSaveStatus::Failed, "stale version deleted a cache");
    require(snapshot(Section::Published).count == count_before_delete, "failed delete changed the catalog");
    require(test::source->deleteDraft(delete_id, deleting.edit_generation), "delete was not queued");
    until([&]
          { return test::source->draftSaveStatus(delete_id, deleting.edit_generation) != ui::geocaching::DraftSaveStatus::Pending; },
          "delete did not finish");
    require(test::source->draftSaveStatus(delete_id, deleting.edit_generation) == ui::geocaching::DraftSaveStatus::Saved, "delete failed");
    until([&]
          { const auto view = snapshot(Section::Published); return view.ready && view.count == count_before_delete - 1; },
          "deleted row stayed in the open list");
    closeRuntime();
    test::source->activate(true);
    tick();
    until([&]
          { const auto view = snapshot(Section::Published); return view.ready && view.count == count_before_delete - 1; },
          "deleted row returned after restart");
    require(router.sends == delete_sends, "local deletion started network traffic");
    closeRuntime();
    // A missing directory response has a bounded lifetime without any SD work.
    const auto disk_before_timeout = test::files;
    test::card_ready = false;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "timeout session did not open");
    const auto sends_before_timeout = router.sends;
    announce(router);
    until([&]
          { return router.sends > sends_before_timeout; },
          "timeout query missing");
    const auto retry_request = router.sent_bytes;
    tick_ms = 1000;
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Directory did not reply"); },
          "query did not time out");
    require(router.sends > sends_before_timeout + 1 && router.sends <= sends_before_timeout + 9 && router.sent_bytes == retry_request,
            "query retries were unbounded or changed the request");
    const auto stopped_sends = router.sends;
    for (unsigned i = 0; i < 20; ++i) tick();
    require(router.sends == stopped_sends && test::files == disk_before_timeout && !test::blocked_io,
            "timed out browsing retried or accessed SD");
    tick_ms = 5;
    closeRuntime();
    std::puts("Production runtime discovery/download/publication/GPX/restart/ownership/allocation/checkpoint rotation passed");
}
