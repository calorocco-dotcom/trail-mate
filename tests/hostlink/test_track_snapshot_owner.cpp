#include "chat/ports/i_mesh_adapter.h"
#include "hostlink/management_envelope.h"
#include "platform/esp/arduino_common/route_package_image_runtime.h"
#include "platform/esp/common/storage/track_snapshot_service.h"
#include "platform/esp/idf_common/hostlink_route_install_identity.h"
#include "platform/esp/idf_common/hostlink_route_install_owner.h"
#include "platform/esp/idf_common/hostlink_route_install_runtime.h"
#include "platform/esp/idf_common/hostlink_track_attachment_prepare_owner.h"
#include "platform/esp/idf_common/hostlink_track_history_authentication.h"
#include "platform/esp/idf_common/hostlink_track_history_query.h"
#include "platform/esp/idf_common/hostlink_track_ip_confirmation_owner.h"
#include "platform/esp/idf_common/hostlink_track_ip_confirmation_runtime.h"
#include "platform/esp/idf_common/hostlink_track_kml_retention.h"
#include "platform/esp/idf_common/hostlink_track_object_admission.h"
#include "platform/esp/idf_common/hostlink_track_package_input_owner.h"
#include "platform/esp/idf_common/hostlink_track_package_route_publication.h"
#include "platform/esp/idf_common/hostlink_track_prepare_owner.h"
#include "platform/esp/idf_common/hostlink_track_prepare_persistence.h"
#include "platform/esp/idf_common/hostlink_track_prepare_runtime.h"
#include "platform/esp/idf_common/hostlink_track_prepare_work.h"
#include "platform/esp/idf_common/hostlink_track_public_package_retention.h"
#include "platform/esp/idf_common/hostlink_track_publication_admission.h"
#include "platform/esp/idf_common/hostlink_track_publication_execution.h"
#include "platform/esp/idf_common/hostlink_track_publication_request.h"
#include "platform/esp/idf_common/hostlink_track_publication_runtime.h"
#include "platform/esp/idf_common/hostlink_track_read_owner.h"
#include "platform/esp/idf_common/hostlink_track_read_service.h"
#include "platform/esp/idf_common/hostlink_track_retention_admission.h"
#include "platform/esp/idf_common/hostlink_track_snapshot_owner.h"
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
#include "platform/esp/idf_common/hostlink_track_upload_maintenance.h"
#include "platform/esp/idf_common/hostlink_track_upload_owner.h"
#include "platform/esp/idf_common/hostlink_track_upload_runtime.h"
#include "track_sharing/device_signer.h"
#include "track_sharing/file_copy_slice.h"
#include "track_sharing/immutable_file_copy.h"
#include "track_sharing/persist_publication_intent.h"
#include "track_sharing/prepare_upload_inputs.h"
#include "track_sharing/publication_candidate_reader.h"
#include "track_sharing/verified_file_publish.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace hostlink::management;
struct UploadAuthorAdapter
{
    chat::DeviceAuthorResult result = chat::DeviceAuthorResult::Ok;
    uint32_t generation = 7;
    chat::DeviceAuthorResult readDeviceAuthorKey(uint8_t* output, uint32_t& actual)
    {
        std::memset(output, 3, 64);
        actual = generation;
        return result;
    }
};
struct PersistenceAuthor final : chat::IMeshAdapter
{
    uint8_t public_key[64]{}, secret[64]{};
    mutable unsigned signing_calls = 0;
    uint32_t identity_generation = 1;
    chat::DeviceAuthorResult read_failure = chat::DeviceAuthorResult::Ok;
    const chat::lxmf::TrackDeliveryBinding* binding = nullptr;
    bool binding_busy = false, in_visit = false, send_allowed = true;
    unsigned network_sends = 0;
    std::vector<uint8_t> network_wire;
    PersistenceAuthor()
    {
        uint8_t seed[32] = {6};
        public_key[0] = 1;
        ed25519_create_keypair(public_key + 32, secret, seed);
    }
    chat::DeviceAuthorResult readDeviceAuthorKey(uint8_t* output, uint32_t& generation) const override
    {
        if (read_failure != chat::DeviceAuthorResult::Ok) return read_failure;
        std::memcpy(output, public_key, 64);
        generation = identity_generation;
        return chat::DeviceAuthorResult::Ok;
    }
    bool identityStorageError() const { return false; }
    const uint8_t* encryptionPublicKey() const { return public_key; }
    const uint8_t* signingPublicKey() const { return public_key + 32; }
    uint32_t authorGeneration() const { return identity_generation; }
    void combinedPublicKey(uint8_t* output) const { std::memcpy(output, public_key, 64); }
    bool sign(const uint8_t* message, size_t size, uint8_t* signature) const
    {
        ++signing_calls;
        ed25519_sign(signature, message, size, public_key + 32, secret);
        return true;
    }
    chat::DeviceAuthorResult signTrackManifest(const uint8_t* raw, size_t size, uint32_t generation, track_sharing::ManifestView& view, uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity) const override
    {
        return track_sharing::signDeviceTrackManifest(*this, raw, size, generation, view, workspace, capacity, signature, signature_capacity);
    }
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override { return false; }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId, uint32_t, const uint8_t*, size_t, chat::NodeId, bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }
    chat::TrackBindingChange replaceTrackDelivery(const chat::lxmf::TrackDeliveryBinding* expected, const chat::lxmf::TrackDeliveryBinding* replacement) override
    {
        if (binding_busy) return chat::TrackBindingChange::Busy;
        if (binding != expected) return chat::TrackBindingChange::Conflict;
        binding = replacement;
        return chat::TrackBindingChange::Applied;
    }
    bool trackLocalDestination(uint8_t* output) override
    {
        std::memset(output, 0, 16);
        output[0] = 42;
        return true;
    }
    chat::TrackBindingChange visitTrackDelivery(const chat::lxmf::TrackDeliveryBinding* expected, uint8_t* author, uint32_t& generation, chat::TrackBindingVisit visit, void* context) override
    {
        if (binding_busy || read_failure != chat::DeviceAuthorResult::Ok) return chat::TrackBindingChange::Busy;
        if (binding != expected || !expected || !expected->expected_local_author || identity_generation != expected->expected_author_generation ||
            std::memcmp(public_key, expected->expected_local_author, 64)) return chat::TrackBindingChange::Conflict;
        std::memcpy(author, public_key, 64);
        generation = identity_generation;
        assert(!in_visit);
        in_visit = true;
        visit(context);
        in_visit = false;
        return chat::TrackBindingChange::Applied;
    }
    chat::MeshSendResult sendTrackData(const uint8_t*, chat::lxmf::ByteSpan bytes, const uint8_t* author, uint32_t generation, bool response, std::array<uint8_t, 32>*) override
    {
        assert(!in_visit && !response && generation == identity_generation && !std::memcmp(author, public_key, 64));
        ++network_sends;
        network_wire.assign(bytes.data, bytes.data + bytes.size);
        return send_allowed ? chat::MeshSendResult::success(1) : chat::MeshSendResult::fail(chat::MeshOperationFailure::Busy);
    }
};
namespace runtime = hostlink::management::snapshot_runtime;
static std::map<std::string, std::vector<uint8_t>> files;
static std::map<std::string, platform::esp::arduino_common::storage::SdFileReadStatus> directory_status;
static std::vector<std::string> created_directories;
static bool mkdir_fail = false;
static uint32_t media = 1;
static int64_t clock_us = 100000;
static bool held = false, recording = false, allocation_fail = false, remove_fail = false;
static bool backing_allocation_fail = false;
static std::string remove_failure_path;
static size_t write_limit = SIZE_MAX;
static bool flush_failure = false;
static bool rename_failure = false, corrupt_after_rename = false;
static unsigned allocations = 0;
void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (allocation_fail) return nullptr;
    ++allocations;
    return std::malloc(bytes);
}
void heap_caps_free(void* pointer)
{
    --allocations;
    std::free(pointer);
}
int64_t esp_timer_get_time() { return clock_us; }
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
namespace platform::ui::tracker
{
bool try_acquire_snapshot_read()
{
    if (held || recording) return false;
    held = true;
    return true;
}
void release_snapshot_read()
{
    assert(held);
    held = false;
}
} // namespace platform::ui::tracker
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return true; }
uint32_t sd_media_session() { return media; }
bool sd_external_block_owner_active() { return false; }
bool sd_is_directory(const char*) { return true; }
bool sd_mkdir(const char* path)
{
    created_directories.emplace_back(path);
    if (mkdir_fail) return false;
    directory_status[path] = SdFileReadStatus::Ready;
    return true;
}
SdFileReadResult sd_read_file(const char* path, uint8_t* bytes, size_t capacity)
{
    auto found = files.find(path);
    if (found == files.end()) return {SdFileReadStatus::Missing};
    if (!bytes || found->second.empty() || found->second.size() > capacity) return {SdFileReadStatus::Invalid};
    std::memcpy(bytes, found->second.data(), found->second.size());
    return {SdFileReadStatus::Ready};
}
bool sd_remove(const char* path, uint32_t expected)
{
    if (remove_fail || expected != media || remove_failure_path == path) return false;
    return files.erase(path) != 0;
}
bool sd_rename(const char* from, const char* to, uint32_t expected)
{
    if (rename_failure || expected != media || files.count(to) || !files.count(from)) return false;
    files[to] = std::move(files[from]);
    files.erase(from);
    if (corrupt_after_rename && !files[to].empty()) files[to][0] ^= 1;
    return true;
}
class SdRuntimeFile::Impl
{
  public:
    std::string path;
    size_t offset = 0;
    uint32_t session = 0;
    bool open = false;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::SdRuntimeFile(SdRuntimeMemoryPolicy policy) : impl_(backing_allocation_fail ? nullptr : new Impl)
{
    assert(policy == SdRuntimeMemoryPolicy::PsramOnly);
}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char* mode, uint32_t expected)
{
    if (expected != media) return false;
    if (*mode == 'w') files[path].clear();
    else if (!files.count(path)) return false;
    impl_->path = path;
    impl_->offset = 0;
    impl_->session = expected;
    impl_->open = true;
    return true;
}
void SdRuntimeFile::close() { impl_->open = false; }
bool SdRuntimeFile::is_open() const { return impl_->open && impl_->session == media; }
bool SdRuntimeFile::seek(uint64_t offset)
{
    if (!is_open() || offset > files[impl_->path].size()) return false;
    impl_->offset = size_t(offset);
    return true;
}
uint64_t SdRuntimeFile::size() const { return is_open() ? files[impl_->path].size() : 0; }
int SdRuntimeFile::read(void* output, size_t length)
{
    if (!is_open()) return 0;
    auto& bytes = files[impl_->path];
    length = std::min(length, bytes.size() - impl_->offset);
    std::memcpy(output, bytes.data() + impl_->offset, length);
    impl_->offset += length;
    return int(length);
}
size_t SdRuntimeFile::write(const void* input, size_t length)
{
    if (!is_open()) return 0;
    length = std::min(length, write_limit);
    auto& bytes = files[impl_->path];
    bytes.resize(std::max(bytes.size(), impl_->offset + length));
    std::memcpy(bytes.data() + impl_->offset, input, length);
    impl_->offset += length;
    return length;
}
bool SdRuntimeFile::flush() { return is_open() && !flush_failure; }
class SdRuntimeDir::Impl
{
  public:
    size_t index = 0;
    std::string prefix;
};
SdRuntimeDir::SdRuntimeDir() : impl_(new Impl) {}
SdRuntimeDir::SdRuntimeDir(SdRuntimeMemoryPolicy policy) : impl_(backing_allocation_fail ? nullptr : new Impl)
{
    assert(policy == SdRuntimeMemoryPolicy::PsramOnly);
}
SdRuntimeDir::~SdRuntimeDir() { delete impl_; }
SdFileReadStatus SdRuntimeDir::open_read_status(const char* path, uint32_t expected)
{
    impl_->index = 0;
    impl_->prefix = std::string(path) + "/";
    if (expected != media) return SdFileReadStatus::Unavailable;
    const auto found = directory_status.find(path);
    return found == directory_status.end() ? SdFileReadStatus::Ready : found->second;
}
SdFileReadStatus SdRuntimeDir::seek_read_status(uint64_t position)
{
    if (!impl_ || position % 32) return SdFileReadStatus::Invalid;
    impl_->index = size_t(position / 32);
    return SdFileReadStatus::Ready;
}
SdDirReadStatus SdRuntimeDir::read_next_info(char* name, size_t capacity, bool* directory, uint64_t* bytes, uint64_t* next)
{
    std::map<std::string, std::pair<bool, uint64_t>> children;
    for (const auto& item : files)
    {
        if (item.first.rfind(impl_->prefix, 0) != 0) continue;
        const auto filename = item.first.substr(impl_->prefix.size());
        const auto slash = filename.find('/');
        children[filename.substr(0, slash)] = {slash != std::string::npos, slash != std::string::npos ? 0 : item.second.size()};
    }
    size_t index = 0;
    for (const auto& child : children)
    {
        if (index++ != impl_->index) continue;
        if (child.first.size() + 1 > capacity) return SdDirReadStatus::IoError;
        std::memcpy(name, child.first.c_str(), child.first.size() + 1);
        *directory = child.second.first;
        *bytes = child.second.second;
        *next = ++impl_->index * 32;
        return SdDirReadStatus::Entry;
    }
    return SdDirReadStatus::End;
}
} // namespace platform::esp::arduino_common::storage
static void testReadOwner()
{
    using namespace track_sharing;
    using Owner = hostlink::management::track_read_runtime::TrackReadOwner;
    using Change = chat::TrackBindingChange;
    PersistenceAuthor adapter;
    PublicationDirectory directory;
    std::memset(directory.delivery, 4, 16);
    std::memset(directory.public_key, 5, 64);
    std::memset(directory.epoch, 6, 16);
    directory.file_limit = 8388608;
    directory.chunk_limit = 1024;
    directory.query_limit = 32;
    uint8_t request[16], session[16], digest[32], raw[512], output[320];
    std::memset(request, 1, 16);
    std::memset(session, 2, 16);
    std::memset(digest, 3, 32);
    ReadValidationContext expected;
    expected.file_hash = {digest, 32};
    expected.file_bytes = 3;
    expected.length = 3;
    const unsigned before = allocations;
    auto* owner = Owner::create(adapter, 8, {request, 16}, {session, 16}, directory, {adapter.public_key, 64}, 1, expected);
    assert(owner && allocations > before && owner->matchesSession(session) && owner->matchesRequest({request, 16}));
    // Inputs are copied; later directory/config/request edits cannot retarget.
    digest[0] = 0;
    directory.delivery[0] = 0;
    namespace directories = hostlink::management::directory_runtime;
    directories::owner = new (heap_caps_malloc(sizeof(directories::Owner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) directories::Owner(adapter, session, 100);
    adapter.binding = &directories::owner->binding;
    assert(directories::handoverRead(adapter, *owner, 100) == Change::Applied && !directories::owner);
    ReadExchangeState state;
    assert(owner->advance(101, state) == Change::Applied && state == ReadExchangeState::Waiting && adapter.network_sends == 1);
    uint8_t source[16], local[16], message[32], original_hash[32];
    std::memset(source, 4, 16);
    std::memset(local, 0, 16);
    local[0] = 42;
    std::memset(message, 7, 32);
    std::memset(original_hash, 3, 32);
    geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
    assert(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(8) && writer.binary({request, 16}) &&
           writer.unsignedInteger(200) && writer.array(4) && writer.binary({original_hash, 32}) && writer.unsignedInteger(3) && writer.unsignedInteger(0) && writer.binary({source, 3}));
    chat::lxmf::CustomDeliveryView view{{source, 16}, {local, 16}, {message, 32}, {raw, writer.size()}};
    assert(chat::lxmf::deliverVerifiedTrack(adapter.binding, false, false, view));
    assert(owner->advance(102, state) == Change::Applied && state == ReadExchangeState::Ready);
    StoredReadResponse metadata;
    ReadStoreResult result;
    size_t written = 0;
    assert(owner->read(0, 0, output, sizeof(output), written, metadata, result) == Change::Applied && result == ReadStoreResult::Ready && metadata.ready && metadata.bytes == writer.size());
    uint8_t response_hash[32];
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    assert(hash.start() && hash.update(raw, writer.size()) && hash.finish(response_hash));
    assert(!std::memcmp(response_hash, metadata.digest, 32));
    assert(owner->read(0, metadata.bytes, output, sizeof(output), written, metadata, result) == Change::Applied && result == ReadStoreResult::Ready && written == writer.size() && !std::memcmp(raw, output, written));
    namespace publication = hostlink::management::publication_runtime;
    publication::controller = new (heap_caps_malloc(sizeof(publication::Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) publication::Controller;
    publication::controller->adapter = &adapter;
    publication::controller->read = owner;
    publication::controller->read_state = ReadExchangeState::Ready;
    directory.delivery[0] = 4;
    publication::controller->directory = directory;
    publication::controller->read_directory_cached = true;
    std::memcpy(publication::controller->local, session, 16);
    uint8_t command[471];
    auto resultCommand = [&](Operation operation, bool chunk)
    {
        geocaching::protocol::CmpWriter body(command, sizeof(command));
        assert(body.array(chunk ? 3 : 1) && body.binary({request, 16}));
        if (chunk) assert(body.unsignedInteger(0) && body.unsignedInteger(writer.size()));
        EnvelopeView envelope{session, request, uint16_t(operation), command, body.size()};
        return hostlink::management::track_read_runtime::processReadResult(adapter, envelope, 103, command, sizeof(command), written);
    };
    assert(resultCommand(Operation::TrackReadStatus, false) == Status::Ok);
    assert(resultCommand(Operation::TrackReadChunk, true) == Status::Ok);
    geocaching::protocol::CmpReader response({command, written});
    size_t fields = 0;
    uint64_t total = 0, offset = 0;
    geocaching::ByteView hash_view, content;
    assert(response.array(fields, 4) && fields == 4 && response.binary(hash_view, 32) && !std::memcmp(hash_view.data, response_hash, 32) &&
           response.unsignedInteger(total) && total == writer.size() && response.unsignedInteger(offset) && !offset &&
           response.binary(content, 320) && content.size == writer.size() && !std::memcmp(content.data, raw, content.size) && response.finished());
    adapter.binding_busy = true;
    assert(resultCommand(Operation::TrackReadRelease, false) == Status::Busy && owner && adapter.binding && publication::controller->read_retiring);
    adapter.binding_busy = false;
    assert(resultCommand(Operation::TrackReadRelease, false) == Status::Ok && publication::controller && !publication::controller->read && !publication::holdsStorage() && !adapter.binding && allocations == before + 1 && files.empty());
    owner = nullptr;
    digest[0] = 3;
    directory.delivery[0] = 4;
    // Successive file requests reuse the pinned directory for this USB session
    // after its discovery callback owner has already been retired.
    request[0] = 8;
    assert(publication::beginRead(adapter, 8, {request, 16}, {session, 16}, {directory.delivery, 16}, {directory.epoch, 16},
                                  {adapter.public_key, 64}, 1, expected, 104) == Status::Ok &&
           publication::controller->read && !directories::owner);
    publication::closeSession(session);
    assert(!publication::controller && !adapter.binding && allocations == before);
    request[0] = 1;
    owner = Owner::create(adapter, 8, {request, 16}, {session, 16}, directory, {adapter.public_key, 64}, 1, expected);
    assert(owner && owner->attach(200, nullptr) == Change::Applied);
    publication::controller = new (heap_caps_malloc(sizeof(publication::Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) publication::Controller;
    publication::controller->adapter = &adapter;
    publication::controller->read = owner;
    publication::controller->read_state = ReadExchangeState::Waiting;
    publication::controller->last = 200;
    assert(publication::holdsStorage());
    assert(publication::getStatus(adapter, {request, 16}, 1, raw, sizeof(raw), written) == Status::Busy);
    const EnvelopeView commit{session, request, uint16_t(Operation::TrackCommit), nullptr, 0};
    assert(publication::processCommit(adapter, commit, 200, raw, sizeof(raw), written) == Status::Busy);
    adapter.binding_busy = true;
    publication::closeSession(session);
    assert(publication::controller && publication::controller->read == owner && publication::controller->read_retiring && adapter.binding);
    assert(!chat::lxmf::deliverVerifiedTrack(adapter.binding, false, false, view));
    publication::tick(201);
    assert(publication::controller && publication::holdsStorage());
    adapter.binding_busy = false;
    publication::tick(202);
    owner = nullptr;
    assert(!publication::controller && !publication::holdsStorage() && !adapter.binding && allocations == before);
}
static void testReadRegionalOwner()
{
    using namespace track_sharing;
    using Owner = hostlink::management::track_read_runtime::TrackReadOwner;
    using Change = chat::TrackBindingChange;
    PersistenceAuthor adapter;
    PublicationDirectory directory;
    std::memset(directory.delivery, 4, 16);
    std::memset(directory.public_key, 5, 64);
    std::memset(directory.epoch, 6, 16);
    directory.file_limit = 8388608;
    directory.chunk_limit = 1024;
    directory.query_limit = 32;
    uint8_t request[16], session[16], version[32], digest[32], reply_version[32], reply_digest[32], query_author[64], raw[512], message[32], local[16], source[16], output[320];
    std::memset(request, 50, 16);
    std::memset(session, 51, 16);
    std::memset(message, 52, 32);
    std::memset(local, 0, 16);
    local[0] = 42;
    std::memset(source, 4, 16);
    std::memset(reply_version, 2, 32);
    std::memset(reply_digest, 3, 32);
    for (uint8_t op : {9, 10, 11, 12, 13})
    {
        std::memset(version, 2, 32);
        std::memset(digest, 3, 32);
        std::memset(query_author, 7, 64);
        request[0] = op;
        char name_space[] = "test", id[] = "test:r", parent[] = "test:r";
        RegionReadScope scope;
        scope.name_space = name_space;
        scope.id = id;
        scope.version = {version, 32};
        scope.depth = 3;
        if (op == 10)
        {
            scope.parent = parent;
            scope.parent_level = 0;
        }
        ReadQuery query;
        query.west = query.south = -100;
        query.east = query.north = 100;
        query.has_snapshot = true;
        query.snapshot = 9007199254740993ULL;
        query.author = {query_author, 64};
        ReadValidationContext expected;
        expected.region = &scope;
        expected.query = &query;
        expected.file_hash = {digest, 32};
        expected.file_bytes = 3;
        expected.length = 3;
        const unsigned before = allocations;
        if (op >= 10)
        {
            expected.region = nullptr;
            assert(!Owner::create(adapter, op, {request, 16}, {session, 16}, directory, {adapter.public_key, 64}, 1, expected) && allocations == before);
            expected.region = &scope;
        }
        auto* owner = Owner::create(adapter, op, {request, 16}, {session, 16}, directory, {adapter.public_key, 64}, 1, expected);
        assert(owner);
        name_space[0] = 'X';
        id[5] = 'x';
        parent[5] = 'x';
        version[0] = 9;
        digest[0] = 9;
        query_author[0] = 9;
        scope.depth = 0;
        query.snapshot = 0;
        expected.file_bytes = 99;
        assert(owner->attach(300, nullptr) == Change::Applied);
        ReadExchangeState state;
        assert(owner->advance(301, state) == Change::Applied && state == ReadExchangeState::Waiting);
        geocaching::protocol::CmpReader wire({adapter.network_wire.data(), adapter.network_wire.size()});
        size_t fields = 0;
        uint64_t value = 0;
        geocaching::ByteView bytes;
        std::string_view text;
        assert(wire.array(fields, 6) && fields == 6 && wire.unsignedInteger(value) && value == 1 && wire.unsignedInteger(value) && !value && wire.unsignedInteger(value) && value == op && wire.binary(bytes, 16) && wire.unsignedInteger(value) && value == 8192 && wire.array(fields, 9));
        if (op == 9) assert(!fields && wire.finished());
        if (op == 10) assert(fields == 4 && wire.binary(bytes, 32) && bytes.data[0] == 2 && wire.text(text, 89) && text == "test:r" && wire.nil() && wire.unsignedInteger(value) && value == 16 && wire.finished());
        if (op == 11 || op == 12 || op == 13) assert(wire.text(text, 89) && text == "test:r" && wire.binary(bytes, 32) && bytes.data[0] == 2);
        if (op == 11) assert(fields == 2 && wire.finished());
        if (op == 12) assert(fields == 4 && wire.unsignedInteger(value) && !value && wire.unsignedInteger(value) && value == 3 && wire.finished());
        if (op == 13) assert(fields == 9 && wire.text(text, 10) && text == "intersects" && wire.nil() && wire.binary(bytes, 64) && bytes.data[0] == 7 && wire.unsignedInteger(value) && !value && wire.unsignedInteger(value) && value == 16 && wire.unsignedInteger(value) && value == 9007199254740993ULL && wire.nil() && wire.finished());
        geocaching::protocol::CmpWriter reply(raw, sizeof(raw));
        assert(reply.array(6) && reply.unsignedInteger(1) && reply.unsignedInteger(1) && reply.unsignedInteger(op) && reply.binary({request, 16}) && reply.unsignedInteger(200) && reply.array(op == 9 ? 8 : op == 12 ? 4
                                                                                                                                                                                                                      : 3));
        if (op == 9)
        {
            assert(reply.unsignedInteger(1) && reply.text("trailmate.track.regions.v1") && reply.text("test") && reply.binary({reply_version, 32}) && reply.unsignedInteger(3) && reply.unsignedInteger(32) && reply.unsignedInteger(1024) && reply.array(5));
            for (unsigned i = 9; i <= 13; i++) assert(reply.unsignedInteger(i));
        }
        if (op == 10 || op == 11)
        {
            assert(reply.binary({reply_version, 32}));
            if (op == 10) assert(reply.nil() && reply.array(1));
            assert(reply.array(7) && reply.text(op == 10 ? "test:a" : "test:r") && (op == 10 ? reply.text("test:r") : reply.nil()) && reply.unsignedInteger(op == 10 ? 1 : 0) && reply.text("Region") && reply.array(0) && reply.array(4) && reply.signedInteger(-1) && reply.signedInteger(-1) && reply.signedInteger(1) && reply.signedInteger(1) && reply.unsignedInteger(0));
            if (op == 11) assert(reply.array(3) && reply.text("region-e7-json-v1") && reply.binary({reply_digest, 32}) && reply.unsignedInteger(3));
        }
        if (op == 12)
        {
            const uint8_t data[] = {7, 8, 9};
            assert(reply.binary({reply_digest, 32}) && reply.unsignedInteger(3) && reply.unsignedInteger(0) && reply.binary({data, 3}));
        }
        if (op == 13) assert(reply.unsignedInteger(9007199254740993ULL) && reply.nil() && reply.array(0));
        chat::lxmf::CustomDeliveryView view{{source, 16}, {local, 16}, {message, 32}, {raw, reply.size()}};
        assert(chat::lxmf::deliverVerifiedTrack(adapter.binding, false, false, view));
        assert(owner->advance(302, state) == Change::Applied && state == ReadExchangeState::Ready);
        StoredReadResponse metadata;
        ReadStoreResult result;
        size_t written = 0;
        assert(owner->read(0, 0, output, sizeof(output), written, metadata, result) == Change::Applied && result == ReadStoreResult::Ready && metadata.operation == op);
        assert(Owner::release(owner) && !owner && !adapter.binding && allocations == before && files.empty());
    }
}
static void testRouteInstallOwner()
{
    namespace route = hostlink::management::route_install_runtime;
    using hostlink::management::Status;
    const unsigned before = allocations;
    uint8_t id[16]{77}, author[32], public_key[64], session[16]{9}, digest[32], encoded[108], scratch[448];
    UploadAuthorAdapter adapter;
    uint32_t generation = 0;
    assert(hostlink::management::upload_runtime::readUploadAuthor(adapter, public_key, sizeof(public_key), author, generation) == Status::Ok && generation == 7);
    const std::string xml = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><LineString><coordinates>120,30 120.01,30.01</coordinates></LineString></Placemark></kml>";
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    assert(hash.start() && hash.update(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()) && hash.finish(digest));
    hostlink::management::RouteInstallView request{{id, 16}, {digest, 32}, uint32_t(xml.size()), 7};
    char source[64], journal_path[64];
    assert(track_sharing::resolveUploadPath(id, 16, track_sharing::UploadFile::Candidate, source, sizeof(source)));
    assert(track_sharing::resolveUploadPath(id, 16, track_sharing::UploadFile::JournalA, journal_path, sizeof(journal_path)));
    track_sharing::UploadJournal journal;
    std::memcpy(journal.id, id, 16);
    std::memcpy(journal.author_hash, author, 32);
    std::memcpy(journal.file_hash, digest, 32);
    journal.sequence = 1;
    journal.generation = 7;
    journal.total = journal.durable_offset = uint32_t(xml.size());
    journal.phase = track_sharing::UploadPhase::Sealed;
    assert(track_sharing::encodeUploadJournal(journal, encoded, sizeof(encoded)));
    files[source] = std::vector<uint8_t>(xml.begin(), xml.end());
    files[journal_path] = std::vector<uint8_t>(encoded, encoded + 108);
    const auto allow = []
    { return Status::Ok; };
    auto* owner = route::createRouteInstallOwner(request, session, author, media, 10);
    assert(owner && owner->matches(session, request) && owner->identityMatches(7, {author, 32}) && owner->alive(11, true));
    auto status = route::startAuthenticatedRouteInstall(*owner, adapter, true, scratch, sizeof(scratch), allow);
    for (unsigned steps = 0; status == Status::Busy && steps < 100; ++steps) status = route::advanceAuthenticatedRouteInstall(*owner, adapter, scratch, sizeof(scratch), allow);
    assert(status == Status::Ok && owner->wireState() == 1 && files.count(owner->target()) && !files.count(source));
    route::destroyRouteInstallOwner(owner);
    assert(allocations == before);
    files.erase(journal_path);
    owner = route::createRouteInstallOwner(request, session, author, media, 20);
    assert(owner);
    status = owner->start(false, scratch, sizeof(scratch), allow);
    for (unsigned steps = 0; status == Status::Busy && steps < 100; ++steps) status = owner->advance(scratch, sizeof(scratch), allow);
    assert(status == Status::Ok && owner->wireState() == 1);
    const auto target = std::string(owner->target());
    route::destroyRouteInstallOwner(owner);
    assert(allocations == before);
    files.erase(target);
    files.erase(journal_path);
    files[source] = std::vector<uint8_t>(xml.begin(), xml.end());
    files[journal_path] = std::vector<uint8_t>(encoded, encoded + 108);
    owner = route::createRouteInstallOwner(request, session, author, media, 30);
    assert(owner);
    assert(route::startAuthenticatedRouteInstall(*owner, adapter, true, scratch, sizeof(scratch), allow) == Status::Busy);
    adapter.generation = 8;
    assert(route::advanceAuthenticatedRouteInstall(*owner, adapter, scratch, sizeof(scratch), allow) == Status::StaleIdentity && files.count(source) && !files.count(target));
    route::destroyRouteInstallOwner(owner);
    adapter.generation = 7;
    owner = route::createRouteInstallOwner(request, session, author, media, 30);
    assert(owner);
    adapter.result = chat::DeviceAuthorResult::StorageError;
    assert(route::startAuthenticatedRouteInstall(*owner, adapter, true, scratch, sizeof(scratch), allow) == Status::StorageUnavailable);
    adapter.result = chat::DeviceAuthorResult::Ok;
    assert(route::startAuthenticatedRouteInstall(*owner, adapter, true, reinterpret_cast<uint8_t*>(owner), 448, allow) == Status::InvalidField);
    route::destroyRouteInstallOwner(owner);
    route::RouteInstallOwner* slot = nullptr;
    uint8_t body[128];
    geocaching::protocol::CmpWriter writer(body, sizeof(body));
    assert(writer.array(4) && writer.binary({id, 16}) && writer.binary({digest, 32}) && writer.unsignedInteger(xml.size()) && writer.unsignedInteger(7));
    hostlink::management::EnvelopeView envelope;
    envelope.code = uint16_t(hostlink::management::Operation::RouteInstall);
    envelope.session = session;
    envelope.body = body;
    envelope.body_size = writer.size();
    size_t written = 0;
    const auto policy = [](const auto&, bool)
    { return Status::Ok; };
    const auto replyState = [&]
    {
        geocaching::protocol::CmpReader reply({scratch, written});
        size_t fields = 0;
        geocaching::ByteView returned_id, returned_hash;
        uint64_t total = 0, state = 255;
        std::string_view path;
        assert(reply.array(fields, 5) && fields == 5 && reply.binary(returned_id, 16) && returned_id.size == 16 &&
               reply.binary(returned_hash, 32) && returned_hash.size == 32 && reply.unsignedInteger(total) && reply.text(path, 76) &&
               reply.unsignedInteger(state) && reply.finished());
        assert(!std::memcmp(returned_id.data, id, 16) && !std::memcmp(returned_hash.data, digest, 32) && total == xml.size() && path == slot->target());
        return state;
    };
    assert(route::processRouteInstallRequest(slot, adapter, envelope, 40, scratch, sizeof(scratch), written, policy) == Status::Ok && slot && slot->wireState() == 0 && written);
    assert(replyState() == 0);
    assert(!route::retireDeliveredRouteInstall(slot) && slot);
    uint8_t other_session[16]{8};
    envelope.session = other_session;
    assert(route::processRouteInstallRequest(slot, adapter, envelope, 41, scratch, sizeof(scratch), written, policy) == Status::Unauthorized);
    envelope.session = session;
    for (unsigned step = 0; slot->status() == Status::Busy && step < 100; ++step) route::advanceRouteInstallSlot(slot, &adapter, scratch, sizeof(scratch), 42, true, policy);
    assert(slot->wireState() == 1 && !slot->resultDelivered());
    assert(!route::retireDeliveredRouteInstall(slot) && slot);
    envelope.code = uint16_t(hostlink::management::Operation::RouteInstallStatus);
    assert(route::processRouteInstallRequest(slot, adapter, envelope, 43, scratch, sizeof(scratch), written, policy) == Status::Ok && slot->wireState() == 1 && slot->resultDelivered());
    assert(replyState() == 1);
    const std::vector<uint8_t> confirmed_reply(scratch, scratch + written);
    assert(route::retireDeliveredRouteInstall(slot) && !slot && allocations == before);
    assert(std::equal(confirmed_reply.begin(), confirmed_reply.end(), scratch));
    const auto installed_files = files;
    assert(route::processRouteInstallRequest(slot, adapter, envelope, 44, scratch, sizeof(scratch), written, policy) == Status::Ok && slot->queryOnly() && slot->wireState() == 0);
    for (unsigned step = 0; slot->status() == Status::Busy && step < 100; ++step) route::advanceRouteInstallSlot(slot, &adapter, scratch, sizeof(scratch), 45, true, policy);
    assert(route::processRouteInstallRequest(slot, adapter, envelope, 46, scratch, sizeof(scratch), written, policy) == Status::Ok && slot->wireState() == 1 && files == installed_files);
    const std::string installed_target = slot->target();
    route::closeRouteInstallSession(slot, session);
    assert(!slot && allocations == before);
    files.erase(installed_target);
    assert(route::processRouteInstallRequest(slot, adapter, envelope, 47, scratch, sizeof(scratch), written, policy) == Status::Ok && slot->queryOnly() && slot->wireState() == 2 && !files.count(installed_target));
    assert(replyState() == 2);
    route::closeRouteInstallSession(slot, session);
    assert(!slot && allocations == before);
    files.erase(source);
    files.erase(journal_path);
    files[journal_path] = std::vector<uint8_t>(encoded, encoded + 108);
    unsigned policy_calls = 0;
    envelope.code = uint16_t(hostlink::management::Operation::RouteInstall);
    const auto files_before_busy = files;
    assert(route::processRouteInstallRequest(slot, adapter, envelope, 48, scratch, sizeof(scratch), written, [&](const auto&, bool)
                                             { return ++policy_calls == 1 ? Status::Ok : Status::Busy; }) == Status::Busy);
    assert(!slot && allocations == before && files == files_before_busy && written == 0);
    files.erase(journal_path);
    allocation_fail = true;
    assert(!route::createRouteInstallOwner(request, session, author, media, 20));
    allocation_fail = false;
    backing_allocation_fail = true;
    assert(!route::createRouteInstallOwner(request, session, author, media, 20));
    backing_allocation_fail = false;
    assert(allocations == before);
}
static void testAttachmentPreparation()
{
    using namespace track_sharing;
    using R = AttachmentPackageResult;
    namespace prepare = hostlink::management::prepare_runtime;
    namespace upload = hostlink::management::upload_runtime;
    const auto saved = files;
    assert(!allocations && !upload::owner);
    files.clear();
    uint8_t author[32] = {2}, kml_hash[32] = {3}, index_id[16] = {4}, ids[1024]{}, session[16] = {9};
    uint8_t digest[32], index_digest[32], scratch[448];
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    const std::vector<uint8_t> resource(641, 6);
    const auto calculate = [&](const std::vector<uint8_t>& bytes, uint8_t* output)
    { assert(hash.start() && hash.update(bytes.data(), bytes.size()) && hash.finish(output)); hash.reset(); };
    calculate(resource, digest);
    const auto sealed = [&](const uint8_t* id, UploadKind kind, const std::vector<uint8_t>& bytes, const uint8_t* file_hash)
    {
        char path[64];
        assert(resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path), kind));
        files[path] = bytes;
        UploadJournal journal;
        std::memcpy(journal.id, id, 16);
        std::memcpy(journal.author_hash, author, 32);
        std::memcpy(journal.file_hash, file_hash, 32);
        journal.generation = 1;
        journal.sequence = 3;
        journal.phase = UploadPhase::Sealed;
        journal.total = journal.durable_offset = bytes.size();
        std::vector<uint8_t> encoded(kUploadJournalBytes);
        assert(encodeUploadJournal(journal, encoded.data(), encoded.size()));
        assert(resolveUploadPath(id, 16, UploadFile::JournalA, path, sizeof(path), kind));
        files[path] = encoded;
    };
    // Canonical 64-entry index, separate upload IDs can share identical bytes.
    std::vector<uint8_t> raw(kMaxAttachmentIndexBytes);
    geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
    assert(writer.array(4) && writer.unsignedInteger(1) && writer.binary({kml_hash, 32}) && writer.unsignedInteger(300) && writer.array(64));
    for (unsigned i = 0; i < 64; ++i)
    {
        ids[i * 16] = uint8_t(i + 1);
        const auto id = "img" + std::to_string(i);
        const auto reference = "photos/" + std::to_string(i) + ".png";
        assert(writer.array(7) && writer.text(id) && writer.binary({digest, 32}) && writer.unsignedInteger(resource.size()) &&
               writer.text("image/png") && writer.text(reference) && writer.unsignedInteger(0) && writer.unsignedInteger(i == 0 ? 1 : 0));
        sealed(ids + i * 16, UploadKind::AttachmentResource, resource, digest);
    }
    raw.resize(writer.size());
    calculate(raw, index_digest);
    const AttachmentIndexBinding binding{{index_digest, 32}, uint32_t(raw.size()), 64};
    sealed(index_id, UploadKind::AttachmentIndex, raw, index_digest);
    const auto intact = files;
    for (unsigned fault = 0; fault < 10; ++fault)
    {
        files = intact;
        uint8_t frozen_ids[1024];
        std::memcpy(frozen_ids, ids, sizeof(ids));
        char path[64];
        if (fault == 1) std::memset(frozen_ids + 16, 0, 16); // optional absent
        if (fault == 2) std::memset(frozen_ids, 0, 16);      // required absent
        if (fault == 3 || fault == 4)
        {
            assert(resolveUploadPath(ids + (fault == 3 ? 0 : 16), 16, UploadFile::Candidate, path, sizeof(path), UploadKind::AttachmentResource));
            files[path][10] ^= 1; // required or supplied optional corruption
        }
        if (fault == 5)
        {
            assert(resolveUploadPath(ids, 16, UploadFile::JournalA, path, sizeof(path), UploadKind::AttachmentResource));
            files.erase(path); // missing supplied upload metadata is not optional absence
        }
        if (fault == 6)
        {
            assert(resolveUploadPath(index_id, 16, UploadFile::Candidate, path, sizeof(path), UploadKind::AttachmentIndex));
            files[path].back() = 1; // valid required flag, but digest no longer matches
        }
        const auto before = files;
        auto* owner = prepare::createAttachmentPrepareOwner(media, 1, author, index_id, {frozen_ids, sizeof(frozen_ids)}, binding, {kml_hash, 32}, 300);
        assert(owner && allocations == 1 && owner->begin() == R::Pending && !owner->ready());
        // Mutation of borrowed constructor input cannot change the admitted job.
        std::memset(frozen_ids, 0, sizeof(frozen_ids));
        R result = R::Pending;
        unsigned steps = 0;
        while (result == R::Pending && steps++ < 3000)
        {
            result = owner->step(fault == 7 ? 2 : 1, author);
            if (fault == 8 && steps == 1)
            {
                ++media;
                result = owner->step(1, author);
                --media;
            }
            if (fault == 9 && steps == 1)
            {
                owner->cancel();
                result = owner->step(1, author);
            }
        }
        const auto expected = fault <= 1 ? R::Ready : fault == 2                           ? R::MissingRequired
                                                  : fault == 3 || fault == 4 || fault == 6 ? R::HashMismatch
                                                  : fault == 8                             ? R::StorageFailure
                                                  : fault == 9                             ? R::Cancelled
                                                                                           : R::Invalid;
        assert(result == expected && files == before);
        if (fault <= 1)
        {
            assert(owner->ready() && owner->matches(binding, {kml_hash, 32}, 300));
            assert(owner->allAvailable() == (fault == 0));
            assert(owner->availability() == (fault == 0 ? UINT64_MAX : UINT64_MAX ^ uint64_t(2)));
            auto wrong = binding;
            --wrong.count;
            assert(!owner->matches(wrong, {kml_hash, 32}, 300));
            assert(!owner->matches(binding, {kml_hash, 32}, 301));
        }
        else assert(!owner->ready() && !owner->availability());
        prepare::destroyAttachmentPrepareOwner(owner);
        assert(allocations == 0);
    }
    allocation_fail = true;
    assert(!prepare::createAttachmentPrepareOwner(media, 1, author, index_id, {ids, sizeof(ids)}, binding, {kml_hash, 32}, 300));
    allocation_fail = false;
    backing_allocation_fail = true;
    assert(!prepare::createAttachmentPrepareOwner(media, 1, author, index_id, {ids, sizeof(ids)}, binding, {kml_hash, 32}, 300) && !allocations);
    backing_allocation_fail = false;
    files = intact;
    // All 64 resources (192 candidate/journal slots) are admitted, while the
    // historical KML directory still permits only two transactions.
    assert(upload::admitUploadSpace(media, resource.size(), scratch, sizeof(scratch), UploadKind::AttachmentResource) == Status::Ok);
    for (unsigned i = 0; i < 64; ++i)
    {
        char path[64];
        assert(resolveUploadPath(ids + i * 16, 16, UploadFile::JournalB, path, sizeof(path), UploadKind::AttachmentResource));
        files[path] = std::vector<uint8_t>(108, 0);
    }
    assert(upload::admitUploadSpace(media, resource.size(), scratch, sizeof(scratch), UploadKind::AttachmentResource) == Status::LimitExceeded);
    files.clear();
    // Real upload owner/journals/seal/recovery for each new transfer kind.
    for (const auto kind : {UploadKind::AttachmentIndex, UploadKind::AttachmentResource})
    {
        upload::TrackUploadOwner* owner = nullptr;
        assert(upload::beginNewUpload(owner, index_id, session, media, 1, author, resource.size(), digest, 100, scratch, sizeof(scratch), kind) == Status::Ok);
        assert(owner && owner->kind == kind);
        for (size_t offset = 0; offset < resource.size(); offset += 320)
            assert(owner->resources.append(index_id, offset, resource.data() + offset, std::min<size_t>(320, resource.size() - offset), scratch, sizeof(scratch)) == JournalStoreResult::Ready);
        assert(owner->resources.beginSeal(scratch, sizeof(scratch)) == UploadSealResult::Pending);
        UploadSealResult sealed_result;
        do
        {
            sealed_result = owner->resources.step(scratch, sizeof(scratch));
        } while (sealed_result == UploadSealResult::Pending);
        assert(sealed_result == UploadSealResult::Ready);
        upload::destroyOwner(owner);
        owner = nullptr;
        assert(upload::recoverUpload(owner, index_id, session, media, 1, author, resource.size(), digest, 200, scratch, sizeof(scratch), kind) == Status::Ok);
        assert(owner && owner->resources.metadata().phase == UploadPhase::Sealed);
        upload::destroyOwner(owner);
        assert(!allocations);
    }
    files = saved;
}
static void testPackagePrepareWorkflow()
{
    using namespace track_sharing;
    namespace prepare = hostlink::management::prepare_runtime;
    const auto saved = files;
    files.clear();
    assert(!prepare::work && !allocations);
    PersistenceAuthor adapter;
    uint8_t author_hash[32], kml_hash[32], resource_hash[32], index_hash[32], nonce[16] = {10}, session[16] = {11};
    uint8_t kml_id[16] = {12}, manifest_id[16] = {13}, index_id[16] = {14}, mapping_id[16] = {15}, operation[16] = {16}, ids[48]{};
    uint8_t payload[151], scratch[471], raw_manifest[4096], signed_raw[4166], verify_workspace[4200];
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    const auto digest = [&](const uint8_t* data, size_t size, uint8_t* output)
    { assert(hash.start() && hash.update(data, size) && hash.finish(output)); hash.reset(); };
    digest(adapter.public_key, 64, author_hash);
    const std::string xml = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>";
    const std::vector<uint8_t> resource(641, 17);
    digest(reinterpret_cast<const uint8_t*>(xml.data()), xml.size(), kml_hash);
    digest(resource.data(), resource.size(), resource_hash);
    const auto sealed = [&](const uint8_t* id, UploadKind kind, const uint8_t* data, size_t size)
    {
        char path[64];
        UploadJournal journal;
        std::memcpy(journal.id, id, 16);
        std::memcpy(journal.author_hash, author_hash, 32);
        digest(data, size, journal.file_hash);
        journal.generation = 1;
        journal.sequence = 3;
        journal.phase = UploadPhase::Sealed;
        journal.total = journal.durable_offset = size;
        assert(resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path), kind));
        files[path] = {data, data + size};
        assert(resolveUploadPath(id, 16, UploadFile::JournalA, path, sizeof(path), kind));
        files[path].resize(108);
        assert(encodeUploadJournal(journal, files[path].data(), 108));
    };
    std::vector<uint8_t> index(1000);
    geocaching::protocol::CmpWriter iw(index.data(), index.size());
    assert(iw.array(4) && iw.unsignedInteger(1) && iw.binary({kml_hash, 32}) && iw.unsignedInteger(xml.size()) && iw.array(3));
    for (unsigned i = 0; i < 3; ++i)
    {
        if (i != 1)
        {
            ids[i * 16] = uint8_t(20 + i);
            sealed(ids + i * 16, UploadKind::AttachmentResource, resource.data(), resource.size());
        }
        const auto name = "img" + std::to_string(i), reference = "photos/" + std::to_string(i) + ".png";
        assert(iw.array(7) && iw.text(name) && iw.binary({resource_hash, 32}) && iw.unsignedInteger(resource.size()) && iw.text("image/png") &&
               iw.text(reference) && iw.unsignedInteger(0) && iw.unsignedInteger(i == 0 ? 1 : 0));
    }
    index.resize(iw.size());
    digest(index.data(), index.size(), index_hash);
    sealed(index_id, UploadKind::AttachmentIndex, index.data(), index.size());
    uint8_t mapping[1100];
    geocaching::protocol::CmpWriter mw(mapping, sizeof(mapping));
    assert(mw.array(3) && mw.unsignedInteger(1) && mw.binary({index_id, 16}) && mw.binary({ids, sizeof(ids)}));
    sealed(mapping_id, UploadKind::AttachmentBindings, mapping, mw.size());
    geocaching::protocol::CmpWriter manifest(raw_manifest, sizeof(raw_manifest));
    assert(manifest.array(16) && manifest.unsignedInteger(2) && manifest.binary({adapter.public_key, 64}) && manifest.binary({nonce, 16}) &&
           manifest.unsignedInteger(0) && manifest.unsignedInteger(1) && manifest.nil() && manifest.unsignedInteger(0) && manifest.unsignedInteger(100) &&
           manifest.unsignedInteger(100) && manifest.text("Package") && manifest.text(""));
    assert(manifest.array(4) && manifest.unsignedInteger(10000000) && manifest.unsignedInteger(20000000) && manifest.unsignedInteger(10000000) && manifest.unsignedInteger(20000000));
    assert(manifest.array(4) && manifest.unsignedInteger(0) && manifest.nil() && manifest.nil() && manifest.unsignedInteger(1));
    assert(manifest.array(4) && manifest.binary({kml_hash, 32}) && manifest.unsignedInteger(xml.size()) && manifest.text("application/vnd.google-earth.kml+xml") && manifest.unsignedInteger(1));
    assert(manifest.array(8) && manifest.unsignedInteger(0) && manifest.array(0) && manifest.unsignedInteger(0) && manifest.unsignedInteger(0) &&
           manifest.unsignedInteger(0) && manifest.unsignedInteger(0) && manifest.unsignedInteger(0) && manifest.nil());
    assert(manifest.array(4) && manifest.binary({index_hash, 32}) && manifest.unsignedInteger(index.size()) && manifest.unsignedInteger(3) && manifest.unsignedInteger(1));
    const size_t manifest_size = manifest.size();
    sealed(kml_id, UploadKind::Kml, reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
    sealed(manifest_id, UploadKind::Manifest, raw_manifest, manifest_size);
    const auto uploads = files;
    const auto request = [&](bool package, Operation command)
    {
        geocaching::protocol::CmpWriter pw(payload, sizeof(payload));
        assert(pw.array(package ? 6 : 5) && pw.binary({operation, 16}) && pw.binary({kml_id, 16}) && pw.binary({manifest_id, 16}) && pw.unsignedInteger(1) && pw.nil());
        if (package) assert(pw.binary({mapping_id, 16}));
        EnvelopeView value;
        value.code = uint16_t(command);
        value.session = session;
        value.body = payload;
        value.body_size = pw.size();
        return value;
    };
    size_t written = 0;
    assert(prepare::processPrepareRequest(adapter, request(true, Operation::TrackSignPrepare), 100, scratch, sizeof(scratch), written) == Status::InvalidField && !prepare::work);
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        files = uploads;
        adapter.signing_calls = 0;
        char path[64];
        if (fault == 0)
        {
            assert(resolveUploadPath(ids, 16, UploadFile::Candidate, path, sizeof(path), UploadKind::AttachmentResource));
            files[path][100] ^= 1;
        }
        const auto before = files;
        auto envelope = request(fault != 1, fault == 1 ? Operation::TrackSignPrepare : Operation::TrackPackageSignPrepare);
        Status result = Status::Ok;
        bool ready = false, restarted = false;
        for (unsigned step = 0; step < 3000 && (result == Status::Ok || result == Status::Busy) && !ready; ++step)
        {
            envelope = request(fault != 1, fault == 1 ? Operation::TrackSignPrepare : Operation::TrackPackageSignPrepare);
            result = prepare::processPrepareRequest(adapter, envelope, 100 + step, scratch, sizeof(scratch), written);
            if (result != Status::Ok) continue;
            geocaching::protocol::CmpReader response({scratch, written});
            size_t fields = 0;
            geocaching::ByteView returned_operation, object, revision_hash, signature;
            uint64_t state = 0, revision = 0;
            assert(response.array(fields, 6) && fields == 6 && response.binary(returned_operation, 16) && response.unsignedInteger(state));
            if (!state)
            {
                assert(response.nil() && response.nil() && response.unsignedInteger(revision) && !revision && response.nil() && response.finished());
                // Drop the complete job during a partial resource copy, then
                // restart from persisted signature and verified file prefix.
                if (fault == 2 && !restarted)
                {
                    for (const auto& file : files)
                        if (file.first.size() > 4 && file.first.substr(file.first.size() - 4) == ".atm" && !file.second.empty() && file.second.size() < resource.size())
                        {
                            prepare::closePrepareSession(session);
                            restarted = true;
                            assert(!allocations && adapter.signing_calls == 1);
                            break;
                        }
                }
                continue;
            }
            assert(response.binary(object, 32) && response.binary(revision_hash, 32) && response.unsignedInteger(revision) && revision == 1 && response.binary(signature, 64) && response.finished());
            assert(fault == 2 && restarted && adapter.signing_calls == 1);
            geocaching::protocol::CmpWriter sw(signed_raw, sizeof(signed_raw));
            assert(sw.array(2) && sw.binary({raw_manifest, manifest_size}) && sw.binary(signature));
            platform::esp::storage::TrackManifestCrypto crypto;
            VerifiedManifestView verified;
            assert(verifyManifest({signed_raw, sw.size()}, crypto, hash, verify_workspace, sizeof(verify_workspace), verified, {adapter.public_key, 64}) == ManifestVerification::Valid);
            assert(!std::memcmp(object.data, verified.object_id, 32) && !std::memcmp(revision_hash.data, verified.revision_hash, 32));
            char retained[160];
            assert(resolveObjectPath(object, ObjectFile::AttachmentIndex, {index_hash, 32}, retained, sizeof(retained)) && files.at(retained) == index);
            assert(resolveObjectPath(object, ObjectFile::AttachmentResource, {resource_hash, 32}, retained, sizeof(retained)) && files.at(retained) == resource);
            assert(resolveObjectPath(object, ObjectFile::KmlFile, {kml_hash, 32}, retained, sizeof(retained)) && files.at(retained) == std::vector<uint8_t>(xml.begin(), xml.end()));
            const auto completed = files;
            assert(prepare::processPrepareRequest(adapter, request(true, Operation::TrackPackageSignPrepare), 4000, scratch, sizeof(scratch), written) == Status::Ok && files == completed && adapter.signing_calls == 1);
            ready = true;
        }
        if (fault < 2) assert(result == (fault == 0 ? Status::HashMismatch : Status::InvalidField) && files == before && !adapter.signing_calls && !ready);
        else
        {
            if (!ready)
            {
                std::fprintf(stderr, "package result=%u signs=%u restart=%u allocations=%u work=%u holds=%u\n", unsigned(result), adapter.signing_calls, unsigned(restarted), allocations, unsigned(prepare::work->status()), unsigned(prepare::work->holdsStages()));
                for (const auto& file : files) std::fprintf(stderr, "%s %u\n", file.first.c_str(), unsigned(file.second.size()));
            }
            assert(ready);
        }
        prepare::closePrepareSession(session);
        assert(!allocations && !prepare::work);
    }
    files = saved;
}
// Test transport only: actual production upload/prepare code and cryptography,
// same simulated SD/heap as the native regression. No USB/RF/private-key output.
static void testSharedPackageInput()
{
    using namespace track_sharing;
    namespace package = hostlink::management::package_install_runtime;
    const auto saved = files;
    files.clear();
    assert(!allocations);
    PersistenceAuthor local, remote;
    uint8_t seed[32] = {9};
    remote.public_key[0] = 2;
    ed25519_create_keypair(remote.public_key + 32, remote.secret, seed);
    uint8_t actor[32], xml_hash[32], resource_hash[32], index_hash[32], raw[4096], signed_raw[4166], workspace[4200], signature[64], scratch[471];
    uint8_t nonce[16] = {1}, kml_id[16] = {2}, signed_id[16] = {3}, index_id[16] = {4}, map_id[16] = {5}, resource_id[16] = {6};
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    const auto digest = [&](const uint8_t* bytes, size_t size, uint8_t* out)
    {assert(hash.start()&&hash.update(bytes,size)&&hash.finish(out));hash.reset(); };
    digest(local.public_key, 64, actor);
    const std::string xml = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>";
    const std::vector<uint8_t> resource(641, 7);
    digest(reinterpret_cast<const uint8_t*>(xml.data()), xml.size(), xml_hash);
    digest(resource.data(), resource.size(), resource_hash);
    std::vector<uint8_t> index(1000);
    geocaching::protocol::CmpWriter iw(index.data(), index.size());
    assert(iw.array(4) && iw.unsignedInteger(1) && iw.binary({xml_hash, 32}) && iw.unsignedInteger(xml.size()) && iw.array(1) && iw.array(7) && iw.text("photo") && iw.binary({resource_hash, 32}) && iw.unsignedInteger(resource.size()) && iw.text("image/png") && iw.text("photos/p.png") && iw.unsignedInteger(0) && iw.unsignedInteger(1));
    index.resize(iw.size());
    digest(index.data(), index.size(), index_hash);
    const auto sealed = [&](const uint8_t* id, UploadKind kind, const uint8_t* bytes, size_t size, const uint8_t* owner = static_cast<const uint8_t*>(nullptr))
    {
        UploadJournal journal;
        std::memcpy(journal.id, id, 16);
        std::memcpy(journal.author_hash, owner ? owner : actor, 32);
        digest(bytes, size, journal.file_hash);
        journal.generation = 1;
        journal.sequence = 3;
        journal.phase = UploadPhase::Sealed;
        journal.total = journal.durable_offset = size;
        char path[64];
        assert(resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path), kind));
        files[path] = {bytes, bytes + size};
        assert(resolveUploadPath(id, 16, UploadFile::JournalA, path, sizeof(path), kind));
        files[path].resize(108);
        assert(encodeUploadJournal(journal, files[path].data(), 108));
    };
    for (unsigned version : {1U, 2U})
    {
        files.clear();
        geocaching::protocol::CmpWriter mw(raw, sizeof(raw));
        assert(mw.array(version == 1 ? 14 : 16) && mw.unsignedInteger(version) && mw.binary({remote.public_key, 64}) && mw.binary({nonce, 16}) && mw.unsignedInteger(0) && mw.unsignedInteger(1) && mw.nil() && mw.unsignedInteger(0) && mw.unsignedInteger(100) && mw.unsignedInteger(100) && mw.text("Shared") && mw.text(""));
        assert(mw.array(4) && mw.unsignedInteger(10000000) && mw.unsignedInteger(20000000) && mw.unsignedInteger(10000000) && mw.unsignedInteger(20000000) && mw.array(4) && mw.unsignedInteger(0) && mw.nil() && mw.nil() && mw.unsignedInteger(1));
        assert(mw.array(4) && mw.binary({xml_hash, 32}) && mw.unsignedInteger(xml.size()) && mw.text("application/vnd.google-earth.kml+xml") && mw.unsignedInteger(1));
        if (version == 2)
        {
            assert(mw.array(8) && mw.unsignedInteger(0) && mw.array(0) && mw.unsignedInteger(0) && mw.unsignedInteger(0) && mw.unsignedInteger(0) && mw.unsignedInteger(0) && mw.unsignedInteger(0) && mw.nil());
            assert(mw.array(4) && mw.binary({index_hash, 32}) && mw.unsignedInteger(index.size()) && mw.unsignedInteger(1) && mw.unsignedInteger(1));
        }
        ManifestView view;
        assert(remote.signTrackManifest(raw, mw.size(), 1, view, workspace, sizeof(workspace), signature, sizeof(signature)) == chat::DeviceAuthorResult::Ok);
        geocaching::protocol::CmpWriter sw(signed_raw, sizeof(signed_raw));
        assert(sw.array(2) && sw.binary({raw, mw.size()}) && sw.binary({signature, 64}));
        platform::esp::storage::TrackManifestCrypto crypto;
        VerifiedManifestView verified;
        assert(verifyManifest({signed_raw, sw.size()}, crypto, hash, workspace, sizeof(workspace), verified) == ManifestVerification::Valid);
        sealed(kml_id, UploadKind::Kml, reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
        sealed(signed_id, UploadKind::SignedManifest, signed_raw, sw.size());
        geocaching::protocol::CmpWriter mapping(scratch, sizeof(scratch));
        assert(mapping.array(3) && mapping.unsignedInteger(1) && mapping.binary({index_id, 16}) && mapping.binary({resource_id, 16}));
        std::vector<uint8_t> mapping_bytes(scratch, scratch + mapping.size());
        if (version == 2)
        {
            sealed(index_id, UploadKind::AttachmentIndex, index.data(), index.size());
            sealed(resource_id, UploadKind::AttachmentResource, resource.data(), resource.size());
            sealed(map_id, UploadKind::AttachmentBindings, mapping_bytes.data(), mapping_bytes.size());
        }
        const auto intact = files;
        for (unsigned fault = 0; fault < 8; ++fault)
        {
            files = intact;
            uint8_t expected_object[32], expected_revision[32];
            std::memcpy(expected_object, verified.object_id, 32);
            std::memcpy(expected_revision, verified.revision_hash, 32);
            if (fault == 1) expected_revision[0] ^= 1;
            if (fault == 2)
            {
                auto altered = std::vector<uint8_t>(signed_raw, signed_raw + sw.size());
                altered.back() ^= 1;
                sealed(signed_id, UploadKind::SignedManifest, altered.data(), altered.size());
            }
            if (fault == 3)
            {
                uint8_t foreign_actor[32];
                digest(remote.public_key, 64, foreign_actor);
                sealed(kml_id, UploadKind::Kml, reinterpret_cast<const uint8_t*>(xml.data()), xml.size(), foreign_actor);
            }
            const auto before = files;
            auto* input = package::createPackageInputOwner(media, 1, actor, {kml_id, 16}, {signed_id, 16}, version == 2 ? geocaching::ByteView{map_id, 16} : geocaching::ByteView{}, {expected_object, 32}, {expected_revision, 32});
            assert(input && allocations == 1);
            auto result = input->begin(scratch, sizeof(scratch));
            // Expected identifiers are frozen, not borrowed from request arena.
            std::memset(expected_object, 0, 32);
            std::memset(expected_revision, 0, 32);
            if (fault == 4) ++media;
            if (fault == 5) input->cancel();
            unsigned steps = 0;
            while (result == ManifestFileResult::Pending && steps++ < 1000) result = input->step(fault == 6 ? 2 : 1, actor, scratch, sizeof(scratch));
            if (fault == 4) --media;
            if (fault == 7)
            {
                uint8_t changed[32] = {99};
                result = input->step(1, changed, scratch, sizeof(scratch));
            }
            if (!fault)
            {
                assert(result == ManifestFileResult::Ready && input->ready() && input->verified());
                assert(!std::memcmp(input->verified()->manifest.author.data, remote.public_key, 64));
                assert(std::memcmp(input->verified()->manifest.author.data, local.public_key, 64));
                assert(input->signedBytes().size == sw.size() && !std::memcmp(input->signedBytes().data, signed_raw, sw.size()));
            }
            else assert(!input->ready() && !input->verified() && !input->signedBytes().size);
            assert(files == before && !local.signing_calls);
            if (!fault)
            {
                auto* retained = package::createPublicPackageRetention(*input, media);
                assert(retained);
                assert(!package::createPackageRoutePublication(*input, *retained, media));
                Status copied = Status::Busy;
                // Fail a real write, retain its prefix, release only copy work,
                // then resume the original signed bytes without any signer.
                write_limit = 32;
                copied = retained->step(1, actor, scratch, sizeof(scratch));
                write_limit = SIZE_MAX;
                assert(copied == Status::StorageUnavailable && !retained->ready());
                package::destroyPublicPackageRetention(retained);
                retained = package::createPublicPackageRetention(*input, media);
                assert(retained);
                for (unsigned i = 0; i < 1000 && copied != Status::Ok; ++i) copied = retained->step(1, actor, scratch, sizeof(scratch));
                assert(copied == Status::Ok && retained->ready() && !local.signing_calls);
                char cached[160];
                const geocaching::ByteView revision_key{verified.revision_hash, 32};
                assert(resolvePackageCachePath(revision_key, ObjectFile::SignedManifest, {}, cached, sizeof(cached)) && files.at(cached) == std::vector<uint8_t>(signed_raw, signed_raw + sw.size()));
                assert(resolvePackageCachePath(revision_key, ObjectFile::KmlFile, {}, cached, sizeof(cached)) && files.at(cached) == std::vector<uint8_t>(xml.begin(), xml.end()));
                if (version == 2)
                {
                    assert(resolvePackageCachePath(revision_key, ObjectFile::AttachmentIndex, {}, cached, sizeof(cached)) && files.at(cached) == index);
                    assert(resolvePackageCachePath(revision_key, ObjectFile::AttachmentResource, {resource_hash, 32}, cached, sizeof(cached)) && files.at(cached) == resource);
                }
                for (const auto& file : files) assert(file.first.rfind("/trailmate/tracks/", 0) != 0 && file.first.rfind("/routes/", 0) != 0);
                const auto complete = files;
                package::destroyPublicPackageRetention(retained);
                retained = package::createPublicPackageRetention(*input, media);
                assert(retained);
                copied = Status::Busy;
                for (unsigned i = 0; i < 1000 && copied == Status::Busy; ++i) copied = retained->step(1, actor, scratch, sizeof(scratch));
                assert(copied == Status::Ok && files == complete);
                auto* publication = package::createPackageRoutePublication(*input, *retained, media);
                assert(publication);
                const std::string target = publication->target();
                assert(target.rfind("/routes/p-", 0) == 0 && !files.count(target));
                const auto allow = [](const char*)
                { return Status::Ok; };
                const auto blocked = [](const char*)
                { return Status::Busy; };
                const auto before_blocked = files;
                assert(publication->step(1, actor, scratch, sizeof(scratch), blocked) == Status::Busy && files == before_blocked && !publication->ready());
                write_limit = 32;
                assert(publication->step(1, actor, scratch, sizeof(scratch), allow) == Status::StorageUnavailable && !publication->ready() && !files.count(target));
                write_limit = SIZE_MAX;
                package::destroyPackageRoutePublication(publication);
                publication = package::createPackageRoutePublication(*input, *retained, media);
                assert(publication);
                copied = publication->step(1, actor, scratch, sizeof(scratch), allow);
                assert(copied == Status::Busy && !files.count(target)); // Marker alone is not a visible route.
                char marker[80];
                assert(routePackageTarget(revision_key, PackageRouteFile::Binding, marker, sizeof(marker)) && files.count(marker));
                InstalledPackageBinding installed;
                assert(decodeInstalledPackageBinding(files.at(marker).data(), files.at(marker).size(), installed) && !std::memcmp(installed.object, verified.object_id, 32) && !std::memcmp(installed.revision, verified.revision_hash, 32));
                assert(installed.count == (version == 2 ? 1 : 0) && installed.availability == (version == 2 ? 1U : 0U));
                const auto valid_marker = files.at(marker);
                for (size_t byte = 0; byte < valid_marker.size(); ++byte)
                {
                    auto bad = valid_marker;
                    bad[byte] ^= 1;
                    InstalledPackageBinding rejected;
                    assert(!decodeInstalledPackageBinding(bad.data(), bad.size(), rejected));
                }
                for (unsigned i = 0; i < 1000 && copied == Status::Busy; ++i) copied = publication->step(1, actor, scratch, sizeof(scratch), allow);
                assert(copied == Status::Ok && publication->ready() && files.at(target) == std::vector<uint8_t>(xml.begin(), xml.end()) && !local.signing_calls);
                const auto installed_files = files;
                package::destroyPackageRoutePublication(publication);
                publication = package::createPackageRoutePublication(*input, *retained, media);
                assert(publication);
                copied = Status::Busy;
                for (unsigned i = 0; i < 1000 && copied == Status::Busy; ++i) copied = publication->step(1, actor, scratch, sizeof(scratch), allow);
                assert(copied == Status::Ok && files == installed_files);
                package::destroyPackageRoutePublication(publication);
                files[marker].back() ^= 1;
                const auto corrupted_marker = files;
                publication = package::createPackageRoutePublication(*input, *retained, media);
                assert(publication);
                assert(publication->step(1, actor, scratch, sizeof(scratch), allow) == Status::Conflict && !publication->ready() && files == corrupted_marker);
                package::destroyPackageRoutePublication(publication);
                files = installed_files;
                {
                    namespace images = platform::esp::route_images;
                    const auto baseline = allocations;
                    auto* image_work = images::createRoutePackageImageWork();
                    assert(image_work && !hostlink::route_storage_lease::acquire());
                    uint8_t reference[32];
                    constexpr char image_ref[] = "photos/p.png";
                    digest(reinterpret_cast<const uint8_t*>(image_ref), sizeof(image_ref) - 1, reference);
                    auto checked = image_work->begin(target);
                    assert(!image_work->materialize(reference, "/routes/.trailmate/wrong/images/img-0001.jpg"));
                    for (unsigned i = 0; i < 1000 && checked == ManifestFileResult::Pending; ++i) checked = image_work->step();
                    assert(checked == ManifestFileResult::Ready && image_work->ready() && allocations == baseline + 1);
                    char destination[128];
                    std::snprintf(destination, sizeof(destination), "/routes/.trailmate/%.*s/images/img-0001.jpg", 66, target.data() + 8);
                    assert(!image_work->materialize(reference, "/routes/.trailmate/wrong/images/img-0001.jpg"));
                    if (version == 2)
                    {
                        const auto cached_before_images = files;
                        assert(image_work->materialize(reference, destination));
                        assert(files.at(destination) == resource);
                        const auto copied_images = files;
                        allocation_fail = true;
                        assert(!image_work->materialize(reference, destination) && files == copied_images);
                        allocation_fail = false;
                        backing_allocation_fail = true;
                        assert(!image_work->materialize(reference, destination) && files == copied_images);
                        backing_allocation_fail = false;
                        images::RouteImageSourceWork bound(platform::esp::arduino_common::storage::SdRuntimeMemoryPolicy::PsramOnly);
                        assert(bound.matchesExisting(destination, reference));
                        // Fresh copy rechecks resource bytes after package proof.
                        assert(resolvePackageCachePath(revision_key, ObjectFile::AttachmentResource, {resource_hash, 32}, cached, sizeof(cached)));
                        files[cached][0] ^= 1;
                        assert(!image_work->materialize(reference, destination));
                        ++media;
                        assert(!image_work->ready() && !image_work->materialize(reference, destination));
                        --media;
                        files = cached_before_images;
                    }
                    else assert(!image_work->materialize(reference, destination));
                    images::destroyRoutePackageImageWork(image_work);
                    assert(allocations == baseline && hostlink::route_storage_lease::acquire());
                    hostlink::route_storage_lease::release();
                    // Missing actual route is never an image proof.
                    const auto route_bytes = files.at(target);
                    files.erase(target);
                    image_work = images::createRoutePackageImageWork();
                    assert(image_work && image_work->begin(target) != ManifestFileResult::Pending && !image_work->ready());
                    images::destroyRoutePackageImageWork(image_work);
                    files[target] = route_bytes;
                    if (version == 2)
                    {
                        assert(resolvePackageCachePath(revision_key, ObjectFile::AttachmentResource, {resource_hash, 32}, cached, sizeof(cached)));
                        files[cached][0] ^= 1;
                        image_work = images::createRoutePackageImageWork();
                        assert(image_work);
                        checked = image_work->begin(target);
                        for (unsigned i = 0; i < 1000 && checked == ManifestFileResult::Pending; ++i) checked = image_work->step();
                        assert(checked == ManifestFileResult::HashMismatch && !image_work->ready() && !image_work->materialize(reference, destination));
                        images::destroyRoutePackageImageWork(image_work);
                        files[cached][0] ^= 1;
                    }
                    allocation_fail = true;
                    assert(!images::createRoutePackageImageWork());
                    allocation_fail = false;
                    backing_allocation_fail = true;
                    assert(!images::createRoutePackageImageWork());
                    backing_allocation_fail = false;
                    assert(allocations == baseline && hostlink::route_storage_lease::acquire());
                    hostlink::route_storage_lease::release();
                }
                // Cache/history never becomes an owned publication head.
                for (const auto& file : files) assert(file.first.rfind("/trailmate/tracks/", 0) != 0);
                package::destroyPublicPackageRetention(retained);
            }
            package::destroyPackageInputOwner(input);
            assert(!allocations);
        }
    }
    allocation_fail = true;
    assert(!package::createPackageInputOwner(media, 1, actor, {kml_id, 16}, {signed_id, 16}, {}, {resource_hash, 32}, {index_hash, 32}));
    allocation_fail = false;
    // Global cache and route-directory quotas count unknown/orphan entries.
    files.clear();
    for (unsigned i = 0; i < 64; ++i) files["/trailmate/packages/orphan" + std::to_string(i) + "/bytes"] = {};
    assert(prepare_runtime::ensurePackageCacheDirectory(media, {index_hash, 32}, scratch, sizeof(scratch)) == Status::LimitExceeded);
    assert(prepare_runtime::ensurePackageCacheDirectory(media, {index_hash, 32}, scratch, 160) == Status::InvalidField);
    files.clear();
    for (unsigned i = 0; i < 256; ++i) files["/routes/orphan" + std::to_string(i)] = {};
    assert(package::admitPackageRouteStorage(media, {index_hash, 32}, 1, scratch, sizeof(scratch)) == Status::LimitExceeded);
    files = saved;
}
static int packageProtocolBridge()
{
    PersistenceAuthor adapter;
    uint8_t session[16] = {1}, author_hash[32], delivery[32], prefix[32], scratch[471], input[471];
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    assert(hash.start() && hash.update(adapter.public_key, 64) && hash.finish(author_hash));
    hash.reset();
    constexpr char aspect[] = "lxmf.delivery";
    assert(hash.start() && hash.update(reinterpret_cast<const uint8_t*>(aspect), sizeof(aspect) - 1) && hash.finish(prefix));
    hash.reset();
    assert(hash.start() && hash.update(prefix, 10) && hash.update(author_hash, 16) && hash.finish(delivery));
    hash.reset();
    const auto emit = [&](Status status, const uint8_t* bytes, size_t size)
    {
        std::printf("%u ", unsigned(status));
        for (size_t i = 0; i < size; ++i) std::printf("%02x", bytes[i]);
        std::printf("\n");
        std::fflush(stdout);
    };
    emit(Status::Ok, adapter.public_key, 64);
    uint64_t now = 100;
    std::string line;
    while (std::getline(std::cin, line))
    {
        size_t written = 0;
        Status status = Status::InvalidField;
        if (line == "state")
        {
            geocaching::protocol::CmpWriter writer(scratch, sizeof(scratch));
            assert(writer.array(2) && writer.unsignedInteger(adapter.signing_calls) && writer.unsignedInteger(allocations));
            emit(Status::Ok, scratch, writer.size());
            continue;
        }
        if (line == "close")
        {
            prepare_runtime::closePrepareSession(session);
            upload_runtime::closeUploadSession(session);
            scratch[0] = 0x90;
            emit(Status::Ok, scratch, 1);
            continue;
        }
        if (line == "clear-uploads" || line == "corrupt-cache" || line == "remove-installed")
        {
            upload_runtime::closeUploadSession(session);
            for (auto it = files.begin(); it != files.end();)
            {
                if (line == "clear-uploads" && it->first.rfind("/trailmate/web-", 0) == 0)
                {
                    it = files.erase(it);
                    continue;
                }
                if (line == "remove-installed" && it->first.rfind("/routes/p-", 0) == 0 && it->first.substr(it->first.size() - 4) == ".kml")
                {
                    it = files.erase(it);
                    continue;
                }
                if (line == "corrupt-cache" && it->first.rfind("/trailmate/packages/", 0) == 0 && it->first.substr(it->first.size() - 4) == ".ast" && !it->second.empty()) it->second.back() ^= 1;
                ++it;
            }
            scratch[0] = 0x90;
            emit(Status::Ok, scratch, 1);
            continue;
        }
        std::istringstream fields(line);
        unsigned operation = 0;
        std::string hex, tail;
        const auto digit = [](char c)
        { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                       : -1; };
        bool valid = bool(fields >> operation >> hex) && !(fields >> tail) && operation <= UINT16_MAX && !hex.empty() && !(hex.size() % 2) && hex.size() <= sizeof(input) * 2;
        if (valid)
            for (size_t i = 0; i < hex.size() / 2; ++i)
            {
                const int a = digit(hex[2 * i]), b = digit(hex[2 * i + 1]);
                if (a < 0 || b < 0)
                {
                    valid = false;
                    break;
                }
                input[i] = uint8_t(a * 16 + b);
            }
        if (valid)
        {
            EnvelopeView request;
            request.code = operation;
            request.session = session;
            request.body = input;
            request.body_size = hex.size() / 2;
            upload_runtime::advanceUploads(scratch, sizeof(scratch), now, true);
            if (operation == 1)
            {
                geocaching::protocol::CmpWriter writer(scratch, sizeof(scratch));
                assert(writer.array(4) && writer.binary({adapter.public_key, 64}) && writer.binary({author_hash, 16}) && writer.binary({delivery, 16}) && writer.unsignedInteger(1));
                status = Status::Ok;
                written = writer.size();
            }
            else if (operation >= 32 && operation <= 36)
                status = upload_runtime::processUploadRequest(request, 1, author_hash, now, scratch, sizeof(scratch), written);
            else if (operation == 82 || operation == 96 || operation == 99 || operation == 100 || operation == 101)
                status = prepare_runtime::processPrepareRequest(adapter, request, now, scratch, sizeof(scratch), written, [](const char*, uint8_t*, size_t)
                                                                { return Status::Ok; });
            else status = Status::Unsupported;
            ++now;
        }
        if (status != Status::Ok)
        {
            scratch[0] = 0x90;
            written = 1;
        }
        emit(status, scratch, written);
    }
    prepare_runtime::releasePrepare();
    upload_runtime::closeUploadSession(session);
    assert(!allocations);
    return 0;
}
int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "package-protocol")) return packageProtocolBridge();
    testAttachmentPreparation();
    testPackagePrepareWorkflow();
    testSharedPackageInput();
    {
        namespace upload = hostlink::management::upload_runtime;
        const auto saved = files;
        const auto before = allocations;
        uint8_t id[16] = {70}, session[16] = {71}, other[16] = {72}, digest[32] = {73}, author[32] = {74};
        uint8_t raw[128], output[448], content[2] = {9, 8};
        assert(upload::beginNewUpload(upload::owner, id, session, media, 7, author, 3, digest, 100, output, sizeof(output)) == Status::Ok);
        assert(upload::owner->resources.append(id, 0, content, sizeof(content), output, sizeof(output)) == track_sharing::JournalStoreResult::Ready);
        geocaching::protocol::CmpWriter clean(raw, sizeof(raw));
        assert(clean.array(4) && clean.binary({id, 16}) && clean.unsignedInteger(3) && clean.unsignedInteger(3) && clean.binary({digest, 32}));
        EnvelopeView request{session, session, uint16_t(Operation::TransferCleanup), raw, clean.size()};
        size_t written = 0;
        const auto populated = files;
        assert(upload::cleanupBoundUpload(request, 8, author, 101, output, sizeof(output), written) == Status::StaleIdentity && files == populated);
        request.session = other;
        assert(upload::cleanupBoundUpload(request, 7, author, 102, output, sizeof(output), written) == Status::Unauthorized && files == populated);
        request.session = session;
        char path[64];
        assert(track_sharing::resolveUploadPath(id, 16, track_sharing::UploadFile::JournalB, path, sizeof(path)));
        remove_failure_path = path;
        assert(upload::cleanupBoundUpload(request, 7, author, 103, output, sizeof(output), written) == Status::StorageUnavailable);
        assert(upload::owner && upload::owner->discarding && files.count(path));
        upload::closeUploadSession(session);
        assert(!upload::owner && allocations == before);
        remove_failure_path.clear();
        // Catalogue recovers metadata even though the candidate and A journal
        // are already removed. It never reads candidate bytes or alters files.
        const auto partial = files;
        uint8_t catalog[8] = {0x92, 3, 0xc0};
        EnvelopeView list{session, session, uint16_t(Operation::TransferListOwned), catalog, 3};
        assert(upload::listOwnedUploads(list, 7, author, 104, output, sizeof(output), written) == Status::Ok && !upload::owner && allocations == before && files == partial);
        geocaching::protocol::CmpReader reader({output, written});
        size_t fields = 0;
        uint64_t value = 0;
        geocaching::ByteView found;
        assert(reader.array(fields, 3) && fields == 3 && reader.unsignedInteger(value) && value == media && reader.unsignedInteger(value));
        assert(reader.array(fields, 7) && fields == 7 && reader.binary(found, 16) && !std::memcmp(found.data, id, 16));
        assert(reader.unsignedInteger(value) && value == 3 && reader.unsignedInteger(value) && value == 3 && reader.binary(found, 32) && !std::memcmp(found.data, digest, 32));
        assert(reader.unsignedInteger(value) && value == 7 && reader.unsignedInteger(value) && value == 0 && reader.unsignedInteger(value) && value == 2 && reader.finished());
        uint8_t foreign[32] = {75};
        assert(upload::listOwnedUploads(list, 7, foreign, 104, output, sizeof(output), written) == Status::Ok && files == partial && allocations == before);
        geocaching::protocol::CmpReader hidden({output, written});
        assert(hidden.array(fields, 3) && fields == 3 && hidden.unsignedInteger(value) && hidden.unsignedInteger(value) && hidden.nil() && hidden.finished());
        backing_allocation_fail = true;
        assert(upload::listOwnedUploads(list, 7, author, 104, output, sizeof(output), written) == Status::Busy && files == partial && allocations == before);
        backing_allocation_fail = false;
        const auto valid_journal = files[path];
        files[path][0] ^= 1;
        const auto corrupted = files;
        assert(upload::cleanupBoundUpload(request, 7, author, 104, output, sizeof(output), written) == Status::Conflict && !upload::owner && files == corrupted && allocations == before);
        files[path] = valid_journal;
        // A changed hash cannot delete the remaining bound journal.
        raw[clean.size() - 1] ^= 1;
        assert(upload::cleanupBoundUpload(request, 7, author, 105, output, sizeof(output), written) == Status::Conflict && !upload::owner && files == partial && allocations == before);
        raw[clean.size() - 1] ^= 1;
        assert(upload::cleanupBoundUpload(request, 7, author, 106, output, sizeof(output), written) == Status::Ok && !upload::owner && written == 1 && output[0] == 0x90 && files == saved && allocations == before);
        assert(upload::cleanupBoundUpload(request, 7, author, 107, output, sizeof(output), written) == Status::Ok && files == saved && allocations == before);
        // An unbound orphan is never removed merely because both journals miss.
        assert(track_sharing::resolveUploadPath(id, 16, track_sharing::UploadFile::Candidate, path, sizeof(path)));
        files[path] = {1, 2, 3};
        const auto orphan = files;
        assert(upload::cleanupBoundUpload(request, 7, author, 108, output, sizeof(output), written) == Status::Conflict && files == orphan && !upload::owner && allocations == before);
        files = saved;
        catalog[0] = 0x92;
        catalog[1] = 3;
        catalog[2] = 0x92;
        catalog[3] = uint8_t(media + 1);
        catalog[4] = 0;
        list.body_size = 5;
        assert(upload::listOwnedUploads(list, 7, author, 109, output, sizeof(output), written) == Status::Conflict && allocations == before);
    }
    {
        namespace confirmation = hostlink::management::ip_confirmation_runtime;
        uint8_t id[16] = {1}, hash[32] = {2}, key[64] = {3};
        const track_sharing::IpConfirmationRequest request{{id, 16}, {id, 16}, {hash, 32}, {key, 64}, {id, 16}, {id, 16}, {id, 16}, {hash, 32}, {hash, 32}, 1, 1};
        const auto before = allocations;
        allocation_fail = true;
        assert(!confirmation::createOwner(request, media, key, hash) && allocations == before);
        allocation_fail = false;
        backing_allocation_fail = true;
        assert(!confirmation::createOwner(request, media, key, hash) && allocations == before);
        backing_allocation_fail = false;
        auto* owner = confirmation::createOwner(request, media, key, hash);
        assert(owner && owner->backingReady() && allocations == before + 1 && !owner->result());
        confirmation::destroyOwner(owner);
        assert(allocations == before);
        // Controller's storage gate and lifetime retain only a PSRAM pointer.
        confirmation::controller = new (heap_caps_malloc(sizeof(confirmation::Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) confirmation::Controller;
        confirmation::controller->media = media;
        confirmation::controller->last = 100;
        std::memcpy(confirmation::controller->session, id, 16);
        confirmation::controller->work = confirmation::createOwner(request, media, key, hash);
        assert(confirmation::holdsStorage() && confirmation::blocksOperation(uint16_t(Operation::TransferBegin)) &&
               confirmation::blocksOperation(uint16_t(Operation::IdentityBackupRestore)) &&
               !confirmation::blocksOperation(uint16_t(Operation::TrackConfirmPublication)) &&
               !confirmation::blocksOperation(uint16_t(Operation::SessionClose)));
        uint8_t other[16] = {4};
        confirmation::closeSession(other);
        assert(confirmation::controller);
        confirmation::tick(30099, true);
        assert(confirmation::controller);
        confirmation::tick(30100, true);
        assert(!confirmation::controller && allocations == before);
        PersistenceAuthor adapter;
        adapter.read_failure = chat::DeviceAuthorResult::Unavailable;
        uint8_t raw[288], scratch[448];
        geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
        assert(writer.array(11) && writer.binary(request.operation) && writer.binary(request.transfer) && writer.binary(request.proof_hash) &&
               writer.unsignedInteger(1) && writer.binary(request.directory_public) && writer.binary(request.directory) && writer.binary(request.epoch) &&
               writer.binary(request.network_request) && writer.binary(request.object) && writer.binary(request.revision_hash) && writer.unsignedInteger(1));
        const EnvelopeView envelope{id, id, uint16_t(Operation::TrackConfirmPublication), raw, writer.size()};
        size_t written = 0;
        assert(confirmation::processConfirmation(adapter, envelope, 100, scratch, sizeof(scratch), written) == Status::Busy);
        assert(confirmation::controller && !confirmation::holdsStorage() && !written);
        auto wrong_session = envelope;
        wrong_session.session = other;
        assert(confirmation::processConfirmation(adapter, wrong_session, 101, scratch, sizeof(scratch), written) == Status::Conflict);
        // An in-progress request cannot be replaced, even by its own session.
        raw[writer.size() - 1] = 2;
        assert(confirmation::processConfirmation(adapter, envelope, 102, scratch, sizeof(scratch), written) == Status::Conflict);
        // Model a completed response cache with its PSRAM work still retained.
        confirmation::controller->work = confirmation::createOwner(request, media, key, hash);
        confirmation::controller->status = Status::Ok;
        assert(confirmation::processConfirmation(adapter, wrong_session, 103, scratch, sizeof(scratch), written) == Status::Conflict);
        assert(confirmation::processConfirmation(adapter, envelope, 104, scratch, sizeof(scratch), written) == Status::Busy);
        assert(confirmation::controller && !confirmation::controller->work && confirmation::controller->decoded.revision == 2 && allocations == before + 1);
        confirmation::closeSession(id);
        assert(!confirmation::controller && allocations == before);
    }
    testRouteInstallOwner();
    testReadOwner();
    testReadRegionalOwner();
    {
        namespace storage = platform::esp::arduino_common::storage;
        storage::SdRuntimeFile file;
        storage::SdBorrowedRecordFileIo io(file, media);
        files["/copy-source"] = std::vector<uint8_t>(750, 17);
        files["/copy-temp"] = {};
        uint8_t scratch[320];
        uint32_t next = 0;
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Pending && next == 320 && !file.is_open());
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 320, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Pending && next == 640 && !file.is_open());
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 640, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Copied && next == 750 && !file.is_open());
        assert(files["/copy-temp"] == files["/copy-source"]);
        const auto saved = files;
        next = 19;
        std::strcpy(reinterpret_cast<char*>(scratch), "/copy-source");
        assert(track_sharing::copyFileSlice(io, reinterpret_cast<char*>(scratch), "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Invalid && next == 19 && files == saved && !file.is_open());
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, reinterpret_cast<uint8_t*>(&next), sizeof(next), next) == track_sharing::FileCopyResult::Invalid && next == 19 && files == saved && !file.is_open());
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, reinterpret_cast<uint8_t*>(&io), sizeof(io), next) == track_sharing::FileCopyResult::Invalid && next == 19 && files == saved && !file.is_open());
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Conflict && next == 0 && files == saved && !file.is_open());
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 751, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Conflict && files == saved && !file.is_open());
        storage::SdBorrowedRecordFileIo wrong_media(file, media + 1);
        assert(track_sharing::copyFileSlice(wrong_media, "/copy-source", "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::StorageFailure && next == 0 && files == saved && !file.is_open());
        files["/copy-temp"].clear();
        write_limit = 17;
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::StorageFailure && next == 0 && !file.is_open());
        assert(files["/copy-temp"].size() == 17 && files["/copy-source"] == saved.at("/copy-source"));
        write_limit = SIZE_MAX;
        const auto short_written = files;
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Conflict && next == 0 && files == short_written && !file.is_open());
        files["/copy-temp"].clear();
        flush_failure = true;
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::StorageFailure && next == 0 && !file.is_open());
        assert(files["/copy-temp"].size() == 320 && files["/copy-source"] == saved.at("/copy-source"));
        flush_failure = false;
        const auto unsynced = files;
        assert(track_sharing::copyFileSlice(io, "/copy-source", "/copy-temp", 750, 0, scratch, sizeof(scratch), next) == track_sharing::FileCopyResult::Conflict && next == 0 && files == unsynced && !file.is_open());
        files.erase("/copy-source");
        files.erase("/copy-temp");
        files["/verified-temp"] = std::vector<uint8_t>(750, 17);
        mbedtls_sha256_context context;
        platform::esp::storage::SnapshotStreamSha256 hash(context);
        uint8_t expected[32];
        assert(hash.start() && hash.update(files["/verified-temp"].data(), 750) && hash.finish(expected));
        track_sharing::VerifiedFilePublish<storage::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> publisher(io, hash);
        assert(publisher.begin("/verified-temp", "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Pending);
        auto verified = track_sharing::FileIntegrityResult::Pending;
        for (unsigned step = 0; step < 16 && verified == track_sharing::FileIntegrityResult::Pending; ++step) verified = publisher.step(scratch, sizeof(scratch));
        assert(verified == track_sharing::FileIntegrityResult::Ready && !files.count("/verified-temp") && files["/verified-final"] == std::vector<uint8_t>(750, 17) && !file.is_open());
        const auto published_files = files;
        assert(publisher.begin("/verified-temp", "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Pending);
        do
        {
            verified = publisher.step(scratch, sizeof(scratch));
        } while (verified == track_sharing::FileIntegrityResult::Pending);
        assert(verified == track_sharing::FileIntegrityResult::Ready && files == published_files && !file.is_open());
        assert(publisher.begin(nullptr, "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Invalid);
        assert(publisher.step(scratch, sizeof(scratch)) == track_sharing::FileIntegrityResult::Invalid && files == published_files);
        files["/verified-final"][0] ^= 1;
        files["/verified-temp"] = std::vector<uint8_t>(750, 17);
        const auto corrupt_final = files;
        assert(publisher.begin("/verified-temp", "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Pending);
        do
        {
            verified = publisher.step(scratch, sizeof(scratch));
        } while (verified == track_sharing::FileIntegrityResult::Pending);
        assert(verified == track_sharing::FileIntegrityResult::HashMismatch && files == corrupt_final && !file.is_open());
        files.erase("/verified-final");
        files["/verified-temp"][0] ^= 1;
        const auto corrupt_temp = files;
        assert(publisher.begin("/verified-temp", "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Pending);
        do
        {
            verified = publisher.step(scratch, sizeof(scratch));
        } while (verified == track_sharing::FileIntegrityResult::Pending);
        assert(verified == track_sharing::FileIntegrityResult::HashMismatch && files == corrupt_temp && !file.is_open());
        files["/verified-temp"][0] ^= 1;
        rename_failure = true;
        const auto before_publish_failure = files;
        assert(publisher.begin("/verified-temp", "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Pending);
        do
        {
            verified = publisher.step(scratch, sizeof(scratch));
        } while (verified == track_sharing::FileIntegrityResult::Pending);
        assert(verified == track_sharing::FileIntegrityResult::StorageFailure && files == before_publish_failure && !file.is_open());
        rename_failure = false;
        assert(publisher.begin("/verified-temp", "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Pending);
        do
        {
            verified = publisher.step(scratch, sizeof(scratch));
        } while (verified == track_sharing::FileIntegrityResult::Pending);
        assert(verified == track_sharing::FileIntegrityResult::Ready && !files.count("/verified-temp") && !file.is_open());
        files.erase("/verified-final");
        files["/verified-temp"] = std::vector<uint8_t>(750, 17);
        corrupt_after_rename = true;
        assert(publisher.begin("/verified-temp", "/verified-final", 750, expected) == track_sharing::FileIntegrityResult::Pending);
        do
        {
            verified = publisher.step(scratch, sizeof(scratch));
        } while (verified == track_sharing::FileIntegrityResult::Pending);
        assert(verified == track_sharing::FileIntegrityResult::HashMismatch && files.count("/verified-final") && !files.count("/verified-temp") && !file.is_open());
        corrupt_after_rename = false;
        files.erase("/verified-temp");
        files.erase("/verified-final");
        using Copier = track_sharing::ImmutableFileCopy<storage::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256>;
        static_assert(sizeof(Copier) <= 224, "Immutable copier cannot own file buffers");
        Copier copier(io, hash);
        files["/immutable-source"] = std::vector<uint8_t>(750, 17);
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        auto preserved = track_sharing::ImmutableCopyResult::Pending;
        for (unsigned step = 0; step < 24 && preserved == track_sharing::ImmutableCopyResult::Pending; ++step) preserved = copier.step(scratch, sizeof(scratch));
        assert(preserved == track_sharing::ImmutableCopyResult::Ready && files["/immutable-source"] == files["/immutable-final"] && !files.count("/immutable-temp") && !file.is_open());
        const auto immutable_files = files;
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        do
        {
            preserved = copier.step(scratch, sizeof(scratch));
        } while (preserved == track_sharing::ImmutableCopyResult::Pending);
        assert(preserved == track_sharing::ImmutableCopyResult::Ready && files == immutable_files);
        files.erase("/immutable-final");
        files["/immutable-temp"] = {1, 2, 3};
        const auto partial_files = files;
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        assert(copier.step(scratch, sizeof(scratch)) == track_sharing::ImmutableCopyResult::Conflict && files == partial_files && !file.is_open());
        files["/immutable-temp"] = std::vector<uint8_t>(317, 17);
        const auto before_prefix_sync = files;
        flush_failure = true;
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        do
        {
            preserved = copier.step(scratch, sizeof(scratch));
        } while (preserved == track_sharing::ImmutableCopyResult::Pending);
        assert(preserved == track_sharing::ImmutableCopyResult::StorageFailure && files == before_prefix_sync && !file.is_open());
        flush_failure = false;
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        do
        {
            preserved = copier.step(scratch, sizeof(scratch));
        } while (preserved == track_sharing::ImmutableCopyResult::Pending);
        assert(preserved == track_sharing::ImmutableCopyResult::Ready && files["/immutable-final"] == files["/immutable-source"] && !files.count("/immutable-temp") && !file.is_open());
        files.erase("/immutable-final");
        files["/immutable-temp"] = std::vector<uint8_t>(750, 17);
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        assert(copier.cancel() == track_sharing::ImmutableCopyResult::Cancelled && copier.step(scratch, sizeof(scratch)) == track_sharing::ImmutableCopyResult::Cancelled);
        const auto cancelled_files = files;
        assert(files.count("/immutable-temp") && !files.count("/immutable-final") && !file.is_open());
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        assert(copier.step(nullptr, 0) == track_sharing::ImmutableCopyResult::Invalid && files == cancelled_files);
        assert(copier.begin("/immutable-source", "/immutable-temp", "/immutable-final", 750, expected) == track_sharing::ImmutableCopyResult::Pending);
        do
        {
            preserved = copier.step(scratch, sizeof(scratch));
        } while (preserved == track_sharing::ImmutableCopyResult::Pending);
        assert(preserved == track_sharing::ImmutableCopyResult::Ready && !files.count("/immutable-temp") && files["/immutable-final"] == files["/immutable-source"] && !file.is_open());
        files.erase("/immutable-final");
        files.erase("/immutable-source");
        files.erase("/immutable-temp");
    }
    {
        uint8_t object[32] = {7}, scratch[320];
        uint8_t retention_hash[32] = {8};
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::Ok);
        assert(prepare_runtime::admitRetainedKml(media + 1, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::StorageUnavailable);
        char path[160];
        assert(prepare_runtime::ensureObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Ok);
        assert(prepare_runtime::ensureObjectDirectory(media + 1, {object, 32}, scratch, sizeof(scratch)) == Status::StorageUnavailable);
        assert(prepare_runtime::requireEmptyObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Ok);
        assert(track_sharing::resolveObjectPath({object, 32}, track_sharing::ObjectFile::Directory, {}, path, sizeof(path)));
        const auto retention_root = std::string(path);
        for (unsigned i = 0; i < 128; ++i) files[retention_root + "/entry" + std::to_string(i)] = {1};
        const auto full_count = files;
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::LimitExceeded && files == full_count);
        char retained_temp[160];
        assert(track_sharing::resolveObjectPath({object, 32}, track_sharing::ObjectFile::KmlTemporary, {retention_hash, 32}, retained_temp, sizeof(retained_temp)));
        files.erase(retention_root + "/entry127");
        files[retained_temp] = std::vector<uint8_t>(300, 17);
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::Ok);
        files[retained_temp].resize(751);
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::Conflict);
        files.erase(retained_temp);
        for (unsigned i = 0; i < 127; ++i) files.erase(retention_root + "/entry" + std::to_string(i));
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {scratch, 32}, 750, scratch, sizeof(scratch)) == Status::InvalidField);
        files[retention_root + "/unknown-large"] = std::vector<uint8_t>(33554432 - 750, 0);
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::Ok);
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 751, scratch, sizeof(scratch)) == Status::LimitExceeded);
        files[retained_temp] = std::vector<uint8_t>(300, 17);
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::Ok);
        files.erase(retained_temp);
        char retained_final[160];
        assert(track_sharing::resolveObjectPath({object, 32}, track_sharing::ObjectFile::KmlFile, {retention_hash, 32}, retained_final, sizeof(retained_final)));
        files[retained_final] = std::vector<uint8_t>(750, 17);
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::Ok);
        uint8_t different_hash[32] = {9};
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {different_hash, 32}, 1, scratch, sizeof(scratch)) == Status::LimitExceeded);
        files.erase(retained_final);
        files.erase(retention_root + "/unknown-large");
        using DirectoryStatus = platform::esp::arduino_common::storage::SdFileReadStatus;
        directory_status[path] = DirectoryStatus::Missing;
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::StorageUnavailable);
        assert(prepare_runtime::requireEmptyObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Ok);
        assert(prepare_runtime::ensureObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Ok && created_directories.size() == 1 && created_directories[0] == path);
        created_directories.clear();
        directory_status[path] = DirectoryStatus::Busy;
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::Busy);
        assert(prepare_runtime::ensureObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Busy && created_directories.empty());
        assert(prepare_runtime::requireEmptyObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Busy);
        directory_status[path] = DirectoryStatus::IoError;
        assert(prepare_runtime::admitRetainedKml(media, {object, 32}, {retention_hash, 32}, 750, scratch, sizeof(scratch)) == Status::StorageUnavailable);
        assert(prepare_runtime::ensureObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::StorageUnavailable && created_directories.empty());
        assert(prepare_runtime::requireEmptyObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::StorageUnavailable);
        directory_status[path] = DirectoryStatus::Missing;
        mkdir_fail = true;
        assert(prepare_runtime::ensureObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::StorageUnavailable && created_directories.size() == 1);
        mkdir_fail = false;
        created_directories.clear();
        directory_status.clear();
        const auto orphan = std::string(path) + "/orphan.cmp";
        files[orphan] = {1, 2, 3};
        const auto saved = files;
        assert(prepare_runtime::requireEmptyObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Conflict && files == saved);
        assert(prepare_runtime::requireEmptyObjectDirectory(media + 1, {object, 32}, scratch, sizeof(scratch)) == Status::StorageUnavailable && files == saved);
        files.erase(orphan);
        assert(prepare_runtime::requireEmptyObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Ok);
        namespace sd = platform::esp::arduino_common::storage;
        sd::SdRuntimeFile file;
        sd::SdBorrowedRecordFileIo io(file, media);
        track_sharing::RevisionReservation reservation, verify;
        std::memcpy(reservation.object, object, 32);
        reservation.author[0] = 9;
        reservation.candidate[0] = 8;
        reservation.operation[0] = 3;
        reservation.sequence = reservation.revision = 1;
        reservation.generation = 7;
        char a[160], b[160];
        assert(track_sharing::resolveObjectPath({object, 32}, track_sharing::ObjectFile::ReservationA, {reservation.operation, 16}, a, sizeof(a)));
        assert(track_sharing::resolveObjectPath({object, 32}, track_sharing::ObjectFile::ReservationB, {reservation.operation, 16}, b, sizeof(b)));
        track_sharing::ReservationStore<sd::SdBorrowedRecordFileIo> store(io, a, b);
        assert(store.initialize(reservation, verify, scratch, sizeof(scratch)) == track_sharing::ReservationStoreResult::Ready);
        track_sharing::PrepareRequest request;
        request.operation = {reservation.operation, 16};
        request.generation = 7;
        const auto owned_files = files;
        assert(prepare_runtime::requireEmptyObjectDirectory(media, {object, 32}, scratch, sizeof(scratch)) == Status::Conflict);
        assert(prepare_runtime::requireOwnedFirstRevisionDirectory(media, request, reservation.author, reservation, store, verify, scratch, sizeof(scratch)) == Status::Ok && files == owned_files);
        files[orphan] = {1};
        const auto blocked_files = files;
        assert(prepare_runtime::requireOwnedFirstRevisionDirectory(media, request, reservation.author, reservation, store, verify, scratch, sizeof(scratch)) == Status::Conflict && files == blocked_files);
        files.erase(orphan);
        request.generation = 8;
        assert(prepare_runtime::requireOwnedFirstRevisionDirectory(media, request, reservation.author, reservation, store, verify, scratch, sizeof(scratch)) == Status::Conflict && files == owned_files);
        files.erase(a);
    }
    {
        UploadAuthorAdapter adapter;
        uint8_t key[64], hash[32];
        uint32_t generation = 0;
        assert(upload_runtime::readUploadAuthor(adapter, key, sizeof(key), hash, generation) == Status::Ok && generation == 7);
        mbedtls_sha256_context context;
        platform::esp::storage::SnapshotStreamSha256 verify_hash(context);
        uint8_t expected[32];
        assert(verify_hash.start() && verify_hash.update(key, 64) && verify_hash.finish(expected) && !std::memcmp(expected, hash, 32));
        adapter.result = chat::DeviceAuthorResult::StorageError;
        assert(upload_runtime::readUploadAuthor(adapter, key, sizeof(key), hash, generation) == Status::StorageUnavailable && generation == 0);
        for (auto byte : hash) assert(!byte);
        adapter.result = chat::DeviceAuthorResult::Ok;
        adapter.generation = 0;
        assert(upload_runtime::readUploadAuthor(adapter, key, sizeof(key), hash, generation) == Status::StaleIdentity && generation == 0);
    }
    uint8_t session[16] = {1}, id[16] = {2}, payload[256], output[471];
    size_t written = 0;
    auto request = [&](uint32_t expected, uint32_t count)
    {geocaching::protocol::CmpWriter writer(payload,sizeof(payload));assert(writer.array(5)&&writer.binary({id,16})&&writer.unsignedInteger(expected)&&writer.text("ridge.gpx")&&writer.unsignedInteger(count)&&writer.unsignedInteger(1));EnvelopeView view;view.session=session;view.body=payload;view.body_size=writer.size();return view; };
    files["/trackers/ridge.gpx"].resize(750, 42);
    allocation_fail = true;
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Busy && allocations == 0);
    allocation_fail = false;
    backing_allocation_fail = true;
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Busy);
    assert(!runtime::owner && allocations == 0 && !held);
    backing_allocation_fail = false;
    recording = true;
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Busy && !runtime::owner && !held);
    recording = false;
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Ok && held && allocations == 1);
    assert(runtime::build(request(1, 749), output, sizeof(output), written) == Status::Conflict);
    for (unsigned i = 0; i < 4; ++i) runtime::advance(output, sizeof(output), true);
    assert(runtime::owner->task.state() == SnapshotSessionState::Ready && !held);
    const std::string destination = runtime::owner->destination;
    assert(files.count(destination) && files[destination].size() == 814);
    assert(std::equal(files[destination].begin() + 64, files[destination].end(), files["/trackers/ridge.gpx"].begin()));
    {
        geocaching::protocol::CmpWriter writer(payload, sizeof(payload));
        assert(writer.array(4) && writer.binary({id, 16}) && writer.unsignedInteger(0) && writer.unsignedInteger(320) && writer.nil());
        EnvelopeView read;
        read.body = payload;
        read.body_size = writer.size();
        assert(readTrackSnapshotService(read, runtime::owner->io, output, sizeof(output), written) == Status::Ok && written > 320);
    }
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Ok);
    runtime::advance(output, sizeof(output), false);
    assert(!runtime::owner && allocations == 0 && files.count(destination));
    // The actual full-file SD reader rejects the one-byte existence probe for
    // an existing large snapshot; it must still refuse any overwrite.
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::StorageUnavailable && allocations == 0);
    id[0] = 3;
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Ok && held);
    const std::string temporary = runtime::owner->temporary;
    remove_fail = true;
    runtime::advance(output, sizeof(output), false);
    assert(runtime::owner && held && allocations == 1 && files.count(temporary));
    remove_fail = false;
    runtime::advance(output, sizeof(output), false);
    assert(!runtime::owner && !held && allocations == 0 && !files.count(temporary));
    id[0] = 4;
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Ok);
    ++media;
    runtime::advance(output, sizeof(output), false);
    assert(!runtime::owner && !held && allocations == 0);
    assert(runtime::build(request(1, 750), output, sizeof(output), written) == Status::Conflict);
    id[0] = 5;
    assert(runtime::build(request(media, 750), output, sizeof(output), written) == Status::Ok);
    uint8_t unrelated[16] = {9};
    runtime::closeSession(unrelated);
    runtime::advance(output, sizeof(output), true);
    assert(runtime::owner && held);
    runtime::closeSession(session);
    runtime::advance(output, sizeof(output), true);
    assert(!runtime::owner && !held && allocations == 0);
    id[0] = 6;
    assert(runtime::build(request(media, 750), output, sizeof(output), written) == Status::Ok);
    const auto releaseRequest = [&]()
    {geocaching::protocol::CmpWriter writer(payload,sizeof(payload));assert(writer.array(1)&&writer.binary({id,16}));EnvelopeView view;view.session=session;view.body=payload;view.body_size=writer.size();return view; };
    assert(runtime::releaseTrackSnapshot(releaseRequest(), runtime::owner->io, media, output, sizeof(output), written) == Status::Busy);
    for (unsigned i = 0; i < 4; ++i) runtime::advance(output, sizeof(output), true);
    const std::string released = runtime::owner->destination;
    remove_fail = true;
    assert(runtime::releaseTrackSnapshot(releaseRequest(), runtime::owner->io, media, output, sizeof(output), written) == Status::StorageUnavailable && files.count(released) && runtime::owner);
    remove_fail = false;
    assert(runtime::releaseTrackSnapshot(releaseRequest(), runtime::owner->io, media, output, sizeof(output), written) == Status::Ok && written == 1 && !runtime::owner && !files.count(released) && allocations == 0);
    platform::esp::arduino_common::storage::SdRuntimeFile file;
    platform::esp::arduino_common::storage::SdBorrowedRecordFileIo io(file, media);
    assert(runtime::releaseTrackSnapshot(releaseRequest(), io, media, output, sizeof(output), written) == Status::Ok && files.count("/trackers/ridge.gpx"));
    id[0] = 93;
    assert(runtime::build(request(media, 750), output, sizeof(output), written) == Status::Ok && held);
    assert(runtime::admitSpace(media, 750, output, sizeof(output)) == Status::Busy);
    runtime::advance(output, sizeof(output), true);
    const std::string stopped_candidate = runtime::owner->temporary;
    assert(files.at(stopped_candidate).size() == 384);
    remove_fail = true;
    assert(runtime::retireForStop() && !runtime::owner && !held && allocations == 0);
    assert(files.count(stopped_candidate) && files.count(destination));
    const auto source_before_recovery = files.at("/trackers/ridge.gpx");
    files["/trailmate/web-tracks/notes.new"] = {17};
    remove_fail = false;
    id[0] = 94;
    assert(runtime::build(request(media, 750), output, sizeof(output), written) == Status::Ok && held);
    assert(!files.count(stopped_candidate) && files.count(destination));
    assert(files.at("/trailmate/web-tracks/notes.new") == std::vector<uint8_t>{17});
    assert(files.at("/trackers/ridge.gpx") == source_before_recovery);
    assert(runtime::retireForStop() && !runtime::owner && !held && allocations == 0);
    assert(runtime::retireForStop());
    for (unsigned i = 0; i < 8; ++i) files["/trailmate/web-tracks/orphan" + std::to_string(i)] = {};
    id[0] = 7;
    assert(runtime::build(request(media, 750), output, sizeof(output), written) == Status::LimitExceeded && !runtime::owner && allocations == 0);
    {
        namespace upload = hostlink::management::upload_runtime;
        uint8_t author[32] = {3};
        assert(upload::admitUploadSpace(media, 5, output, sizeof(output)) == Status::Ok);
        assert(upload::admitUploadSpace(media + 1, 5, output, sizeof(output)) == Status::StorageUnavailable);
        allocation_fail = true;
        assert(!upload::createOwner(id, session, media, 1, author, 100) && allocations == 0);
        allocation_fail = false;
        assert(!upload::createOwner(id, session, media + 1, 1, author, 100) && allocations == 0);
        backing_allocation_fail = true;
        assert(!upload::createOwner(id, session, media, 1, author, 100) && allocations == 0);
        backing_allocation_fail = false;
        auto* owner = upload::createOwner(id, session, media, 1, author, 100);
        assert(owner && allocations == 1);
        uint8_t content[320];
        for (unsigned i = 0; i < sizeof(content); ++i) content[i] = uint8_t(i);
        uint8_t digest[32];
        assert(owner->hash.start() && owner->hash.update(content, sizeof(content)) && owner->hash.finish(digest));
        upload::destroyOwner(owner);
        owner = nullptr;
        allocation_fail = true;
        assert(upload::beginNewUpload(owner, id, session, media, 1, author, sizeof(content), digest, 100, output, sizeof(output)) == Status::Busy && !owner && allocations == 0);
        allocation_fail = false;
        assert(upload::beginNewUpload(owner, id, session, media, 1, author, sizeof(content), digest, 100, output, sizeof(output)) == Status::Ok && owner && allocations == 1);
        track_sharing::UploadRequest decoded;
        geocaching::protocol::CmpWriter chunk_command(output, sizeof(output));
        assert(chunk_command.array(3) && chunk_command.binary({id, 16}) && chunk_command.unsignedInteger(0) && chunk_command.binary({content, sizeof(content)}));
        assert(upload::processExistingUpload(*owner, track_sharing::UploadCommand::Chunk, {output, chunk_command.size()}, session, 101, decoded, output, sizeof(output), written) == Status::Ok);
        geocaching::protocol::CmpWriter finish_command(output, sizeof(output));
        assert(finish_command.array(1) && finish_command.binary({id, 16}));
        const size_t finish_size = finish_command.size();
        assert(upload::processExistingUpload(*owner, track_sharing::UploadCommand::Finish, {output, finish_size}, session, 102, decoded, output, sizeof(output), written) == Status::Ok);
        assert(owner->session.advance(media, 103, true, output, sizeof(output)) == track_sharing::UploadSealResult::Pending);
        assert(owner->session.advance(media, 104, true, output, sizeof(output)) == track_sharing::UploadSealResult::Ready);
        assert(owner->resources.metadata().phase == track_sharing::UploadPhase::Sealed);
        geocaching::protocol::CmpWriter poll(output, sizeof(output));
        assert(poll.array(1) && poll.binary({id, 16}));
        assert(upload::processExistingUpload(*owner, track_sharing::UploadCommand::Finish, {output, poll.size()}, session, 105, decoded, output, sizeof(output), written) == Status::Ok);
        geocaching::protocol::CmpReader progress({output, written});
        size_t fields = 0;
        geocaching::ByteView result_id;
        uint64_t value = 0;
        assert(progress.array(fields, 4) && progress.binary(result_id, 16) && progress.unsignedInteger(value) && value == 320 && progress.unsignedInteger(value) && value == sizeof(content) && progress.unsignedInteger(value) && value == 1 && progress.finished());
        upload::destroyOwner(owner);
        owner = nullptr;
        assert(allocations == 0);
        const auto saved_uploads = files;
        using track_sharing::UploadKind;
        assert(upload::beginNewUpload(owner, id, session, media, 1, author, 4097, digest, 100, output, sizeof(output), UploadKind::Manifest) == Status::InvalidField && !owner && files == saved_uploads);
        assert(upload::recoverUpload(upload::owner, id, session, media, 1, author, sizeof(content), digest, 99, output, sizeof(output)) == Status::Ok && upload::owner);
        uint8_t manifest_id[16] = {11};
        geocaching::protocol::CmpWriter manifest_begin(output, sizeof(output));
        assert(manifest_begin.array(4) && manifest_begin.binary({manifest_id, 16}) && manifest_begin.unsignedInteger(4) && manifest_begin.unsignedInteger(sizeof(content)) && manifest_begin.binary({digest, 32}));
        EnvelopeView manifest_request;
        manifest_request.code = static_cast<uint16_t>(Operation::TransferBegin);
        manifest_request.session = session;
        manifest_request.body = output;
        manifest_request.body_size = manifest_begin.size();
        assert(upload::routeTrackUpload(manifest_request));
        uint8_t wrong_session[16] = {12};
        manifest_request.session = wrong_session;
        assert(upload::processUploadRequest(manifest_request, 1, author, 100, output, sizeof(output), written) == Status::Unauthorized && upload::owner->kind == UploadKind::Kml && files == saved_uploads);
        manifest_request.session = session;
        assert(upload::processUploadRequest(manifest_request, 2, author, 100, output, sizeof(output), written) == Status::StaleIdentity && upload::owner->kind == UploadKind::Kml && files == saved_uploads);
        assert(upload::processUploadRequest(manifest_request, 1, author, 100, output, sizeof(output), written) == Status::Ok && upload::owner && upload::owner->kind == UploadKind::Manifest);
        const auto before_wrong_kind = files;
        geocaching::protocol::CmpWriter wrong_kind(output, sizeof(output));
        assert(wrong_kind.array(4) && wrong_kind.binary({manifest_id, 16}) && wrong_kind.unsignedInteger(3) && wrong_kind.unsignedInteger(sizeof(content)) && wrong_kind.binary({digest, 32}));
        manifest_request.body_size = wrong_kind.size();
        assert(upload::processUploadRequest(manifest_request, 1, author, 100, output, sizeof(output), written) == Status::Conflict && files == before_wrong_kind);
        owner = upload::owner;
        upload::owner = nullptr;
        const std::string manifest_candidate = owner->resources.candidatePath();
        assert(manifest_candidate.find("/web-manifest/") != std::string::npos);
        assert(owner->session.append(session, media, 101, manifest_id, 0, content, sizeof(content), output, sizeof(output)) == track_sharing::JournalStoreResult::Ready);
        assert(owner->session.beginSeal(session, media, 102, output, sizeof(output)) == track_sharing::UploadSealResult::Pending);
        track_sharing::UploadSealResult manifest_result;
        do
        {
            manifest_result = owner->session.advance(media, 103, true, output, sizeof(output));
        } while (manifest_result == track_sharing::UploadSealResult::Pending);
        assert(manifest_result == track_sharing::UploadSealResult::Ready);
        upload::destroyOwner(owner);
        owner = nullptr;
        assert(upload::recoverUpload(owner, manifest_id, session, media, 1, author, sizeof(content), digest, 200, output, sizeof(output), UploadKind::Manifest) == Status::Ok && owner);
        track_sharing::UploadJournal prepare_kml, prepare_manifest, prepare_verify;
        char prepare_kml_path[64], prepare_manifest_path[64];
        track_sharing::PrepareRequest prepare_request;
        prepare_request.kml_upload = {id, 16};
        prepare_request.manifest_upload = {manifest_id, 16};
        prepare_request.generation = 1;
        const auto before_prepare = files;
        assert(track_sharing::loadPrepareUploadInputs(owner->io, prepare_request, 1, author, prepare_kml, prepare_manifest, prepare_verify, prepare_kml_path, prepare_manifest_path, output, sizeof(output)) == track_sharing::JournalStoreResult::Ready);
        assert(!std::memcmp(prepare_kml.id, id, 16) && !std::memcmp(prepare_manifest.id, manifest_id, 16));
        assert(std::string(prepare_manifest_path) == manifest_candidate && std::string(prepare_kml_path).find("/web-upload/") != std::string::npos && files == before_prepare);
        const auto preserved_kml = prepare_kml;
        const auto preserved_manifest = prepare_manifest;
        for (auto* invalid_path : {reinterpret_cast<char*>(&prepare_kml), reinterpret_cast<char*>(&prepare_manifest), reinterpret_cast<char*>(&prepare_verify)})
        {
            assert(track_sharing::loadPrepareUploadInputs(owner->io, prepare_request, 1, author, prepare_kml, prepare_manifest, prepare_verify, invalid_path, prepare_manifest_path, output, sizeof(output)) == track_sharing::JournalStoreResult::Invalid);
            assert(!std::memcmp(&preserved_kml, &prepare_kml, sizeof(prepare_kml)) && !std::memcmp(&preserved_manifest, &prepare_manifest, sizeof(prepare_manifest)) && files == before_prepare);
        }
        prepare_request.operation = {output, 16};
        assert(track_sharing::loadPrepareUploadInputs(owner->io, prepare_request, 1, author, prepare_kml, prepare_manifest, prepare_verify, prepare_kml_path, prepare_manifest_path, output, sizeof(output)) == track_sharing::JournalStoreResult::Invalid && files == before_prepare);
        prepare_request.operation = {};
        prepare_request.generation = 2;
        assert(track_sharing::loadPrepareUploadInputs(owner->io, prepare_request, 2, author, prepare_kml, prepare_manifest, prepare_verify, prepare_kml_path, prepare_manifest_path, output, sizeof(output)) == track_sharing::JournalStoreResult::Conflict);
        assert(!prepare_kml.sequence && !prepare_manifest.sequence && !prepare_kml_path[0] && !prepare_manifest_path[0] && files == before_prepare);
        prepare_request.generation = 1;
        prepare_request.operation = {id, 16};
        PersistenceAuthor persistence_author;
        uint8_t prepare_public[64];
        std::memcpy(prepare_public, persistence_author.public_key, 64);
        allocation_fail = true;
        assert(!prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, author));
        allocation_fail = false;
        const auto files_before_backing_failure = files;
        const auto allocations_before_backing_failure = allocations;
        backing_allocation_fail = true;
        assert(!prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, author));
        assert(allocations == allocations_before_backing_failure && files == files_before_backing_failure);
        backing_allocation_fail = false;
        auto* preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, author);
        assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
        track_sharing::ManifestFileResult preparation_result;
        do
        {
            preparation_result = preparation->step(1, author, output, sizeof(output));
        } while (preparation_result == track_sharing::ManifestFileResult::Pending);
        assert(preparation_result == track_sharing::ManifestFileResult::Invalid && files == before_prepare);
        prepare_runtime::destroyPrepareOwner(preparation);
        assert(allocations == 1);
        {
            const auto original_files = files;
            prepare_request.generation = 1;
            assert(track_sharing::loadPrepareUploadInputs(owner->io, prepare_request, 1, author, prepare_kml, prepare_manifest, prepare_verify, prepare_kml_path, prepare_manifest_path, output, sizeof(output)) == track_sharing::JournalStoreResult::Ready);
            const std::string xml = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>";
            uint8_t proof_hash[32], xml_hash[32], raw_manifest[512];
            assert(owner->hash.start() && owner->hash.update(prepare_public, 64) && owner->hash.finish(proof_hash));
            assert(owner->hash.start() && owner->hash.update(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()) && owner->hash.finish(xml_hash));
            geocaching::protocol::CmpWriter manifest_writer(raw_manifest, sizeof(raw_manifest));
            assert(manifest_writer.array(14) && manifest_writer.unsignedInteger(1) && manifest_writer.binary({prepare_public, 64}) && manifest_writer.binary({id, 16}) && manifest_writer.unsignedInteger(0) && manifest_writer.unsignedInteger(1) && manifest_writer.nil() && manifest_writer.unsignedInteger(0) && manifest_writer.unsignedInteger(1) && manifest_writer.unsignedInteger(1) && manifest_writer.text("Platform") && manifest_writer.text(""));
            assert(manifest_writer.array(4) && manifest_writer.unsignedInteger(10000000) && manifest_writer.unsignedInteger(20000000) && manifest_writer.unsignedInteger(10000000) && manifest_writer.unsignedInteger(20000000));
            assert(manifest_writer.array(4) && manifest_writer.unsignedInteger(0) && manifest_writer.nil() && manifest_writer.nil() && manifest_writer.unsignedInteger(1));
            assert(manifest_writer.array(4) && manifest_writer.binary({xml_hash, 32}) && manifest_writer.unsignedInteger(xml.size()) && manifest_writer.text("application/vnd.google-earth.kml+xml") && manifest_writer.unsignedInteger(1));
            files[prepare_kml_path] = {xml.begin(), xml.end()};
            files[prepare_manifest_path] = {raw_manifest, raw_manifest + manifest_writer.size()};
            prepare_kml.total = prepare_kml.durable_offset = uint32_t(xml.size());
            prepare_manifest.total = prepare_manifest.durable_offset = uint32_t(manifest_writer.size());
            std::memcpy(prepare_kml.file_hash, xml_hash, 32);
            assert(owner->hash.start() && owner->hash.update(raw_manifest, manifest_writer.size()) && owner->hash.finish(prepare_manifest.file_hash));
            for (auto* record : {&prepare_kml, &prepare_manifest})
            {
                std::memcpy(record->author_hash, proof_hash, 32);
                record->sequence = 1;
                char journal_path[64];
                const auto kind = record == &prepare_kml ? UploadKind::Kml : UploadKind::Manifest;
                assert(track_sharing::resolveUploadPath(record->id, 16, track_sharing::UploadFile::JournalA, journal_path, sizeof(journal_path), kind));
                assert(track_sharing::encodeUploadJournal(*record, output, sizeof(output)));
                files[journal_path] = {output, output + track_sharing::kUploadJournalBytes};
                assert(track_sharing::resolveUploadPath(record->id, 16, track_sharing::UploadFile::JournalB, journal_path, sizeof(journal_path), kind));
                files.erase(journal_path);
            }
            const auto valid_files = files;
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            do
            {
                preparation_result = preparation->step(1, proof_hash, output, sizeof(output));
            } while (preparation_result == track_sharing::ManifestFileResult::Pending);
            assert(preparation_result == track_sharing::ManifestFileResult::Ready && preparation->ready());
            auto* persistence = prepare_runtime::createPreparePersistence(*preparation);
            assert(persistence && !persistence->ready());
            assert(persistence->finish(persistence_author, nullptr, output, sizeof(output)) == Status::Ok && persistence->ready() && persistence_author.signing_calls == 1);
            const auto first_head = persistence->head();
            char recovery_log[160], recovery_signature[160];
            assert(track_sharing::resolveObjectPath({persistence->head().object, 32}, track_sharing::ObjectFile::ReservationA, prepare_request.operation, recovery_log, sizeof(recovery_log)));
            assert(track_sharing::resolveObjectPath({persistence->head().object, 32}, track_sharing::ObjectFile::SignedManifest, {persistence->head().hash, 32}, recovery_signature, sizeof(recovery_signature)));
            const auto prepared_files = files;
            prepare_runtime::destroyPreparePersistence(persistence);
            persistence = prepare_runtime::createPreparePersistence(*preparation);
            assert(persistence && persistence->finish(persistence_author, nullptr, output, sizeof(output)) == Status::Ok && persistence->ready());
            assert(files == prepared_files && persistence_author.signing_calls == 1);
            prepare_runtime::destroyPreparePersistence(persistence);
            files[recovery_log][0] ^= 1;
            persistence = prepare_runtime::createPreparePersistence(*preparation);
            assert(persistence && persistence->finish(persistence_author, nullptr, output, sizeof(output)) == Status::Ok && persistence->ready());
            assert(files == prepared_files && persistence_author.signing_calls == 1);
            prepare_runtime::destroyPreparePersistence(persistence);
            files[recovery_signature].back() ^= 1;
            const auto corrupt_signature_files = files;
            persistence = prepare_runtime::createPreparePersistence(*preparation);
            assert(persistence && persistence->finish(persistence_author, nullptr, output, sizeof(output)) == Status::Conflict && !persistence->ready());
            assert(files == corrupt_signature_files && persistence_author.signing_calls == 1);
            prepare_runtime::destroyPreparePersistence(persistence);
            files = prepared_files;
            assert(preparation->view.points == 1 && preparation->view.west == 10000000 && preparation->view.south == 20000000);
            assert(preparation->manifest().size == manifest_writer.size() && !std::memcmp(preparation->manifest().data, raw_manifest, manifest_writer.size()) && files == prepared_files);
            assert(preparation->step(2, proof_hash, output, sizeof(output)) == track_sharing::ManifestFileResult::Invalid);
            assert(!preparation->ready() && !preparation->manifest().size && !preparation->matchesCandidate(preparation->view));
            assert(preparation->step(1, proof_hash, output, sizeof(output)) == track_sharing::ManifestFileResult::Invalid && files == prepared_files);
            prepare_runtime::destroyPrepareOwner(preparation);
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            do
            {
                preparation_result = preparation->step(1, proof_hash, output, sizeof(output));
            } while (preparation_result == track_sharing::ManifestFileResult::Pending);
            assert(preparation_result == track_sharing::ManifestFileResult::Ready);
            ++media;
            assert(!preparation->ready() && !preparation->manifest().size);
            assert(preparation->step(1, proof_hash, output, sizeof(output)) == track_sharing::ManifestFileResult::StorageFailure);
            --media;
            assert(preparation->step(1, proof_hash, output, sizeof(output)) == track_sharing::ManifestFileResult::StorageFailure && !preparation->ready() && files == prepared_files);
            prepare_runtime::destroyPrepareOwner(preparation);
            assert(allocations == 1);
            // Fixture supplies local task admission; no real network job exists.
            char head_a[160], head_b[160];
            assert(track_sharing::resolveObjectPath({first_head.object, 32}, track_sharing::ObjectFile::HeadA, {}, head_a, sizeof(head_a)));
            assert(track_sharing::resolveObjectPath({first_head.object, 32}, track_sharing::ObjectFile::HeadB, {}, head_b, sizeof(head_b)));
            track_sharing::ObjectHead committed = first_head, head_verify;
            committed.phase = track_sharing::HeadPhase::Committed;
            ++committed.sequence;
            track_sharing::ObjectHeadStore<prepare_runtime::sd::SdBorrowedRecordFileIo> heads(owner->io, head_a, head_b);
            assert(heads.advance(first_head, committed, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready);
            uint8_t update_id[16] = {12}, update_operation[16] = {4};
            prepare_request.manifest_upload = {update_id, 16};
            prepare_request.operation = {update_operation, 16};
            prepare_request.expected_parent = {first_head.hash, 32};
            geocaching::protocol::CmpWriter update_writer(raw_manifest, sizeof(raw_manifest));
            assert(update_writer.array(14) && update_writer.unsignedInteger(1) && update_writer.binary({prepare_public, 64}) && update_writer.binary({id, 16}) && update_writer.unsignedInteger(0) && update_writer.unsignedInteger(2) && update_writer.binary({first_head.hash, 32}) && update_writer.unsignedInteger(0) && update_writer.unsignedInteger(1) && update_writer.unsignedInteger(2) && update_writer.text("Updated platform") && update_writer.text(""));
            assert(update_writer.array(4) && update_writer.unsignedInteger(10000000) && update_writer.unsignedInteger(20000000) && update_writer.unsignedInteger(10000000) && update_writer.unsignedInteger(20000000));
            assert(update_writer.array(4) && update_writer.unsignedInteger(0) && update_writer.nil() && update_writer.nil() && update_writer.unsignedInteger(1));
            assert(update_writer.array(4) && update_writer.binary({xml_hash, 32}) && update_writer.unsignedInteger(xml.size()) && update_writer.text("application/vnd.google-earth.kml+xml") && update_writer.unsignedInteger(1));
            std::memcpy(prepare_manifest.id, update_id, 16);
            prepare_manifest.total = prepare_manifest.durable_offset = uint32_t(update_writer.size());
            assert(owner->hash.start() && owner->hash.update(raw_manifest, update_writer.size()) && owner->hash.finish(prepare_manifest.file_hash));
            assert(track_sharing::resolveUploadPath(update_id, 16, track_sharing::UploadFile::Candidate, prepare_manifest_path, sizeof(prepare_manifest_path), UploadKind::Manifest));
            files[prepare_manifest_path] = {raw_manifest, raw_manifest + update_writer.size()};
            char update_log[64];
            assert(track_sharing::resolveUploadPath(update_id, 16, track_sharing::UploadFile::JournalA, update_log, sizeof(update_log), UploadKind::Manifest));
            assert(track_sharing::encodeUploadJournal(prepare_manifest, output, sizeof(output)));
            files[update_log] = {output, output + track_sharing::kUploadJournalBytes};
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            do
            {
                preparation_result = preparation->step(1, proof_hash, output, sizeof(output));
            } while (preparation_result == track_sharing::ManifestFileResult::Pending);
            assert(preparation_result == track_sharing::ManifestFileResult::Ready);
            persistence = prepare_runtime::createPreparePersistence(*preparation);
            assert(persistence && persistence->finish(persistence_author, nullptr, output, sizeof(output)) == Status::Busy && !persistence->ready() && persistence_author.signing_calls == 1);
            Status update_result = Status::Busy;
            for (unsigned attempt = 0; attempt < 32 && update_result == Status::Busy; ++attempt) update_result = persistence->finish(persistence_author, nullptr, output, sizeof(output));
            assert(update_result == Status::Ok && persistence->ready() && persistence->head().revision == 2 && persistence_author.signing_calls == 2);
            assert(!std::memcmp(persistence->head().parent, first_head.hash, 32) && !std::memcmp(persistence->head().object, first_head.object, 32));
            std::string retained_path;
            const auto retained_bytes = files.at(preparation->kml_path);
            {
                prepare_runtime::TrackKmlRetention retention(*preparation);
                auto retained = retention.begin({first_head.object, 32});
                for (unsigned step = 0; step < 32 && retained == track_sharing::ImmutableCopyResult::Pending; ++step) retained = retention.step(output, sizeof(output));
                assert(retained == track_sharing::ImmutableCopyResult::Ready && files.at(retention.destination()) == files.at(preparation->kml_path));
                retained_path = retention.destination();
            }
            files.erase(retained_path);
            prepare_runtime::destroyPreparePersistence(persistence);
            prepare_runtime::destroyPrepareOwner(preparation);
            assert(allocations == 1);
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            auto* work = prepare_runtime::createPrepareWork(preparation, session, 100);
            assert(work && work->matches(session, prepare_request) && work->alive(media, 100, true));
            Status work_result = Status::Busy;
            for (unsigned attempt = 0; attempt < 32 && work_result == Status::Busy; ++attempt) work_result = work->advance(persistence_author, output, sizeof(output));
            assert(work_result == Status::Ok && work->ready() && work->result()->revision == 2 && allocations == 2 && persistence_author.signing_calls == 2);
            assert(files.at(retained_path) == retained_bytes);
            {
                namespace storage = platform::esp::arduino_common::storage;
                storage::SdRuntimeFile journal_file;
                storage::SdBorrowedRecordFileIo journal_io(journal_file, media);
                track_sharing::PublicationJournal intent, current_job, verify_job;
                const auto& prepared_head = *work->result();
                std::memcpy(intent.operation, prepared_head.operation, 16);
                std::memcpy(intent.object, prepared_head.object, 32);
                std::memcpy(intent.revision_hash, prepared_head.hash, 32);
                std::memcpy(intent.author_hash, proof_hash, 32);
                std::memcpy(intent.file_hash, prepare_kml.file_hash, 32);
                intent.directory[0] = 8;
                intent.directory_epoch[0] = 9;
                intent.sequence = 1;
                intent.generation = 1;
                intent.revision = 2;
                intent.file_bytes = uint32_t(retained_bytes.size());
                char slot_a[160], slot_b[160];
                assert(track_sharing::resolveObjectPath({intent.object, 32}, track_sharing::ObjectFile::PublicationA, {intent.operation, 16}, slot_a, sizeof(slot_a)));
                assert(track_sharing::resolveObjectPath({intent.object, 32}, track_sharing::ObjectFile::PublicationB, {intent.operation, 16}, slot_b, sizeof(slot_b)));
                track_sharing::PublicationJournalStore<storage::SdBorrowedRecordFileIo> journal(journal_io, slot_a, slot_b);
                assert(track_sharing::persistPublicationIntent(journal, intent, current_job, verify_job, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready);
                assert(journal.load(current_job, verify_job, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready && current_job.revision == 2 && !journal_file.is_open());
                files.erase(slot_a);
                files.erase(slot_b);
            }
            const auto cached_files = files;
            persistence_author.read_failure = chat::DeviceAuthorResult::Unavailable;
            assert(work->advance(persistence_author, output, sizeof(output)) == Status::Busy && !work->ready() && !work->result());
            persistence_author.read_failure = chat::DeviceAuthorResult::Ok;
            assert(work->advance(persistence_author, output, sizeof(output)) == Status::Ok && work->ready() && files == cached_files && persistence_author.signing_calls == 2);
            assert(!work->alive(media, 30100, true) && !work->alive(media, 99, true) && !work->alive(media, 101, false));
            uint8_t wrong_session[16] = {17};
            assert(!work->authorize(wrong_session, prepare_request, 200));
            assert(work->authorize(session, prepare_request, 200) && work->alive(media, 30199, true));
            prepare_runtime::work = work;
            prepare_runtime::closePrepareSession(wrong_session);
            assert(prepare_runtime::work == work && allocations == 2);
            assert(prepare_runtime::advancePrepare(nullptr, output, sizeof(output), 201, true) == Status::Busy && !work->ready());
            assert(prepare_runtime::advancePrepare(&persistence_author, output, sizeof(output), 202, true) == Status::Ok && work->ready());
            prepare_runtime::closePrepareSession(session);
            assert(!prepare_runtime::work);
            assert(allocations == 1);
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            prepare_runtime::work = prepare_runtime::createPrepareWork(preparation, session, 300);
            assert(prepare_runtime::work && allocations == 3);
            assert(prepare_runtime::advancePrepare(&persistence_author, output, sizeof(output), 30300, true) == Status::Expired && !prepare_runtime::work && allocations == 1 && files == cached_files);
            files[retained_path][0] ^= 1;
            const auto damaged_retention = files;
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            work = prepare_runtime::createPrepareWork(preparation, session, 350);
            assert(work);
            work_result = Status::Busy;
            for (unsigned attempt = 0; attempt < 32 && work_result == Status::Busy; ++attempt) work_result = work->advance(persistence_author, output, sizeof(output));
            assert(work_result == Status::HashMismatch && !work->ready() && !work->holdsStages() && allocations == 2 && files == damaged_retention && persistence_author.signing_calls == 2);
            prepare_runtime::destroyPrepareWork(work);
            assert(allocations == 1);
            files = cached_files;
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            work = prepare_runtime::createPrepareWork(preparation, session, 355);
            assert(work);
            work_result = Status::Busy;
            for (unsigned attempt = 0; attempt < 32 && allocations < 5 && work_result == Status::Busy; ++attempt) work_result = work->advance(persistence_author, output, sizeof(output));
            assert(work_result == Status::Busy && allocations == 5 && work->holdsStages() && !work->ready());
            const auto before_media_change = files;
            ++media;
            assert(work->advance(persistence_author, output, sizeof(output)) == Status::StorageUnavailable && !work->ready() && !work->holdsStages() && allocations == 2 && files == before_media_change);
            --media;
            assert(work->advance(persistence_author, output, sizeof(output)) == Status::StorageUnavailable && !work->ready() && files == before_media_change && persistence_author.signing_calls == 2);
            prepare_runtime::destroyPrepareWork(work);
            assert(allocations == 1);
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            work = prepare_runtime::createPrepareWork(preparation, session, 357);
            assert(work);
            work_result = Status::Busy;
            for (unsigned attempt = 0; attempt < 32 && allocations < 5 && work_result == Status::Busy; ++attempt) work_result = work->advance(persistence_author, output, sizeof(output));
            assert(work_result == Status::Busy && allocations == 5 && work->holdsStages());
            persistence_author.read_failure = chat::DeviceAuthorResult::Unavailable;
            assert(work->advance(persistence_author, output, sizeof(output)) == Status::Busy && !work->ready() && allocations == 5 && files == cached_files);
            persistence_author.read_failure = chat::DeviceAuthorResult::Ok;
            persistence_author.identity_generation = 2;
            assert(work->advance(persistence_author, output, sizeof(output)) == Status::StaleIdentity && !work->ready() && !work->holdsStages() && allocations == 2 && files == cached_files);
            persistence_author.identity_generation = 1;
            assert(work->advance(persistence_author, output, sizeof(output)) == Status::StaleIdentity && !work->ready() && files == cached_files && persistence_author.signing_calls == 2);
            prepare_runtime::destroyPrepareWork(work);
            assert(allocations == 1);
            files = cached_files;
            files.erase(retained_path);
            const auto retained_temporary = retained_path.substr(0, retained_path.size() - 4) + ".ktm";
            files[retained_temporary] = {retained_bytes.begin(), retained_bytes.begin() + retained_bytes.size() / 2};
            preparation = prepare_runtime::createPrepareOwner(prepare_request, media, prepare_public, proof_hash);
            assert(preparation && preparation->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
            work = prepare_runtime::createPrepareWork(preparation, session, 360);
            assert(work);
            work_result = Status::Busy;
            for (unsigned attempt = 0; attempt < 32 && work_result == Status::Busy; ++attempt) work_result = work->advance(persistence_author, output, sizeof(output));
            assert(work_result == Status::Ok && work->ready() && !work->holdsStages() && allocations == 2 && files == cached_files && persistence_author.signing_calls == 2);
            prepare_runtime::destroyPrepareWork(work);
            assert(allocations == 1);
            // Request and response deliberately share the command scratch. The
            // runtime must copy request bindings before advancing file readers.
            EnvelopeView direct_request;
            direct_request.session = session;
            direct_request.body = output;
            direct_request.code = static_cast<uint16_t>(Operation::TrackPrepare);
            bool direct_ready = false;
            for (unsigned attempt = 0; attempt < 32 && !direct_ready; ++attempt)
            {
                geocaching::protocol::CmpWriter request_writer(output, sizeof(output));
                assert(request_writer.array(5) && request_writer.binary(prepare_request.operation) &&
                       request_writer.binary(prepare_request.kml_upload) && request_writer.binary(prepare_request.manifest_upload) &&
                       request_writer.unsignedInteger(prepare_request.generation) && request_writer.binary(prepare_request.expected_parent));
                direct_request.body_size = request_writer.size();
                assert(prepare_runtime::processPrepareRequest(persistence_author, direct_request, 400 + attempt, output, sizeof(output), written) == Status::Ok);
                geocaching::protocol::CmpReader response({output, written});
                size_t fields = 0;
                geocaching::ByteView operation, object, revision_hash;
                uint64_t ready = 0, revision = 0;
                assert(response.array(fields, 5) && fields == 5 && response.binary(operation, 16) && operation.size == 16 &&
                       !std::memcmp(operation.data, prepare_request.operation.data, 16) && response.unsignedInteger(ready) && ready <= 1);
                if (ready)
                {
                    assert(response.binary(object, 32) && object.size == 32 && response.binary(revision_hash, 32) && revision_hash.size == 32 &&
                           response.unsignedInteger(revision) && revision == 2 && response.finished());
                    assert(!std::memcmp(object.data, first_head.object, 32) && !std::memcmp(revision_hash.data, prepare_runtime::work->result()->hash, 32));
                    direct_ready = true;
                }
                else assert(response.nil() && response.nil() && response.unsignedInteger(revision) && revision == 0 && response.finished());
            }
            assert(direct_ready && files == cached_files && persistence_author.signing_calls == 2 && allocations == 2);
            auto encode_direct_request = [&](uint32_t generation)
            {
                geocaching::protocol::CmpWriter writer(output, sizeof(output));
                assert(writer.array(5) && writer.binary(prepare_request.operation) && writer.binary(prepare_request.kml_upload) &&
                       writer.binary(prepare_request.manifest_upload) && writer.unsignedInteger(generation) && writer.binary(prepare_request.expected_parent));
                direct_request.body_size = writer.size();
            };
            auto* retained_work = prepare_runtime::work;
            encode_direct_request(1);
            direct_request.session = wrong_session;
            assert(prepare_runtime::processPrepareRequest(persistence_author, direct_request, 450, output, sizeof(output), written) == Status::Conflict && written == 0);
            assert(prepare_runtime::work == retained_work && files == cached_files && persistence_author.signing_calls == 2);
            direct_request.session = session;
            encode_direct_request(2);
            assert(prepare_runtime::processPrepareRequest(persistence_author, direct_request, 451, output, sizeof(output), written) == Status::StaleIdentity && written == 0);
            assert(prepare_runtime::work == retained_work && files == cached_files && persistence_author.signing_calls == 2);
            encode_direct_request(1);
            persistence_author.read_failure = chat::DeviceAuthorResult::Unavailable;
            assert(prepare_runtime::processPrepareRequest(persistence_author, direct_request, 452, output, sizeof(output), written) == Status::Busy && written == 0);
            persistence_author.read_failure = chat::DeviceAuthorResult::Ok;
            assert(prepare_runtime::work == retained_work && files == cached_files && persistence_author.signing_calls == 2);
            encode_direct_request(1);
            assert(prepare_runtime::processPrepareRequest(persistence_author, direct_request, 453, output, sizeof(output), written) == Status::Ok && written > 0);
            assert(prepare_runtime::work == retained_work && files == cached_files && persistence_author.signing_calls == 2 && allocations == 2);
            const auto publication_head = *prepare_runtime::work->result();
            prepare_runtime::closePrepareSession(session);
            assert(!prepare_runtime::work && allocations == 1);
            {
                namespace publication = hostlink::management::publication_runtime;
                track_sharing::PublicationDirectory directory;
                directory.delivery[0] = 8;
                directory.public_key[0] = 16;
                directory.epoch[0] = 9;
                uint8_t directory_secret[64], directory_seed[32] = {19};
                ed25519_create_keypair(directory.public_key + 32, directory_secret, directory_seed);
                // IP confirmation also authenticates the LXMF delivery address.
                uint8_t directory_hash[32], service_hash[32], delivery_hash[32], delivery_input[26];
                constexpr char delivery_service[] = "lxmf.delivery";
                assert(owner->hash.start() && owner->hash.update(directory.public_key, 64) && owner->hash.finish(directory_hash));
                assert(owner->hash.start() && owner->hash.update(reinterpret_cast<const uint8_t*>(delivery_service), sizeof(delivery_service) - 1) && owner->hash.finish(service_hash));
                std::memcpy(delivery_input, service_hash, 10);
                std::memcpy(delivery_input + 10, directory_hash, 16);
                assert(owner->hash.start() && owner->hash.update(delivery_input, sizeof(delivery_input)) && owner->hash.finish(delivery_hash));
                std::memcpy(directory.delivery, delivery_hash, 16);
                directory.file_limit = 8388608;
                directory.chunk_limit = 1024;
                directory.query_limit = 32;
                track_sharing::CommitRequest commit{{publication_head.operation, 16}, {publication_head.object, 32}, {publication_head.hash, 32}, {directory.delivery, 16}, {directory.epoch, 16}, 1, 2};
                allocation_fail = true;
                assert(!publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash) && allocations == 1);
                allocation_fail = false;
                backing_allocation_fail = true;
                assert(!publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash) && allocations == 1);
                backing_allocation_fail = false;
                auto* admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                assert(admission && allocations == 2 && !admission->ready());
                commit.revision = 99;
                directory.public_key[0] ^= 1;
                Status admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                commit.revision = 2;
                directory.public_key[0] ^= 1;
                assert(admitted == Status::Ok && admission->ready() && admission->result()->phase == track_sharing::PublicationPhase::Accepted);
                assert(admission->manifest()->verified && persistence_author.signing_calls == 2 && files.at(retained_path) == retained_bytes);
                assert(heads.load(committed, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready && committed.phase == track_sharing::HeadPhase::Committed && committed.revision == 2);
                track_sharing::PublicationJournal accepted_job = *admission->result(), uploading_job = accepted_job, job_verify;
                const auto accepted_files = files;
                uint8_t local_delivery[16] = {42};
                chat::TrackBindingChange binding_change;
                allocation_fail = true;
                assert(!publication::TrackPublicationExecution::create(admission, persistence_author, local_delivery, binding_change) && admission && binding_change == chat::TrackBindingChange::Busy && allocations == 2);
                allocation_fail = false;
                persistence_author.binding_busy = true;
                assert(!publication::TrackPublicationExecution::create(admission, persistence_author, local_delivery, binding_change) && admission && allocations == 2 && !persistence_author.binding);
                persistence_author.binding_busy = false;
                auto* execution = publication::TrackPublicationExecution::create(admission, persistence_author, local_delivery, binding_change);
                assert(execution && !admission && binding_change == chat::TrackBindingChange::Applied && allocations == 3 && persistence_author.binding);
                persistence_author.send_allowed = false;
                assert(execution->advance(1000) == track_sharing::NetworkSessionResult::Rejected && persistence_author.network_sends == 1);
                const auto rejected_packet = persistence_author.network_wire;
                assert(execution->advance(1001) == track_sharing::NetworkSessionResult::Waiting && persistence_author.network_sends == 1);
                persistence_author.send_allowed = true;
                assert(execution->advance(4000) == track_sharing::NetworkSessionResult::Queued && persistence_author.network_sends == 2 && persistence_author.network_wire == rejected_packet);
                geocaching::protocol::CmpReader begin_packet({persistence_author.network_wire.data(), persistence_author.network_wire.size()});
                size_t begin_fields = 0;
                uint64_t begin_value = 0;
                geocaching::ByteView begin_id;
                assert(begin_packet.array(begin_fields, 6) && begin_fields == 6 && begin_packet.unsignedInteger(begin_value) && begin_value == 1 &&
                       begin_packet.unsignedInteger(begin_value) && begin_value == 0 && begin_packet.unsignedInteger(begin_value) && begin_value == 4 && begin_packet.binary(begin_id, 16));
                uint8_t remote_response[128], message_hash[32]{};
                geocaching::protocol::CmpWriter remote_writer(remote_response, sizeof(remote_response));
                assert(remote_writer.array(6) && remote_writer.unsignedInteger(1) && remote_writer.unsignedInteger(1) && remote_writer.unsignedInteger(4) && remote_writer.binary(begin_id) &&
                       remote_writer.unsignedInteger(200) && remote_writer.array(4) && remote_writer.nil() && remote_writer.unsignedInteger(accepted_job.file_bytes) &&
                       remote_writer.unsignedInteger(1) && remote_writer.unsignedInteger(1024));
                chat::lxmf::CustomDeliveryView completed_file{{directory.delivery, 16}, {local_delivery, 16}, {message_hash, 32}, {remote_response, remote_writer.size()}};
                assert(chat::lxmf::deliverVerifiedTrackForAuthor(persistence_author.binding, false, false, completed_file, persistence_author));
                assert(execution->advance(4001) == track_sharing::NetworkSessionResult::Progress);
                assert(execution->advance(4002) == track_sharing::NetworkSessionResult::Queued && persistence_author.network_sends == 3);
                ++persistence_author.identity_generation;
                assert(execution->advance(4003) == track_sharing::NetworkSessionResult::Conflict && persistence_author.network_sends == 3);
                --persistence_author.identity_generation;
                persistence_author.binding_busy = true;
                assert(execution->advance(4004) == track_sharing::NetworkSessionResult::Waiting && persistence_author.network_sends == 3);
                assert(!publication::TrackPublicationExecution::release(execution) && execution && allocations == 3 && persistence_author.binding);
                persistence_author.binding_busy = false;
                assert(publication::TrackPublicationExecution::release(execution) && !execution && allocations == 1 && !persistence_author.binding);
                files = accepted_files;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Ok);
                execution = publication::TrackPublicationExecution::create(admission, persistence_author, local_delivery, binding_change);
                assert(execution && !admission && execution->advance(5000) == track_sharing::NetworkSessionResult::Queued);
                const auto before_cancel = files;
                track_sharing::PublicationJournal cancelled_job;
                persistence_author.binding_busy = true;
                assert(execution->cancel(commit.operation, commit.generation, cancelled_job) == Status::Busy && files == before_cancel);
                persistence_author.binding_busy = false;
                ++persistence_author.identity_generation;
                assert(execution->cancel(commit.operation, commit.generation, cancelled_job) == Status::StaleIdentity && files == before_cancel);
                --persistence_author.identity_generation;
                uint8_t wrong_cancel[16];
                std::memcpy(wrong_cancel, commit.operation.data, 16);
                wrong_cancel[0] ^= 1;
                assert(execution->cancel({wrong_cancel, 16}, commit.generation, cancelled_job) == Status::Conflict && files == before_cancel);
                assert(execution->cancel(commit.operation, commit.generation, cancelled_job) == Status::Ok && cancelled_job.phase == track_sharing::PublicationPhase::Cancelled);
                const auto sends_after_cancel = persistence_author.network_sends;
                assert(execution->advance(5001) == track_sharing::NetworkSessionResult::Paused && persistence_author.network_sends == sends_after_cancel);
                assert(publication::TrackPublicationExecution::release(execution) && !execution && allocations == 1 && !persistence_author.binding);
                files = accepted_files;
                publication::destroyPublicationAdmission(admission);
                assert(allocations == 1);
                char job_a[160], job_b[160];
                assert(track_sharing::resolveObjectPath(commit.object, track_sharing::ObjectFile::PublicationA, commit.operation, job_a, sizeof(job_a)));
                assert(track_sharing::resolveObjectPath(commit.object, track_sharing::ObjectFile::PublicationB, commit.operation, job_b, sizeof(job_b)));
                track_sharing::PublicationJournalStore<prepare_runtime::sd::SdBorrowedRecordFileIo> running_jobs(owner->io, job_a, job_b);
                ++uploading_job.sequence;
                uploading_job.phase = track_sharing::PublicationPhase::Uploading;
                uploading_job.offset = 32;
                assert(running_jobs.advance(accepted_job, uploading_job, job_verify, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready);
                const auto before_newer_head = files;
                assert(heads.load(committed, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready);
                auto newer_head = committed;
                ++newer_head.sequence;
                ++newer_head.revision;
                newer_head.phase = track_sharing::HeadPhase::Signed;
                std::memcpy(newer_head.parent, committed.hash, 32);
                newer_head.hash[0] ^= 1;
                newer_head.operation[0] ^= 1;
                assert(heads.advance(committed, newer_head, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready);
                auto admitted_files = files;
                auto* stale_commit = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                assert(stale_commit && stale_commit->step(persistence_author, output, sizeof(output)) == Status::Conflict && files == admitted_files);
                publication::destroyPublicationAdmission(stale_commit);
                assert(allocations == 1);
                track_sharing::PublicationCandidateReader<prepare_runtime::sd::SdBorrowedRecordFileIo> history_candidate(owner->io);
                track_sharing::PublicationSlotKey history_key;
                std::memcpy(history_key.object, uploading_job.object, 32);
                std::memcpy(history_key.operation, uploading_job.operation, 16);
                assert(history_candidate.load(history_key, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready);
                {
                    struct CandidateScan
                    {
                        track_sharing::PublicationSlotKey key;
                        unsigned steps = 0;
                        publication::HistoryScanResult step(track_sharing::PublicationSlotKey& output)
                        {
                            if (!steps++) return publication::HistoryScanResult::Busy;
                            if (steps > 2) return publication::HistoryScanResult::End;
                            output = key;
                            return publication::HistoryScanResult::Candidate;
                        }
                    } scan{history_key};
                    publication::HistoryQuery<CandidateScan, decltype(history_candidate)> query(scan, history_candidate, commit.operation, commit.generation, media);
                    assert(query.step(persistence_author, output, sizeof(output)) == Status::Busy && !query.result());
                    Status recovered = Status::Busy;
                    for (unsigned attempt = 0; attempt < 64 && recovered == Status::Busy; ++attempt) recovered = query.step(persistence_author, output, sizeof(output));
                    assert(recovered == Status::Ok && query.result() && query.result()->offset == 32 && files == admitted_files);
                    ++persistence_author.identity_generation;
                    assert(query.step(persistence_author, output, sizeof(output)) == Status::StaleIdentity && !query.result());
                    --persistence_author.identity_generation;
                }
                assert(allocations == 1 && files == admitted_files);
                admission = nullptr;
                ++persistence_author.identity_generation;
                assert(publication::beginHistoryAuthentication(persistence_author, *history_candidate.unverifiedJournal(), *history_candidate.unverifiedDirectory(), media, output, sizeof(output), admission) == Status::StaleIdentity && !admission);
                --persistence_author.identity_generation;
                assert(publication::beginHistoryAuthentication(persistence_author, *history_candidate.unverifiedJournal(), *history_candidate.unverifiedDirectory(), media, output, sizeof(output), admission) == Status::Busy && admission);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Ok && admission->ready() && files == admitted_files && admission->result()->phase == track_sharing::PublicationPhase::Uploading && admission->result()->offset == 32);
                assert(admission->readOnly());
                uint8_t history_local[16] = {42};
                chat::TrackBindingChange history_binding;
                auto* history_owner = admission;
                assert(!publication::TrackPublicationExecution::create(admission, persistence_author, history_local, history_binding) && admission == history_owner && files == admitted_files);
                persistence_author.read_failure = chat::DeviceAuthorResult::Unavailable;
                assert(admission->step(persistence_author, output, sizeof(output)) == Status::Busy && !admission->ready() && !admission->result());
                persistence_author.read_failure = chat::DeviceAuthorResult::Ok;
                assert(admission->step(persistence_author, output, sizeof(output)) == Status::Ok && admission->ready());
                ++persistence_author.identity_generation;
                assert(admission->step(persistence_author, output, sizeof(output)) == Status::StaleIdentity && !admission->ready() && files == admitted_files);
                --persistence_author.identity_generation;
                publication::destroyPublicationAdmission(admission);
                assert(allocations == 1);
                files = before_newer_head;
                admitted_files = files;
                assert(running_jobs.load(uploading_job, job_verify, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready);
                auto stored_remote = uploading_job;
                ++stored_remote.sequence;
                stored_remote.offset = stored_remote.file_bytes;
                stored_remote.phase = track_sharing::PublicationPhase::FileStored;
                assert(running_jobs.advance(uploading_job, stored_remote, job_verify, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready);
                uploading_job = stored_remote;
                ++stored_remote.sequence;
                stored_remote.phase = track_sharing::PublicationPhase::Publishing;
                assert(running_jobs.advance(uploading_job, stored_remote, job_verify, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready);
                uint8_t receipt_raw[512], receipt_signature[64];
                geocaching::protocol::CmpWriter receipt_writer(receipt_raw, sizeof(receipt_raw));
                assert(receipt_writer.array(13) && receipt_writer.unsignedInteger(1) && receipt_writer.binary({stored_remote.operation, 16}) &&
                       receipt_writer.binary({stored_remote.object, 32}) && receipt_writer.binary({stored_remote.revision_hash, 32}) &&
                       receipt_writer.unsignedInteger(stored_remote.revision) && receipt_writer.unsignedInteger(0) && receipt_writer.binary({stored_remote.file_hash, 32}) &&
                       receipt_writer.unsignedInteger(stored_remote.file_bytes) && receipt_writer.binary({directory.public_key, 64}) &&
                       receipt_writer.binary({directory.delivery, 16}) && receipt_writer.binary({directory.epoch, 16}) &&
                       receipt_writer.unsignedInteger(1) && receipt_writer.unsignedInteger(5000000000ULL));
                constexpr char receipt_domain[] = "TrailMate.Track.Receipt.v1";
                uint8_t receipt_signing[sizeof(receipt_domain) + 512];
                std::memcpy(receipt_signing, receipt_domain, sizeof(receipt_domain));
                std::memcpy(receipt_signing + sizeof(receipt_domain), receipt_raw, receipt_writer.size());
                ed25519_sign(receipt_signature, receipt_signing, sizeof(receipt_domain) + receipt_writer.size(), directory.public_key + 32, directory_secret);
                char receipt_temporary[160], receipt_path[160];
                assert(track_sharing::resolveObjectPath(commit.object, track_sharing::ObjectFile::ReceiptTemporary, commit.operation, receipt_temporary, sizeof(receipt_temporary)));
                assert(track_sharing::resolveObjectPath(commit.object, track_sharing::ObjectFile::Receipt, commit.operation, receipt_path, sizeof(receipt_path)));
                track_sharing::SignedManifestStore<prepare_runtime::sd::SdBorrowedRecordFileIo> receipt_store(owner->io, receipt_temporary, receipt_path);
                assert(receipt_store.persist({receipt_raw, receipt_writer.size()}, {receipt_signature, 64}, output, sizeof(output)) == track_sharing::SignedStoreResult::Ready);
                uploading_job = stored_remote;
                ++stored_remote.sequence;
                stored_remote.phase = track_sharing::PublicationPhase::Confirmed;
                assert(running_jobs.advance(uploading_job, stored_remote, job_verify, output, sizeof(output)) == track_sharing::PublicationStoreResult::Ready);
                const auto confirmed_remote = files;
                {
                    namespace confirmation = hostlink::management::ip_confirmation_runtime;
                    char signed_path[160], object_path[160];
                    assert(track_sharing::resolveObjectPath(commit.object, track_sharing::ObjectFile::SignedManifest, commit.revision_hash, signed_path, sizeof(signed_path)));
                    assert(track_sharing::resolveObjectPath(commit.object, track_sharing::ObjectFile::Directory, {}, object_path, sizeof(object_path)));
                    const auto signed_bytes = files.at(signed_path), receipt_bytes = files.at(receipt_path);
                    geocaching::protocol::CmpReader signed_reader({signed_bytes.data(), signed_bytes.size()});
                    size_t fields = 0;
                    geocaching::ByteView original_raw, original_signature;
                    assert(signed_reader.array(fields, 2) && fields == 2 && signed_reader.binary(original_raw, 4096) && signed_reader.binary(original_signature, 64) && signed_reader.finished());
                    uint8_t imported_id[16] = {44}, imported_operation[16] = {45}, receipt_hash[32];
                    assert(owner->hash.start() && owner->hash.update(receipt_bytes.data(), receipt_bytes.size()) && owner->hash.finish(receipt_hash));
                    // Replacement device has the SAME author, fresh session epoch,
                    // uploaded evidence, and no old local object files or keys.
                    for (auto it = files.begin(); it != files.end();)
                    {
                        if (it->first.rfind(std::string(object_path) + "/", 0) == 0) it = files.erase(it);
                        else ++it;
                    }
                    persistence_author.identity_generation = 2;
                    auto imported_manifest = prepare_manifest;
                    imported_manifest.generation = 2;
                    track_sharing::UploadJournal imported_receipt = imported_manifest;
                    std::memcpy(imported_receipt.id, imported_id, 16);
                    std::memcpy(imported_receipt.file_hash, receipt_hash, 32);
                    imported_receipt.total = imported_receipt.durable_offset = uint32_t(receipt_bytes.size());
                    for (const auto* item : {&imported_manifest, &imported_receipt})
                    {
                        const auto kind = item == &imported_manifest ? UploadKind::Manifest : UploadKind::IpReceipt;
                        char path[64];
                        assert(track_sharing::resolveUploadPath(item->id, 16, track_sharing::UploadFile::Candidate, path, sizeof(path), kind));
                        files[path] = item == &imported_manifest ? std::vector<uint8_t>(original_raw.data, original_raw.data + original_raw.size) : receipt_bytes;
                        assert(track_sharing::resolveUploadPath(item->id, 16, track_sharing::UploadFile::JournalA, path, sizeof(path), kind));
                        assert(track_sharing::encodeUploadJournal(*item, output, sizeof(output)));
                        files[path] = {output, output + track_sharing::kUploadJournalBytes};
                        assert(track_sharing::resolveUploadPath(item->id, 16, track_sharing::UploadFile::JournalB, path, sizeof(path), kind));
                        files.erase(path);
                    }
                    uint8_t wire[416];
                    geocaching::protocol::CmpWriter writer(wire, sizeof(wire));
                    assert(writer.array(14) && writer.binary({imported_operation, 16}) && writer.binary({imported_id, 16}) && writer.binary({receipt_hash, 32}) &&
                           writer.unsignedInteger(2) && writer.binary({directory.public_key, 64}) && writer.binary({directory.delivery, 16}) && writer.binary({directory.epoch, 16}) &&
                           writer.binary(commit.operation) && writer.binary(commit.object) && writer.binary(commit.revision_hash) && writer.unsignedInteger(2) &&
                           writer.binary({imported_manifest.id, 16}) && writer.binary({imported_manifest.file_hash, 32}) && writer.binary(original_signature));
                    EnvelopeView envelope{session, session, uint16_t(Operation::TrackImportHistory), wire, writer.size()};
                    const auto uploaded = files;
                    uint64_t import_clock = 1000;
                    const auto poll = [&]()
                    {
                        size_t written = 0;
                        Status status = Status::Busy;
                        for (unsigned attempt = 0; attempt < 64; ++attempt)
                        {
                            status = confirmation::processConfirmation(persistence_author, envelope, import_clock++, output, sizeof(output), written);
                            if (status != Status::Busy && (status != Status::Ok || (confirmation::controller->work && confirmation::controller->work->result()))) return status;
                        }
                        return Status::Busy;
                    };
                    files[std::string(object_path) + "/unknown.bin"] = {1};
                    const auto unknown = files;
                    assert(poll() == Status::Conflict && files == unknown);
                    confirmation::release();
                    files = uploaded;
                    wire[writer.size() - 1] ^= 1;
                    assert(poll() == Status::Conflict && files == uploaded);
                    confirmation::release();
                    wire[writer.size() - 1] ^= 1;
                    ++persistence_author.identity_generation;
                    assert(poll() == Status::StaleIdentity && files == uploaded);
                    confirmation::release();
                    --persistence_author.identity_generation;
                    const auto imported_result = poll();
                    assert(imported_result == Status::Ok && confirmation::controller->work->result()->revision == 2 && confirmation::controller->work->result()->generation == 2);
                    const auto adopted = files;
                    confirmation::release(); // Lost reply/session: exact import recovers without signing or network sends.
                    assert(poll() == Status::Ok && files == adopted);
                    // Actual browser retries reupload proof before querying the
                    // completed controller. The sealed upload slot must not
                    // survive a matching cached confirmation and block cleanup.
                    for (const auto* item : {&imported_receipt, &imported_manifest})
                    {
                        const auto kind = item == &imported_receipt ? UploadKind::IpReceipt : UploadKind::Manifest;
                        upload::owner = upload::createOwner(item->id, session, media, 2, item->author_hash, import_clock, kind);
                        assert(upload::owner && upload::owner->resources.recover(output, sizeof(output)) == track_sharing::JournalStoreResult::Ready);
                        assert(poll() == Status::Ok && !upload::owner && files == adopted);
                    }
                    for (unsigned fault = 0; fault < 7; ++fault)
                    {
                        const uint8_t other_session[16] = {71};
                        upload::owner = upload::createOwner(imported_receipt.id, fault == 6 ? other_session : session,
                                                            media, 2, imported_receipt.author_hash, import_clock, UploadKind::IpReceipt);
                        assert(upload::owner && upload::owner->resources.recover(output, sizeof(output)) == track_sharing::JournalStoreResult::Ready);
                        // Fault injection into the actual transient owner: a
                        // cached success must not discard any mismatched or
                        // still-active writer, even if its proof ID resembles ours.
                        auto& metadata = const_cast<track_sharing::UploadJournal&>(upload::owner->resources.metadata());
                        if (fault == 0) metadata.id[0] ^= 1;
                        if (fault == 1) metadata.file_hash[0] ^= 1;
                        if (fault == 2) metadata.author_hash[0] ^= 1;
                        if (fault == 3) ++metadata.generation;
                        if (fault == 4) metadata.phase = track_sharing::UploadPhase::Receiving;
                        if (fault == 5) upload::owner->discarding = true;
                        assert(poll() == Status::Ok && upload::owner && files == adopted);
                        upload::destroyOwner(upload::owner);
                        upload::owner = nullptr;
                    }
                    assert(heads.load(committed, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready && committed.phase == track_sharing::HeadPhase::Committed && committed.sequence == 1);
                    confirmation::release();
                    // The same signed content may acquire its directory receipt
                    // after an identity restore. Keep the original signing op.
                    files = uploaded;
                    for (const auto& item : cached_files)
                        if (item.first.rfind(std::string(object_path) + "/", 0) == 0) files[item.first] = item.second;
                    track_sharing::ObjectHead old_signed, old_verify;
                    assert(heads.load(old_signed, old_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready &&
                           old_signed.phase == track_sharing::HeadPhase::Signed && old_signed.generation == 1 && old_signed.revision == 2);
                    const auto pending_signed = files;
                    assert(poll() == Status::Ok);
                    assert(heads.load(committed, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready &&
                           committed.phase == track_sharing::HeadPhase::Committed && committed.generation == 2 &&
                           committed.sequence == old_signed.sequence + 2 && !std::memcmp(committed.operation, old_signed.operation, 16));
                    const auto resumed_signed = files;
                    confirmation::release();
                    assert(poll() == Status::Ok && files == resumed_signed);
                    confirmation::release();
                    // Model restart between the durable commit and generation
                    // adoption: retry must finish the second journal transition.
                    files = pending_signed;
                    auto intermediate = old_signed;
                    ++intermediate.sequence;
                    intermediate.phase = track_sharing::HeadPhase::Committed;
                    assert(heads.advance(old_signed, intermediate, old_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready);
                    assert(poll() == Status::Ok && files == resumed_signed);
                    confirmation::release();
                    // A valid receipt for another hash cannot replace an
                    // unfinished local version, even with the same author.
                    files = pending_signed;
                    auto conflicting_signed = old_signed;
                    conflicting_signed.hash[0] ^= 1;
                    assert(track_sharing::encodeObjectHead(conflicting_signed, output, sizeof(output)));
                    files[old_signed.sequence & 1 ? head_a : head_b] = {output, output + track_sharing::kObjectHeadBytes};
                    const auto signed_conflict = files;
                    assert(poll() == Status::Conflict && files == signed_conflict);
                    confirmation::release();
                    files = adopted;
                    assert(poll() == Status::Ok);
                    {
                        // Restored history must actually permit the next device
                        // signature, not just display recovered metadata.
                        uint8_t next_raw[1024], next_kml_id[16] = {48}, next_manifest_id[16] = {49}, next_operation[16] = {50};
                        geocaching::protocol::CmpWriter next_writer(next_raw, sizeof(next_raw));
                        assert(next_writer.array(14) && next_writer.unsignedInteger(1) && next_writer.binary({prepare_public, 64}) && next_writer.binary({id, 16}) &&
                               next_writer.unsignedInteger(0) && next_writer.unsignedInteger(3) && next_writer.binary(commit.revision_hash) && next_writer.unsignedInteger(0) &&
                               next_writer.unsignedInteger(1) && next_writer.unsignedInteger(3) && next_writer.text("Restored author update") && next_writer.text(""));
                        assert(next_writer.array(4) && next_writer.unsignedInteger(10000000) && next_writer.unsignedInteger(20000000) && next_writer.unsignedInteger(10000000) && next_writer.unsignedInteger(20000000));
                        assert(next_writer.array(4) && next_writer.unsignedInteger(0) && next_writer.nil() && next_writer.nil() && next_writer.unsignedInteger(1));
                        assert(next_writer.array(4) && next_writer.binary({xml_hash, 32}) && next_writer.unsignedInteger(xml.size()) && next_writer.text("application/vnd.google-earth.kml+xml") && next_writer.unsignedInteger(1));
                        auto next_kml = prepare_kml, next_manifest = imported_manifest;
                        std::memcpy(next_kml.id, next_kml_id, 16);
                        next_kml.generation = 2;
                        std::memcpy(next_manifest.id, next_manifest_id, 16);
                        next_manifest.total = next_manifest.durable_offset = uint32_t(next_writer.size());
                        assert(owner->hash.start() && owner->hash.update(next_raw, next_writer.size()) && owner->hash.finish(next_manifest.file_hash));
                        for (const auto* item : {&next_kml, &next_manifest})
                        {
                            const auto kind = item == &next_kml ? UploadKind::Kml : UploadKind::Manifest;
                            char path[64];
                            assert(track_sharing::resolveUploadPath(item->id, 16, track_sharing::UploadFile::Candidate, path, sizeof(path), kind));
                            files[path] = item == &next_kml ? std::vector<uint8_t>(xml.begin(), xml.end()) : std::vector<uint8_t>(next_raw, next_raw + next_writer.size());
                            assert(track_sharing::resolveUploadPath(item->id, 16, track_sharing::UploadFile::JournalA, path, sizeof(path), kind));
                            assert(track_sharing::encodeUploadJournal(*item, output, sizeof(output)));
                            files[path] = {output, output + track_sharing::kUploadJournalBytes};
                        }
                        track_sharing::PrepareRequest next_request{{next_operation, 16}, {next_kml_id, 16}, {next_manifest_id, 16}, commit.revision_hash, 2};
                        auto* next_input = prepare_runtime::createPrepareOwner(next_request, media, prepare_public, proof_hash);
                        assert(next_input && next_input->start(output, sizeof(output)) == track_sharing::ManifestFileResult::Pending);
                        auto checked = track_sharing::ManifestFileResult::Pending;
                        for (unsigned attempt = 0; attempt < 64 && checked == track_sharing::ManifestFileResult::Pending; ++attempt) checked = next_input->step(2, proof_hash, output, sizeof(output));
                        assert(checked == track_sharing::ManifestFileResult::Ready);
                        auto* next_persistence = prepare_runtime::createPreparePersistence(*next_input);
                        assert(next_persistence);
                        PersistenceAuthor restored_author;
                        restored_author.identity_generation = 2;
                        Status next_status = Status::Busy;
                        for (unsigned attempt = 0; attempt < 64 && next_status == Status::Busy; ++attempt) next_status = next_persistence->finish(restored_author, nullptr, output, sizeof(output));
                        assert(next_status == Status::Ok && next_persistence->head().revision == 3 && next_persistence->head().generation == 2 &&
                               !std::memcmp(next_persistence->head().object, commit.object.data, 32) && !std::memcmp(next_persistence->head().parent, commit.revision_hash.data, 32) && restored_author.signing_calls == 1);
                        prepare_runtime::destroyPreparePersistence(next_persistence);
                        prepare_runtime::destroyPrepareOwner(next_input);
                        // A completed response cache cannot authorize the old
                        // version after this object advances to its successor.
                        assert(poll() == Status::Conflict);
                        confirmation::release();
                        files = adopted;
                        assert(heads.load(committed, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready);
                    }
                    auto foreign_head = committed;
                    foreign_head.hash[0] ^= 1;
                    assert(track_sharing::encodeObjectHead(foreign_head, output, sizeof(output)));
                    files[head_a] = {output, output + track_sharing::kObjectHeadBytes};
                    const auto conflicting = files;
                    assert(poll() == Status::Conflict && files == conflicting);
                    confirmation::release();
                    files = adopted;
                    files[head_a][0] ^= 1;
                    const auto corrupt = files;
                    assert(poll() == Status::Conflict && files == corrupt);
                    confirmation::release();
                    persistence_author.identity_generation = 1;
                    files = confirmed_remote;
                    assert(allocations == 1 && persistence_author.signing_calls == 2);
                }
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Ok && admission->ready() && admission->result()->phase == track_sharing::PublicationPhase::Confirmed && admission->manifest()->verified && files == confirmed_remote);
                publication::destroyPublicationAdmission(admission);
                assert(allocations == 1);
                files.erase(receipt_path);
                const auto receipt_missing = files;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Conflict && !admission->ready() && !admission->result() && files == receipt_missing);
                publication::destroyPublicationAdmission(admission);
                files = confirmed_remote;
                files[receipt_path].back() ^= 1;
                const auto receipt_damaged = files;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Conflict && !admission->ready() && files == receipt_damaged);
                publication::destroyPublicationAdmission(admission);
                files = admitted_files;
                directory.public_key[0] ^= 1;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Conflict && !admission->ready() && files == admitted_files);
                publication::destroyPublicationAdmission(admission);
                directory.public_key[0] ^= 1;
                files[retained_path][0] ^= 1;
                const auto damaged_file = files;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::HashMismatch && !admission->ready() && files == damaged_file);
                publication::destroyPublicationAdmission(admission);
                files = admitted_files;
                char job_path[160];
                assert(track_sharing::resolveObjectPath(commit.object, track_sharing::ObjectFile::PublicationA, commit.operation, job_path, sizeof(job_path)));
                files.erase(job_path);
                const auto orphan_head = files;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Conflict && !admission->ready() && files == orphan_head);
                publication::destroyPublicationAdmission(admission);
                files = cached_files;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                assert(admission->step(persistence_author, output, sizeof(output)) == Status::Busy);
                ++media;
                assert(admission->step(persistence_author, output, sizeof(output)) == Status::StorageUnavailable && !admission->ready());
                --media;
                publication::destroyPublicationAdmission(admission);
                assert(allocations == 1 && files == cached_files);
                flush_failure = true;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::StorageUnavailable && !admission->ready());
                publication::destroyPublicationAdmission(admission);
                assert(heads.load(committed, head_verify, output, sizeof(output)) == track_sharing::HeadStoreResult::Ready && committed.phase == track_sharing::HeadPhase::Signed && committed.revision == 2);
                flush_failure = false;
                admission = publication::createPublicationAdmission(commit, directory, media, prepare_public, proof_hash);
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt) admitted = admission->step(persistence_author, output, sizeof(output));
                assert(admitted == Status::Ok && admission->ready() && admission->result()->phase == track_sharing::PublicationPhase::Accepted);
                publication::destroyPublicationAdmission(admission);
                assert(allocations == 1);
                files = cached_files;
                uint8_t commit_body[160];
                geocaching::protocol::CmpWriter commit_writer(commit_body, sizeof(commit_body));
                assert(commit_writer.array(7) && commit_writer.binary(commit.operation) && commit_writer.binary(commit.object) &&
                       commit_writer.binary(commit.revision_hash) && commit_writer.unsignedInteger(1) && commit_writer.unsignedInteger(2) &&
                       commit_writer.binary(commit.directory) && commit_writer.binary(commit.directory_epoch));
                EnvelopeView commit_envelope;
                commit_envelope.code = uint16_t(Operation::TrackCommit);
                commit_envelope.session = session;
                commit_envelope.body = commit_body;
                commit_envelope.body_size = commit_writer.size();
                publication::TrackPublicationRequestWork* commit_work = nullptr;
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt)
                {
                    admitted = publication::processPublicationRequest(persistence_author, commit_work, commit_envelope, directory, 500 + attempt, output, sizeof(output), written);
                    assert(commit_work);
                    if (admitted == Status::Busy) assert(!written && commit_work->holdsAdmission() && allocations == 3);
                }
                assert(admitted == Status::Ok && written && commit_work->result() && !commit_work->holdsAdmission() && allocations == 2);
                geocaching::protocol::CmpReader admission_reply({output, written});
                size_t fields = 0;
                uint64_t value = 0;
                geocaching::ByteView field;
                assert(admission_reply.array(fields, 8) && fields == 8 && admission_reply.binary(field, 16) && !std::memcmp(field.data, publication_head.operation, 16) &&
                       admission_reply.binary(field, 32) && !std::memcmp(field.data, publication_head.object, 32) &&
                       admission_reply.binary(field, 32) && !std::memcmp(field.data, publication_head.hash, 32) &&
                       admission_reply.unsignedInteger(value) && value == 1 && admission_reply.unsignedInteger(value) && value == 2 &&
                       admission_reply.binary(field, 16) && !std::memcmp(field.data, directory.delivery, 16) &&
                       admission_reply.binary(field, 16) && !std::memcmp(field.data, directory.epoch, 16) &&
                       admission_reply.unsignedInteger(value) && value == 1 && admission_reply.finished());
                const auto committed_files = files;
                uint8_t unrelated_session[16] = {22};
                commit_envelope.session = unrelated_session;
                auto* retained_commit = commit_work;
                assert(publication::processPublicationRequest(persistence_author, commit_work, commit_envelope, directory, 600, output, sizeof(output), written) == Status::Conflict && !written && commit_work == retained_commit && files == committed_files);
                commit_envelope.session = session;
                persistence_author.read_failure = chat::DeviceAuthorResult::Unavailable;
                assert(publication::processPublicationRequest(persistence_author, commit_work, commit_envelope, directory, 601, output, sizeof(output), written) == Status::Busy && !written && !commit_work->result());
                persistence_author.read_failure = chat::DeviceAuthorResult::Ok;
                assert(publication::processPublicationRequest(persistence_author, commit_work, commit_envelope, directory, 602, output, sizeof(output), written) == Status::Ok && written && commit_work->result() && files == committed_files);
                assert(publication::advancePublicationRequest(commit_work, nullptr, output, sizeof(output), 603, true) == Status::Busy && !commit_work->result());
                assert(publication::advancePublicationRequest(commit_work, &persistence_author, output, sizeof(output), 604, true) == Status::Ok && commit_work->result());
                assert(publication::advancePublicationRequest(commit_work, &persistence_author, output, sizeof(output), 605, false) == Status::Expired && !commit_work && allocations == 1 && files == committed_files);
                commit_envelope.session = unrelated_session;
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt)
                    admitted = publication::processPublicationRequest(persistence_author, commit_work, commit_envelope, directory, 700 + attempt, output, sizeof(output), written);
                assert(admitted == Status::Ok && written && !commit_work->holdsAdmission() && files == committed_files);
                assert(publication::advancePublicationRequest(commit_work, &persistence_author, output, sizeof(output), 99999, true) == Status::Expired && !commit_work && allocations == 1 && files == committed_files);
                files = cached_files;
                assert(publication::processPublicationRequest(persistence_author, commit_work, commit_envelope, directory, 100000, output, sizeof(output), written) == Status::Busy && !written && commit_work->holdsAdmission() && allocations == 3);
                assert(publication::advancePublicationRequest(commit_work, &persistence_author, output, sizeof(output), 100001, false) == Status::Expired && !commit_work && allocations == 1 && files == cached_files);
                uint8_t directory_request_body[2] = {0x91, 0};
                EnvelopeView directory_request;
                directory_request.code = uint16_t(Operation::TrackDirectoryList);
                directory_request.session = session;
                directory_request.body = directory_request_body;
                directory_request.body_size = sizeof(directory_request_body);
                assert(directory_runtime::list(persistence_author, directory_request, 110000, output, sizeof(output), written) == Status::Ok && directory_runtime::owner);
                uint8_t directory_metadata[128], discovery_hash[16] = {55}, capabilities_response[256], capability_id[16] = {56};
                geocaching::protocol::CmpWriter advertised(directory_metadata, sizeof(directory_metadata));
                assert(advertised.array(5) && advertised.unsignedInteger(1) && advertised.text(track_sharing::kApplicationType) && advertised.binary({directory.delivery, 16}) &&
                       advertised.binary({directory.epoch, 16}) && advertised.text("Directory"));
                chat::lxmf::TrackDirectoryAnnouncementView advertised_view{{discovery_hash, 16}, {directory.delivery, 16}, {directory.public_key, 64}, {directory_metadata, advertised.size()}};
                assert(directory_runtime::owner->pool.observe(advertised_view, 110000));
                geocaching::protocol::CmpWriter caps(capabilities_response, sizeof(capabilities_response));
                assert(caps.array(6) && caps.unsignedInteger(1) && caps.unsignedInteger(1) && caps.unsignedInteger(0) && caps.binary({capability_id, 16}) && caps.unsignedInteger(200) &&
                       caps.array(8) && caps.unsignedInteger(1) && caps.text(track_sharing::kApplicationType) && caps.binary({directory.public_key, 64}) && caps.binary({directory.epoch, 16}) &&
                       caps.unsignedInteger(8388608) && caps.unsignedInteger(1024) && caps.unsignedInteger(32) && caps.array(9));
                for (unsigned op = 0; op < 9; ++op) assert(caps.unsignedInteger(op));
                assert(directory_runtime::owner->pool.confirmCapabilities({directory.delivery, 16}, {directory.epoch, 16}, {capabilities_response, caps.size()}, {capability_id, 16}));
                commit_envelope.session = session;
                admitted = Status::Busy;
                for (unsigned attempt = 0; attempt < 64 && admitted == Status::Busy; ++attempt)
                    admitted = publication::processCommit(persistence_author, commit_envelope, 110001 + attempt, output, sizeof(output), written);
                assert(admitted == Status::Ok && written && publication::controller && publication::controller->execution && !directory_runtime::owner);
                assert(publication::processCommit(persistence_author, commit_envelope, 110090, output, sizeof(output), written) == Status::Ok && written);
                const auto& active_job = publication::controller->latest;
                assert(publication::getStatus(persistence_author, {active_job.operation, 16}, active_job.generation, output, sizeof(output), written) == Status::Ok && written && output[0] == 0x9e);
                assert(publication::getStatus(persistence_author, {active_job.operation, 16}, active_job.generation + 1, output, sizeof(output), written) == Status::StaleIdentity && !written);
                commit_body[3] ^= 1;
                assert(publication::processCommit(persistence_author, commit_envelope, 110091, output, sizeof(output), written) == Status::Conflict && !written);
                commit_body[3] ^= 1;
                const auto sends_before_disconnect = persistence_author.network_sends;
                uint8_t other_session[16]{};
                std::memcpy(other_session, session, 16);
                other_session[0] ^= 1;
                publication::closeSession(other_session);
                assert(publication::controller->request && publication::controller->execution);
                publication::closeSession(session);
                assert(publication::controller && publication::controller->execution && !publication::controller->request);
                publication::disconnect();
                publication::tick(110100);
                assert(persistence_author.network_sends == sends_before_disconnect + 1);
                assert(publication::cancelStatus(persistence_author, {active_job.operation, 16}, active_job.generation + 1, output, sizeof(output), written) == Status::StaleIdentity && !written);
                persistence_author.binding_busy = true;
                assert(publication::cancelStatus(persistence_author, {active_job.operation, 16}, active_job.generation, output, sizeof(output), written) == Status::Busy && !written);
                persistence_author.binding_busy = false;
                assert(publication::cancelStatus(persistence_author, {active_job.operation, 16}, active_job.generation, output, sizeof(output), written) == Status::Ok && written && publication::controller->retiring && active_job.phase == track_sharing::PublicationPhase::Cancelled);
                const auto cancelled_sends = persistence_author.network_sends;
                publication::tick(110101);
                assert(!publication::controller->execution && persistence_author.network_sends == cancelled_sends && !persistence_author.binding);
                assert(publication::cancelStatus(persistence_author, {active_job.operation, 16}, active_job.generation, output, sizeof(output), written) == Status::Ok && written);
                assert(publication::destroyController() && !publication::controller && !persistence_author.binding && allocations == 1);
            }
            files = original_files;
        }
        assert(owner->resources.metadata().phase == track_sharing::UploadPhase::Sealed);
        assert(upload::abortOwnedUpload(*owner, session, manifest_id, 201, output, sizeof(output)) == Status::Ok);
        upload::destroyOwner(owner);
        owner = nullptr;
        assert(allocations == 0 && files == saved_uploads);
        assert(upload::beginNewUpload(owner, id, session, media, 1, author, sizeof(content), digest, 100, output, sizeof(output)) == Status::Conflict && !owner && allocations == 0 && files == saved_uploads);
        digest[31] ^= 1;
        assert(upload::recoverUpload(owner, id, session, media, 1, author, sizeof(content), digest, 200, output, sizeof(output)) == Status::Conflict && !owner && allocations == 0 && files == saved_uploads);
        digest[31] ^= 1;
        assert(upload::recoverUpload(owner, id, session, media, 2, author, sizeof(content), digest, 200, output, sizeof(output)) == Status::Conflict && !owner && allocations == 0);
        assert(upload::recoverUpload(owner, id, session, media, 1, author, sizeof(content), digest, 200, output, sizeof(output)) == Status::Ok && owner && owner->resources.metadata().phase == track_sharing::UploadPhase::Sealed && files == saved_uploads);
        const std::string candidate = owner->resources.candidatePath();
        upload::destroyOwner(owner);
        owner = nullptr;
        files[candidate].pop_back();
        assert(upload::recoverUpload(owner, id, session, media, 1, author, sizeof(content), digest, 200, output, sizeof(output)) == Status::StorageUnavailable && owner && owner->discarding && allocations == 1);
        assert(upload::abortOwnedUpload(*owner, session, id, 201, output, sizeof(output)) == Status::Ok);
        assert(!files.count(candidate));
        upload::destroyOwner(owner);
        owner = nullptr;
        assert(allocations == 0);
        files = saved_uploads;
        geocaching::protocol::CmpWriter resume_request(output, sizeof(output));
        assert(resume_request.array(4) && resume_request.binary({id, 16}) && resume_request.unsignedInteger(3) && resume_request.unsignedInteger(sizeof(content)) && resume_request.binary({digest, 32}));
        EnvelopeView upload_request;
        upload_request.session = session;
        upload_request.code = static_cast<uint16_t>(Operation::TransferBegin);
        upload_request.body = output;
        upload_request.body_size = resume_request.size();
        assert(upload::processUploadRequest(upload_request, 1, author, 300, output, sizeof(output), written) == Status::Ok && upload::owner && allocations == 1);
        uint8_t unrelated[16] = {9};
        upload::closeUploadSession(unrelated);
        assert(upload::owner && allocations == 1);
        upload::advanceUploads(output, sizeof(output), 301, false);
        assert(!upload::owner && allocations == 0 && files == saved_uploads);
        UploadAuthorAdapter adapter;
        uint8_t new_id[16] = {8};
        geocaching::protocol::CmpWriter auth_begin(output, sizeof(output));
        assert(auth_begin.array(4) && auth_begin.binary({new_id, 16}) && auth_begin.unsignedInteger(3) && auth_begin.unsignedInteger(sizeof(content)) && auth_begin.binary({digest, 32}));
        upload_request.body_size = auth_begin.size();
        assert(upload::routeTrackUpload(upload_request));
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 400, output, sizeof(output), written) == Status::Ok && upload::owner);
        geocaching::protocol::CmpWriter auth_chunk(output, sizeof(output));
        assert(auth_chunk.array(3) && auth_chunk.binary({new_id, 16}) && auth_chunk.unsignedInteger(0) && auth_chunk.binary({content, sizeof(content)}));
        upload_request.code = static_cast<uint16_t>(Operation::TransferChunk);
        upload_request.body_size = auth_chunk.size();
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 401, output, sizeof(output), written) == Status::Ok);
        assert(files[upload::owner->resources.candidatePath()] == std::vector<uint8_t>(content, content + sizeof(content)));
        geocaching::protocol::CmpWriter query_status(output, sizeof(output));
        assert(query_status.array(1) && query_status.binary({new_id, 16}));
        upload_request.code = static_cast<uint16_t>(Operation::TransferStatus);
        upload_request.body_size = query_status.size();
        const auto before_status = files;
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 402, output, sizeof(output), written) == Status::Ok && upload::owner->resources.metadata().phase == track_sharing::UploadPhase::Receiving && !upload::owner->resources.sealingStarted() && files == before_status);
        geocaching::protocol::CmpWriter bad_finish(output, sizeof(output));
        assert(bad_finish.array(1) && bad_finish.binary({new_id, 16}));
        upload_request.code = static_cast<uint16_t>(Operation::TransferFinish);
        upload_request.body_size = bad_finish.size();
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 403, output, sizeof(output), written) == Status::Ok);
        files[upload::owner->resources.candidatePath()][100] ^= 1;
        upload::advanceUploads(output, sizeof(output), 404, true);
        upload::advanceUploads(output, sizeof(output), 405, true);
        assert(upload::owner && upload::owner->resources.metadata().phase == track_sharing::UploadPhase::Failed);
        const auto failed_files = files;
        upload::advanceUploads(output, sizeof(output), 406, false);
        assert(!upload::owner && allocations == 0 && files == failed_files);
        geocaching::protocol::CmpWriter failed_resume(output, sizeof(output));
        assert(failed_resume.array(4) && failed_resume.binary({new_id, 16}) && failed_resume.unsignedInteger(3) && failed_resume.unsignedInteger(sizeof(content)) && failed_resume.binary({digest, 32}));
        upload_request.code = static_cast<uint16_t>(Operation::TransferBegin);
        upload_request.body_size = failed_resume.size();
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 407, output, sizeof(output), written) == Status::HashMismatch && upload::owner && upload::owner->discarding && files == failed_files);
        geocaching::protocol::CmpWriter abort_request(output, sizeof(output));
        assert(abort_request.array(1) && abort_request.binary({new_id, 16}));
        upload_request.code = static_cast<uint16_t>(Operation::TransferAbort);
        upload_request.body_size = abort_request.size();
        remove_fail = true;
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 408, output, sizeof(output), written) == Status::StorageUnavailable && upload::owner && upload::owner->discarding);
        remove_fail = false;
        const std::string abort_candidate = upload::owner->resources.candidatePath();
        char journal_path[64];
        assert(track_sharing::resolveUploadPath(new_id, 16, track_sharing::UploadFile::JournalA, journal_path, sizeof(journal_path)));
        remove_failure_path = journal_path;
        geocaching::protocol::CmpWriter retry_abort(output, sizeof(output));
        assert(retry_abort.array(1) && retry_abort.binary({new_id, 16}));
        upload_request.body_size = retry_abort.size();
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 409, output, sizeof(output), written) == Status::StorageUnavailable && upload::owner && upload::owner->discarding);
        assert(!files.count(abort_candidate) && files.count(journal_path));
        geocaching::protocol::CmpWriter discarded_finish(output, sizeof(output));
        assert(discarded_finish.array(1) && discarded_finish.binary({new_id, 16}));
        upload_request.code = static_cast<uint16_t>(Operation::TransferFinish);
        upload_request.body_size = discarded_finish.size();
        const auto partial_files = files;
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 410, output, sizeof(output), written) == Status::Conflict && files == partial_files);
        remove_failure_path.clear();
        geocaching::protocol::CmpWriter final_abort(output, sizeof(output));
        assert(final_abort.array(1) && final_abort.binary({new_id, 16}));
        upload_request.code = static_cast<uint16_t>(Operation::TransferAbort);
        upload_request.body_size = final_abort.size();
        assert(upload::processAuthenticatedUpload(adapter, upload_request, 411, output, sizeof(output), written) == Status::Ok && written == 1 && output[0] == 0x90);
        assert(!upload::owner && allocations == 0);
        assert(files == saved_uploads);
        files["/trailmate/web-upload/orphan"] = {};
        assert(upload::admitUploadSpace(media, 5, output, sizeof(output)) == Status::LimitExceeded && allocations == 0);
    }
}
