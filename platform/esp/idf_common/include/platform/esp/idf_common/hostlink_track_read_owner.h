#pragma once
#include "chat/ports/i_mesh_adapter.h"
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "platform/esp/common/storage/track_manifest_crypto.h"
#include "track_sharing/publication_directory.h"
#include "track_sharing/read_network_exchange.h"
#include "track_sharing/read_query_binding.h"
#include "track_sharing/read_response_sink.h"
#include "track_sharing/read_response_validator.h"
#include <atomic>
#include <esp_heap_caps.h>
#include <new>

namespace hostlink::management::track_read_runtime
{
namespace sd = platform::esp::arduino_common::storage;
// Scheduler lends exclusive storage and a directory authenticated by discovery.
// All input views are copied before registration. USB disconnect must release
// this owner; Busy removal keeps it alive until callbacks have retired.
class TrackReadOwner
{
  public:
    using Hash = platform::esp::storage::SnapshotStreamSha256;
    using Crypto = platform::esp::storage::TrackManifestCrypto;
    using Store = track_sharing::ReadResponseStore<sd::SdBorrowedRecordFileIo>;
    using Validator = track_sharing::ReadResponseValidator<Crypto, Hash>;
    using Sink = track_sharing::ReadResponseSink<Store, Validator, Hash>;
    struct Lease
    {
        TrackReadOwner& owner;
        bool holds() const { return owner.live_.load(std::memory_order_acquire) && owner.mediaReady(); }
    };
    using Exchange = track_sharing::ReadNetworkExchange<chat::IMeshAdapter, Sink, Lease>;

    static TrackReadOwner* create(chat::IMeshAdapter& adapter, uint8_t operation, geocaching::ByteView request,
                                  geocaching::ByteView session, const track_sharing::PublicationDirectory& directory,
                                  geocaching::ByteView author, uint32_t generation,
                                  const track_sharing::ReadValidationContext& expected)
    {
        using namespace track_sharing;
        if (!readResponseOperation(operation) || !networkRequestId(request) || !networkRequestId(session) ||
            !readSpan(author, 64) || !generation || !publicationDirectoryValid(directory) ||
            !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
        constexpr auto caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        // Leave the existing transport's headroom; never spill into internal RAM.
        if (heap_caps_get_free_size(caps) < sizeof(TrackReadOwner) + 8192 || heap_caps_get_largest_free_block(caps) < sizeof(TrackReadOwner)) return nullptr;
        void* memory = heap_caps_malloc(sizeof(TrackReadOwner), caps);
        if (!memory) return nullptr;
        auto* result = new (memory) TrackReadOwner(adapter, operation, request, session, directory, author, generation, sd::sd_media_session());
        if (result->file_.has_backing_storage() && result->initialize(expected)) return result;
        result->~TrackReadOwner();
        heap_caps_free(memory);
        return nullptr;
    }
    static bool release(TrackReadOwner*& owner)
    {
        if (!owner) return true;
        owner->live_.store(false, std::memory_order_release);
        const auto changed = owner->exchange_.detach();
        if (changed != chat::TrackBindingChange::Applied && changed != chat::TrackBindingChange::Conflict) return false;
        if (owner->paths_owned_ && owner->mediaReady())
        {
            sd::sd_remove(owner->temporary_, owner->media_);
            sd::sd_remove(owner->destination_, owner->media_);
        }
        owner->~TrackReadOwner();
        heap_caps_free(owner);
        owner = nullptr;
        return true;
    }
    bool matchesSession(const uint8_t* session) const { return session && !std::memcmp(session_, session, 16); }
    bool matchesRequest(geocaching::ByteView request) const { return request.data && request.size == 16 && !std::memcmp(request_, request.data, 16); }
    // Caller retires the previous directory owner only after Applied. Its
    // binding must still be registered; no independently invoked bind/unbind.
    chat::TrackBindingChange attach(uint64_t now, const chat::lxmf::TrackDeliveryBinding* previous)
    {
        if (!lease_.holds()) return chat::TrackBindingChange::Conflict;
        size_t bytes = 0;
        bool encoded = false;
        if (operation_ == 0)
        {
            geocaching::protocol::CmpWriter writer(workspace_, sizeof(workspace_));
            encoded = track_sharing::networkRequestHeader(writer, 0, {request_, 16}, 8192, 0);
            bytes = writer.size();
        }
        else if (operation_ == 1) encoded = track_sharing::encodeTrackReadQuery({request_, 16}, query_, 8192, workspace_, sizeof(workspace_), bytes);
        else if (operation_ == 2) encoded = track_sharing::encodeTrackReadManifest({request_, 16}, expected_.object, expected_.revision, 8192, workspace_, sizeof(workspace_), bytes);
        else if (operation_ == 8) encoded = track_sharing::encodeTrackReadFile({request_, 16}, expected_.file_hash, expected_.offset, expected_.length, 8192, workspace_, sizeof(workspace_), bytes);
        else if (operation_ == 9) encoded = track_sharing::encodeRegionCapabilities({request_, 16}, 8192, workspace_, sizeof(workspace_), bytes);
        else if (operation_ == 10) encoded = track_sharing::encodeRegionChildren({request_, 16}, regional_scope_.version, track_sharing::regionBytes(regional_scope_.parent), track_sharing::regionBytes(regional_scope_.after), query_.limit, 8192, workspace_, sizeof(workspace_), bytes);
        else if (operation_ == 11) encoded = track_sharing::encodeRegionDescriptor({request_, 16}, track_sharing::regionBytes(regional_scope_.id), regional_scope_.version, 8192, workspace_, sizeof(workspace_), bytes);
        else if (operation_ == 12) encoded = track_sharing::encodeRegionBoundaryChunk({request_, 16}, track_sharing::regionBytes(regional_scope_.id), regional_scope_.version, expected_.offset, expected_.length, 8192, workspace_, sizeof(workspace_), bytes);
        else if (operation_ == 13) encoded = track_sharing::encodeRegionQuery({request_, 16}, track_sharing::regionBytes(regional_scope_.id), regional_scope_.version, query_, 8192, workspace_, sizeof(workspace_), bytes);
        if (!encoded || bytes > 320) return chat::TrackBindingChange::Conflict;
        return exchange_.attach(operation_, {request_, 16}, {directory_.delivery, 16}, {local_, 16}, {workspace_, bytes}, 8192, now, previous);
    }
    chat::TrackBindingChange advance(uint64_t now, track_sharing::ReadExchangeState& output) { return exchange_.advance(now, output); }
    // Metadata and SD reads are serialized with delivery persistence. The caller
    // must check Applied and Result::Ready before exposing bytes to USB.
    chat::TrackBindingChange read(uint32_t offset, uint16_t count, uint8_t* output, size_t capacity,
                                  size_t& written, track_sharing::StoredReadResponse& metadata, track_sharing::ReadStoreResult& result)
    {
        if (!output || !capacity || !track_sharing::networkInputDisjoint({output, capacity}, reinterpret_cast<uint8_t*>(this), sizeof(*this)) ||
            !track_sharing::networkInputDisjoint({reinterpret_cast<uint8_t*>(&metadata), sizeof(metadata)}, reinterpret_cast<uint8_t*>(this), sizeof(*this))) return chat::TrackBindingChange::Conflict;
        written = 0;
        metadata = {};
        result = track_sharing::ReadStoreResult::Invalid;
        struct Call
        {
            TrackReadOwner& owner;
            uint32_t offset;
            uint16_t count;
            uint8_t* output;
            size_t capacity;
            size_t& written;
            track_sharing::StoredReadResponse& metadata;
            track_sharing::ReadStoreResult& result;
        } call{*this, offset, count, output, capacity, written, metadata, result};
        return adapter_.visitTrackDelivery(
            exchange_.binding(), current_author_, current_generation_, [](void* value)
            {
            auto& data = *static_cast<Call*>(value);
            auto& self = data.owner;
            if (!self.lease_.holds() || self.current_generation_ != self.generation_ || std::memcmp(self.current_author_, self.author_, 64) || !self.stored_.ready) return;
            data.metadata = self.stored_;
            if (!data.count) { data.result = track_sharing::ReadStoreResult::Ready; return; }
            data.result = self.store_.read(self.stored_.bytes, data.offset, data.count, data.output, data.capacity, data.written); },
            &call);
    }

  private:
    TrackReadOwner(chat::IMeshAdapter& adapter, uint8_t operation, geocaching::ByteView request, geocaching::ByteView session,
                   const track_sharing::PublicationDirectory& directory, geocaching::ByteView author, uint32_t generation, uint32_t media)
        : adapter_(adapter), directory_(directory), file_(sd::SdRuntimeMemoryPolicy::PsramOnly), io_(file_, media), hash_(hash_context_),
          store_(io_, temporary_, destination_), validator_(expected_, crypto_, hash_, manifest_, row_, page_, workspace_, sizeof(workspace_), &regional_replies_),
          sink_(store_, validator_, hash_, stored_, workspace_, sizeof(workspace_)), lease_{*this}, exchange_(adapter, sink_, lease_, author, generation),
          generation_(generation), media_(media), operation_(operation)
    {
        std::memcpy(request_, request.data, 16);
        std::memcpy(session_, session.data, 16);
        std::memcpy(author_, author.data, 64);
        expected_.directory_public = {directory_.public_key, 64};
        expected_.directory_epoch = {directory_.epoch, 16};
    }
    bool mediaReady() const { return media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
    bool copy(geocaching::ByteView source, uint8_t* target, size_t size, geocaching::ByteView& output, bool optional = false)
    {
        if (optional && !source.size)
        {
            output = {};
            return true;
        }
        if (!source.data || source.size != size) return false;
        std::memcpy(target, source.data, size);
        output = {target, size};
        return true;
    }
    bool initialize(const track_sharing::ReadValidationContext& input)
    {
        using namespace track_sharing;
        if (adapter_.readDeviceAuthorKey(current_author_, current_generation_) != chat::DeviceAuthorResult::Ok ||
            current_generation_ != generation_ || std::memcmp(current_author_, author_, 64)) return false;
        if (!adapter_.trackLocalDestination(local_) || !networkRequestId({local_, 16}) || !mediaReady()) return false;
        if (operation_ >= 10)
        {
            if (!input.region || !copyRegionScope(*input.region)) return false;
            expected_.region = &regional_scope_;
        }
        if (operation_ == 1 || operation_ == 13)
        {
            if (!input.query || input.query->limit > (operation_ == 1 ? directory_.query_limit : 32)) return false;
            query_ = *input.query;
            if (!copy(input.query->author, query_author_, 64, query_.author, true) ||
                !copy(input.query->cursor_object, cursor_, 32, query_.cursor_object, true) ||
                !copy(input.query->cursor_binding, cursor_ + 32, 32, query_.cursor_binding, true)) return false;
            if (operation_ == 1)
            {
                if (!deriveReadQueryBinding(query_, hash_, workspace_, sizeof(workspace_), query_binding_)) return false;
            }
            else if (query_.west < -1800000000 || query_.west > 1799999999 || query_.east < -1800000000 || query_.east > 1799999999 ||
                     query_.south < -900000000 || query_.north > 900000000 || query_.south > query_.north ||
                     !deriveRegionQueryBinding(regionBytes(regional_scope_.id), regional_scope_.version, query_, hash_, workspace_, sizeof(workspace_), query_binding_)) return false;
            if (query_.has_cursor && std::memcmp(query_.cursor_binding.data, query_binding_, 32)) return false;
            expected_.query = &query_;
            expected_.query_binding = {query_binding_, 32};
        }
        else if (operation_ == 2)
        {
            if (!copy(input.object, reference_, 32, expected_.object) || !copy(input.revision, reference_ + 32, 32, expected_.revision, true)) return false;
        }
        else if (operation_ == 8 || operation_ == 12)
        {
            if (!input.file_bytes || input.file_bytes > (operation_ == 8 ? directory_.file_limit : 8388608) || !input.length || input.length > (operation_ == 8 ? directory_.chunk_limit : 1024) ||
                input.offset >= input.file_bytes || input.length > input.file_bytes - input.offset || !copy(input.file_hash, reference_, 32, expected_.file_hash)) return false;
            expected_.file_bytes = input.file_bytes;
            expected_.offset = input.offset;
            expected_.length = input.length;
        }
        else if (operation_ == 10)
        {
            if (!input.query || !input.query->limit || input.query->limit > 32 || regional_scope_.parent_level < -1 || regional_scope_.parent_level > regional_scope_.depth ||
                (regional_scope_.parent.empty() != (regional_scope_.parent_level == -1))) return false;
            query_.limit = input.query->limit;
            expected_.query = &query_;
        }
        constexpr char root[] = "/trailmate/track-read";
        if ((!sd::sd_is_directory("/trailmate") && !sd::sd_mkdir("/trailmate")) ||
            (!sd::sd_is_directory(root) && !sd::sd_mkdir(root)) || !mediaReady()) return false;
        constexpr char hex[] = "0123456789abcdef";
        size_t position = sizeof(root) - 1;
        std::memcpy(temporary_, root, position);
        temporary_[position++] = '/';
        for (const auto input_id : {geocaching::ByteView{session_, 16}, {request_, 16}})
            for (size_t i = 0; i < 16; ++i)
            {
                temporary_[position++] = hex[input_id.data[i] >> 4];
                temporary_[position++] = hex[input_id.data[i] & 15];
            }
        std::memcpy(destination_, temporary_, position);
        std::memcpy(temporary_ + position, ".tmp", 5);
        std::memcpy(destination_ + position, ".cmp", 5);
        uint8_t probe = 0;
        if (sd::sd_read_file(temporary_, &probe, 1).status != sd::SdFileReadStatus::Missing ||
            sd::sd_read_file(destination_, &probe, 1).status != sd::SdFileReadStatus::Missing || !mediaReady()) return false;
        paths_owned_ = true;
        return true;
    }
    bool copyRegionScope(const track_sharing::RegionReadScope& input)
    {
        using namespace track_sharing;
        if (!regionNamespace(input.name_space) || input.depth > 8 || !copy(input.version, reference_ + 32, 32, regional_scope_.version)) return false;
        const auto text = [](std::string_view source, char* target, size_t capacity, std::string_view& output)
        {
            if (source.size() > capacity || (source.size() && !source.data())) return false;
            if (source.size()) std::memcpy(target, source.data(), source.size());
            output = {target, source.size()};
            return true;
        };
        if (!text(input.name_space, regional_text_, 24, regional_scope_.name_space) || !text(input.id, regional_text_ + 24, 89, regional_scope_.id) ||
            !text(input.parent, regional_text_ + 113, 89, regional_scope_.parent) || !text(input.after, regional_text_ + 202, 89, regional_scope_.after) ||
            !regionVersion(regional_scope_.version) ||
            (!regional_scope_.id.empty() && !regionInNamespace(regional_scope_.id, regional_scope_.name_space)) ||
            (!regional_scope_.parent.empty() && !regionInNamespace(regional_scope_.parent, regional_scope_.name_space)) ||
            (!regional_scope_.after.empty() && !regionInNamespace(regional_scope_.after, regional_scope_.name_space)) ||
            (operation_ != 10 && regional_scope_.id.empty())) return false;
        regional_scope_.depth = input.depth;
        regional_scope_.parent_level = input.parent_level;
        return true;
    }
    chat::IMeshAdapter& adapter_;
    track_sharing::PublicationDirectory directory_;
    sd::SdRuntimeFile file_;
    sd::SdBorrowedRecordFileIo io_;
    mbedtls_sha256_context hash_context_;
    Hash hash_;
    Crypto crypto_;
    char temporary_[96]{}, destination_[96]{};
    uint8_t workspace_[4200]{}, reference_[64]{}, query_author_[64]{}, cursor_[64]{}, query_binding_[32]{};
    uint8_t request_[16]{}, session_[16]{}, author_[64]{}, local_[16]{}, current_author_[64]{};
    track_sharing::ReadQuery query_;
    track_sharing::ReadValidationContext expected_;
    char regional_text_[291]{};
    track_sharing::RegionReadScope regional_scope_;
    track_sharing::RegionReplyWorkspace regional_replies_;
    track_sharing::VerifiedManifestView manifest_;
    track_sharing::ReadQueryRow row_;
    track_sharing::ReadQueryResponse page_;
    track_sharing::StoredReadResponse stored_;
    Store store_;
    Validator validator_;
    Sink sink_;
    Lease lease_;
    Exchange exchange_;
    uint32_t generation_, media_, current_generation_ = 0;
    uint8_t operation_;
    bool paths_owned_ = false;
    std::atomic<bool> live_{true};
};
static_assert(sizeof(TrackReadOwner) <= 8192, "Read owner is a transient PSRAM workspace, never an internal-RAM response queue");
} // namespace hostlink::management::track_read_runtime
