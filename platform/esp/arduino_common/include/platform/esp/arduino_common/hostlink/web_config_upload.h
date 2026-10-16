#pragma once

#include "app/app_facade_access.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/hostlink_codec.h"
#include "hostlink/management_envelope.h"
#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"
#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"

namespace hostlink::management::web_config
{
using platform::esp::arduino_common::storage::SdRuntimeFile;
constexpr const char* kMetaPath = "/trailmate/config.web.meta";
constexpr const char* kMetaNew = "/trailmate/config.web.meta.new";
constexpr const char* kMetaBackup = "/trailmate/config.web.meta.bak";
constexpr size_t kMetaBytes = 111;
struct Metadata
{
    uint8_t owner[16]{}, id[16]{}, hash[32]{}, base_hash[32]{};
    uint32_t total = 0, offset = 0;
    uint8_t phase = 0; // config: 0/1/2; draft: 0 receive,1 seal,2 bind,3 intent,4 committed
    uint8_t kind = 1;  // 1 config, 2 draft, 5 unsigned record, 6 SignedCache confirmation
};
static_assert(sizeof(Metadata) == 108, "Shared transfer metadata grew its RAM budget");
inline const char* candidate_path(uint8_t kind)
{
    if (kind == 5) return "/trailmate/cache.sign.upload";
    if (kind == 6) return "/trailmate/cache.confirm.upload";
    return kind == 2 ? platform::esp::arduino_common::geocaching::browse_runtime::kWebDraftCandidatePath
                     : app::sd_tms::webWorkingCandidatePath();
}
inline uint32_t transfer_limit(uint8_t kind)
{
    return kind == 1 ? 32768 : kind == 6              ? 4166
                           : (kind == 2 || kind == 5) ? 4096
                                                      : 0;
}
inline uint32_t u32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put32(uint8_t* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
inline bool load(Metadata& meta, uint8_t* scratch)
{
    if (!chat::storage::v2::recoverAtomicFile(kMetaPath, kMetaNew, kMetaBackup)) return false;
    SdRuntimeFile file;
    if (!file.open(kMetaPath, "r") || file.size() != kMetaBytes || file.read(scratch, kMetaBytes) != static_cast<int>(kMetaBytes) ||
        std::memcmp(scratch, "TMU1", 4) || crc16_ccitt(scratch, 109) != (uint16_t(scratch[109]) | uint16_t(scratch[110]) << 8)) return false;
    std::memcpy(meta.owner, scratch + 4, 16);
    std::memcpy(meta.id, scratch + 20, 16);
    std::memcpy(meta.hash, scratch + 36, 32);
    std::memcpy(meta.base_hash, scratch + 77, 32);
    meta.total = u32(scratch + 68);
    meta.offset = u32(scratch + 72);
    const auto tagged_phase = scratch[76];
    if (tagged_phase <= 2) meta.kind = 1;
    else if (tagged_phase >= 0x80 && tagged_phase <= 0x84) meta.kind = 2;
    else if (tagged_phase >= 0x40 && tagged_phase <= 0x42) meta.kind = 5;
    else if (tagged_phase >= 0xc0 && tagged_phase <= 0xc2) meta.kind = 6;
    else return false;
    meta.phase = tagged_phase & 0x3f;
    return meta.total && meta.total <= transfer_limit(meta.kind) && meta.offset <= meta.total &&
           meta.phase <= (meta.kind == 2 ? 4 : 2) && (!meta.phase || meta.offset == meta.total);
}
inline bool save(const Metadata& meta, uint8_t* scratch)
{
    if (!scratch || !transfer_limit(meta.kind) || !meta.total ||
        meta.total > transfer_limit(meta.kind) || meta.offset > meta.total ||
        meta.phase > (meta.kind == 2 ? 4 : 2) || (meta.phase && meta.offset != meta.total)) return false;
    std::memcpy(scratch, "TMU1", 4);
    std::memcpy(scratch + 4, meta.owner, 16);
    std::memcpy(scratch + 20, meta.id, 16);
    std::memcpy(scratch + 36, meta.hash, 32);
    put32(scratch + 68, meta.total);
    put32(scratch + 72, meta.offset);
    scratch[76] = meta.phase | (meta.kind == 2 ? 0x80 : meta.kind == 5 ? 0x40
                                                    : meta.kind == 6   ? 0xc0
                                                                       : 0);
    std::memcpy(scratch + 77, meta.base_hash, 32);
    const uint16_t crc = crc16_ccitt(scratch, 109);
    scratch[109] = static_cast<uint8_t>(crc);
    scratch[110] = static_cast<uint8_t>(crc >> 8);
    SdRuntimeFile file;
    if (!file.open(kMetaNew, "w") || file.write(scratch, kMetaBytes) != kMetaBytes || !file.flush()) return false;
    file.close();
    return chat::storage::v2::replaceFileAtomically(kMetaNew, kMetaPath, kMetaBackup);
}
inline bool hash_file(const char* path, uint8_t* scratch, uint8_t hash[32], uint32_t required = 0)
{
    SdRuntimeFile file;
    if (!file.open(path, "r")) return false;
    const uint64_t size = file.size();
    if (!size || size > 32768 || (required && size != required)) return false;
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    bool ok = platform::esp::common::crypto::sha256_starts(&context, 0) == 0;
    uint64_t offset = 0;
    while (ok && offset < size)
    {
        const size_t count = static_cast<size_t>(size - offset > 128 ? 128 : size - offset);
        ok = file.read(scratch, count) == static_cast<int>(count) &&
             platform::esp::common::crypto::sha256_update(&context, scratch, count) == 0;
        offset += count;
    }
    ok = ok && platform::esp::common::crypto::sha256_finish(&context, hash) == 0;
    mbedtls_sha256_free(&context);
    return ok;
}
inline bool reply(const Metadata& meta, uint8_t* scratch, size_t capacity, size_t& size)
{
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    const bool ok = writer.array(4) && writer.binary({meta.id, 16}) && writer.unsignedInteger(320) &&
                    writer.unsignedInteger(meta.offset) && writer.unsignedInteger(meta.phase ? 1 : 0);
    size = writer.size();
    return ok;
}

// A lost draft-put acknowledgement must not permanently occupy the one slot.
// Reconcile only a committed row belonging to the unchanged device author;
// pending/failed intents and candidate bytes remain untouched.
inline Status reconcileCommittedDraft(Metadata& meta, const uint8_t* author, uint32_t generation, uint8_t* scratch)
{
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    if (meta.kind != 2 || meta.phase != 3 || !author || !scratch) return Status::InvalidField;
    if (!generation || generation != u32(meta.base_hash + 24)) return Status::StaleIdentity;
    uint64_t expected = 0;
    for (unsigned i = 0; i < 8; ++i) expected |= static_cast<uint64_t>(meta.base_hash[16 + i]) << (8 * i);
    if (expected == UINT64_MAX) return Status::InvalidField;
    const auto result = runtime::checkWebDraftEditOwner(meta.base_hash, expected, author, meta.hash);
    if (result == runtime::WebReadResult::AlreadySaved)
    {
        meta.phase = 4;
        return save(meta, scratch) ? Status::Ok : Status::StorageUnavailable;
    }
    if (result == runtime::WebReadResult::Pending || result == runtime::WebReadResult::Ready) return Status::Busy;
    if (result == runtime::WebReadResult::Forbidden) return Status::Unauthorized;
    if (result == runtime::WebReadResult::Unavailable) return Status::StorageUnavailable;
    return Status::Conflict;
}

// Reconnect can lose the old browser session while retaining the exact edit.
// Rebind only a draft already tied to this author generation and draft version;
// never truncate its candidate or replace an in-progress save.
inline Status resumeBoundDraft(Metadata& meta, const Metadata& requested, uint8_t* scratch, size_t capacity)
{
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    if (capacity < 448) return Status::Internal;
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    uint32_t generation = 0;
    const auto author = adapter->readDeviceAuthorKey(scratch + 384, generation);
    if (author == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
    if (author != chat::DeviceAuthorResult::Ok) return Status::Busy;
    if (!generation || generation != u32(meta.base_hash + 24)) return Status::StaleIdentity;
    uint64_t expected = 0;
    for (unsigned i = 0; i < 8; ++i) expected |= static_cast<uint64_t>(meta.base_hash[16 + i]) << (8 * i);
    const auto result = runtime::checkWebDraftEditOwner(meta.base_hash, expected, scratch + 384, meta.hash);
    if (result == runtime::WebReadResult::AlreadySaved) meta.phase = 4;
    else if (result == runtime::WebReadResult::Ready)
    {
        if (!runtime::releaseWebDraftSave()) return Status::Busy;
        meta.phase = 2;
    }
    else if (result == runtime::WebReadResult::Pending) return Status::Busy;
    else if (result == runtime::WebReadResult::Forbidden) return Status::Unauthorized;
    else if (result == runtime::WebReadResult::Unavailable) return Status::StorageUnavailable;
    else return Status::Conflict;
    std::memcpy(meta.owner, requested.owner, 16);
    std::memcpy(meta.id, requested.id, 16);
    return save(meta, scratch) ? Status::Ok : Status::StorageUnavailable;
}

// Opt-in operation 38 [5,null]. The generation below is the current proved
// author generation, not an inferred generation of legacy upload metadata.
// Returns only sealed unsigned input belonging to that public author. Does not
// sign, abort, advance a phase, or return candidate/private-key bytes.
// Caller holds withSignatureInputRecovery throughout. Never acquire another
// runtime mutex from inside that non-recursive storage gate.
inline Status listSharedSignatureUpload(const EnvelopeView& request, const uint8_t* author, uint32_t generation,
                                        uint32_t media, uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    if (!request.session || !author || !generation || !scratch || capacity < 448 || request.body_size != 3 ||
        request.body[0] != 0x92 || (request.body[1] != 5 && request.body[1] != 6) || request.body[2] != 0xc0) return Status::InvalidField;
    const uint8_t kind = request.body[1];
    Metadata meta;
    const bool loaded = load(meta, scratch);
    if (!loaded && (platform::esp::arduino_common::storage::sd_exists(kMetaPath) ||
                    platform::esp::arduino_common::storage::sd_exists(kMetaNew) ||
                    platform::esp::arduino_common::storage::sd_exists(kMetaBackup))) return Status::StorageUnavailable;
    bool found = loaded && meta.kind == kind && meta.phase == 1;
    if (found)
    {
        SdRuntimeFile file;
        if (!file.open(candidate_path(kind), "r") || file.size() != meta.total) return Status::StorageUnavailable;
        const auto count = meta.total < 128 ? meta.total : 128;
        if (file.read(scratch, count) != static_cast<int>(count)) return Status::StorageUnavailable;
        file.close();
        size_t start = 0;
        if (kind == 6)
        {
            // SignedCache is [bin(unsigned_record),bin(signature64)]. Inspect
            // only the small public record prefix; never return either body.
            if (count < 4 || scratch[0] != 0x92 || (scratch[1] != 0xc4 && scratch[1] != 0xc5)) return Status::Conflict;
            const uint32_t record_size = scratch[1] == 0xc4 ? scratch[2] : (uint32_t(scratch[2]) << 8 | scratch[3]);
            start = scratch[1] == 0xc4 ? 3 : 4;
            if (!record_size || record_size > 4096 || (start == 4 && record_size < 256) || start + record_size + 66 != meta.total) return Status::Conflict;
        }
        geocaching::protocol::CmpReader prefix({scratch + start, count - start});
        size_t fields = 0;
        uint64_t version = 0;
        geocaching::ByteView key, nonce;
        if (!prefix.array(fields, 16) || fields != 16 || !prefix.unsignedInteger(version) || version != 1 ||
            !prefix.binary(key, 64) || key.size != 64 || !prefix.binary(nonce, 16) || nonce.size != 16) return Status::Conflict;
        found = !std::memcmp(author, key.data, 64);
        if (found)
        {
            std::memcpy(scratch + 256, nonce.data, 16);
            if (!hash_file(candidate_path(kind), scratch, scratch + 288, meta.total)) return Status::StorageUnavailable;
            if (std::memcmp(meta.hash, scratch + 288, 32)) return Status::HashMismatch;
        }
    }
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    if (!writer.array(3) || !writer.unsignedInteger(media) || !writer.nil()) return Status::Internal;
    if (found)
    {
        if (!writer.array(9) || !writer.binary({meta.id, 16}) || !writer.unsignedInteger(kind) ||
            !writer.unsignedInteger(meta.total) || !writer.binary({meta.hash, 32}) || !writer.unsignedInteger(generation) ||
            !writer.unsignedInteger(meta.phase) || !writer.unsignedInteger(meta.offset) ||
            !writer.binary({meta.owner, 16}) || !writer.binary({scratch + 256, 16})) return Status::Internal;
    }
    else if (!writer.nil()) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}

// A durable confirmation releases the single upload slot before its ACK. Keep
// the candidate for exact replay after a lost reply/reboot; new IDs may reuse
// terminal metadata. Response is saved in the existing command arena.
inline Status finishConfirmedUpload(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    if (!request.session || !output || capacity < 448 || !written || written > 160) return Status::Internal;
    const auto reply_size = written;
    written = 0;
    auto* reply = output + capacity - 160;
    std::memmove(reply, output, reply_size);
    geocaching::protocol::CmpReader reader({reply, reply_size});
    size_t fields = 0;
    uint64_t generation = 0, phase = 0, revision = 0, durable = 0;
    geocaching::ByteView id, hash, cache, version_hash;
    if (!reader.array(fields, 8) || fields != 8 || !reader.binary(id, 16) || id.size != 16 ||
        !reader.binary(hash, 32) || hash.size != 32 || !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.unsignedInteger(phase) || phase > 2) return Status::Internal;
    if (phase == 1)
    {
        if (!reader.binary(cache, 32) || cache.size != 32 || !reader.unsignedInteger(revision) || !revision || revision > UINT32_MAX ||
            !reader.binary(version_hash, 32) || version_hash.size != 32 || !reader.unsignedInteger(durable) || durable != 1 || !reader.finished()) return Status::Internal;
        Metadata meta;
        if (!load(meta, output)) return Status::StorageUnavailable;
        if (meta.kind != 6 || !meta.phase || std::memcmp(meta.owner, request.session, 16) ||
            std::memcmp(meta.id, id.data, 16) || std::memcmp(meta.hash, hash.data, 32)) return Status::Conflict;
        if (meta.phase != 2)
        {
            meta.phase = 2;
            if (!save(meta, output)) return Status::StorageUnavailable;
        }
    }
    std::memmove(output, reply, reply_size);
    written = reply_size;
    return Status::Ok;
}

// All persistent transfer state lives on device storage; the command payload is
// reused as scratch. No static payload arena, task, queue or heap document.
inline Status process(const EnvelopeView& request, uint8_t* scratch, size_t capacity, size_t& body_size)
{
    if (capacity < 384) return Status::Internal;
    using namespace platform::esp::arduino_common::storage;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields;
    geocaching::ByteView id, data, expected;
    Metadata requested;
    std::memcpy(requested.owner, request.session, 16);
    uint64_t offset = 0, kind = 0, total = 0;
    const auto op = static_cast<Operation>(request.code);
    if (op == Operation::TransferBegin)
    {
        if (!reader.array(fields, 4) || fields != 4 || !reader.binary(id, 16) || id.size != 16 ||
            !reader.unsignedInteger(kind) || kind > UINT8_MAX || !transfer_limit(static_cast<uint8_t>(kind)) || !reader.unsignedInteger(total) || !total ||
            total > transfer_limit(static_cast<uint8_t>(kind)) ||
            !reader.binary(expected, 32) || expected.size != 32 || !reader.finished()) return Status::InvalidField;
        std::memcpy(requested.id, id.data, 16);
        std::memcpy(requested.hash, expected.data, 32);
        requested.total = static_cast<uint32_t>(total);
        requested.kind = static_cast<uint8_t>(kind);
    }
    else
    {
        const size_t count = op == Operation::TransferChunk ? 3 : op == Operation::ConfigCommit ? 2
                                                                                                : 1;
        if (!reader.array(fields, count) || fields != count || !reader.binary(id, 16) || id.size != 16) return Status::InvalidField;
        std::memcpy(requested.id, id.data, 16);
        if (op == Operation::TransferChunk && (!reader.unsignedInteger(offset) || offset > UINT32_MAX ||
                                               !reader.binary(data, 320) || !data.size)) return Status::InvalidField;
        if (op == Operation::ConfigCommit && (!reader.binary(expected, 32) || expected.size != 32)) return Status::InvalidField;
        if (expected.data) std::memcpy(requested.base_hash, expected.data, 32);
        if (!reader.finished()) return Status::InvalidField;
    }
    // CHUNK bytes may overlap scratch. Move them to the far end before journal
    // reads overwrite the beginning; 128-byte journal/hash scratch stays clear.
    uint8_t* chunk = scratch + capacity - data.size;
    if (data.size) std::memmove(chunk, data.data, data.size);
    Metadata current;
    const bool exists = load(current, scratch);
    if (op == Operation::TransferBegin)
    {
        if (exists && current.kind == 2 && requested.kind == 2 && current.phase >= 2 && current.phase < 4 &&
            current.total == requested.total && !std::memcmp(current.hash, requested.hash, 32) &&
            (std::memcmp(current.owner, requested.owner, 16) || std::memcmp(current.id, requested.id, 16)))
        {
            const auto resumed = resumeBoundDraft(current, requested, scratch, capacity);
            if (resumed != Status::Ok) return resumed;
        }
        if (exists && current.kind == 2 && current.phase == 3 && std::memcmp(current.id, requested.id, 16))
        {
            if (capacity < 448) return Status::Internal;
            auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
            if (!adapter) return Status::Busy;
            uint32_t generation = 0;
            const auto identity = adapter->readDeviceAuthorKey(scratch + 384, generation);
            if (identity == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
            if (identity != chat::DeviceAuthorResult::Ok) return Status::Busy;
            const auto reconciled = reconcileCommittedDraft(current, scratch + 384, generation, scratch);
            if (reconciled != Status::Ok) return reconciled;
        }
        if (exists && (current.phase < (current.kind == 2 ? 4 : 2) || !std::memcmp(current.id, requested.id, 16)))
        {
            if (std::memcmp(current.owner, requested.owner, 16)) return Status::Busy;
            if (std::memcmp(current.id, requested.id, 16) || current.kind != requested.kind || current.total != requested.total ||
                std::memcmp(current.hash, requested.hash, 32)) return Status::Conflict;
        }
        else
        {
            if (app::sd_tms::webConfigurationRestartPending()) return Status::Busy;
            if (sd_exists(kMetaPath) && !exists) return Status::StorageUnavailable;
            if (requested.kind == 1 && (app::sd_tms::syncPendingWorkingConfig() != app::sd_tms::WorkingConfigSyncResult::Synchronized ||
                                        !hash_file("/trailmate/config.tms", scratch, requested.base_hash))) return Status::StorageUnavailable;
            SdRuntimeFile file;
            if (!file.open(candidate_path(requested.kind), "w") || !file.flush()) return Status::StorageUnavailable;
            file.close();
            current = requested;
            if (!save(current, scratch)) return Status::StorageUnavailable;
        }
    }
    else
    {
        if (!exists) return Status::Expired;
        if (std::memcmp(current.owner, requested.owner, 16)) return Status::Unauthorized;
        if (std::memcmp(current.id, requested.id, 16)) return Status::Conflict;
        if (current.kind != 1 && (op == Operation::ConfigValidate || op == Operation::ConfigCommit)) return Status::InvalidField;
        if (op == Operation::TransferAbort)
        {
            if (current.phase >= (current.kind == 2 ? 3 : 2)) return Status::Conflict;
            if (current.kind == 2 && !platform::esp::arduino_common::geocaching::browse_runtime::releaseWebDraftSave()) return Status::Busy;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
            if (current.kind == 5 || current.kind == 6)
            {
                namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
                const auto released = current.kind == 5 ? runtime::releaseWebSignature(request.session) : runtime::releaseWebConfirmation(request.session);
                if (released == runtime::WebReadResult::Pending) return Status::Busy;
                if (released == runtime::WebReadResult::Forbidden) return Status::Unauthorized;
                if (released != runtime::WebReadResult::Ready && released != runtime::WebReadResult::NotFound) return Status::Internal;
            }
#endif
            if ((sd_exists(candidate_path(current.kind)) && !sd_remove(candidate_path(current.kind))) ||
                (sd_exists(kMetaNew) && !sd_remove(kMetaNew)) ||
                (sd_exists(kMetaBackup) && !sd_remove(kMetaBackup)) ||
                !sd_remove(kMetaPath)) return Status::StorageUnavailable;
            scratch[0] = 0x90;
            body_size = 1;
            return Status::Ok;
        }
        if (op == Operation::TransferChunk)
        {
            if (offset > current.total || data.size > current.total - offset || offset > current.offset) return Status::Conflict;
            SdRuntimeFile file;
            if (!file.open(candidate_path(current.kind), "r+") || !file.seek(offset)) return Status::StorageUnavailable;
            if (offset < current.offset)
            {
                if (data.size > current.offset - offset) return Status::Conflict;
                for (size_t i = 0; i < data.size; ++i)
                    if (file.read_byte() != chunk[i]) return Status::Conflict;
            }
            else
            {
                if (current.phase) return Status::Conflict;
                if (file.write(chunk, data.size) != data.size || !file.flush()) return Status::StorageUnavailable;
                file.close();
                current.offset += static_cast<uint32_t>(data.size);
                if (!save(current, scratch)) return Status::StorageUnavailable;
            }
        }
        else if (op == Operation::TransferFinish || op == Operation::ConfigValidate)
        {
            uint8_t hash[32];
            if (current.offset != current.total) return Status::Conflict;
            if (!current.phase)
            {
                if (!hash_file(candidate_path(current.kind), scratch, hash, current.total)) return Status::StorageUnavailable;
                if (std::memcmp(current.hash, hash, 32)) return Status::HashMismatch;
                if (current.kind == 1 && !app::sd_tms::validateWebWorkingCandidate()) return Status::InvalidField;
                current.phase = 1;
                if (!save(current, scratch)) return Status::StorageUnavailable;
            }
        }
        else if (op == Operation::ConfigCommit)
        {
            uint8_t hash[32];
            if (!current.phase || std::memcmp(current.base_hash, requested.base_hash, 32)) return Status::Conflict;
            if (current.phase != 2)
            {
                // A retry after power loss between promotion and journal update
                // recognises the exact already-promoted file, not a new save.
                if (!hash_file("/trailmate/config.tms", scratch, hash)) return Status::StorageUnavailable;
                if (std::memcmp(hash, current.hash, 32))
                {
                    if (app::sd_tms::syncPendingWorkingConfig() != app::sd_tms::WorkingConfigSyncResult::Synchronized ||
                        !hash_file("/trailmate/config.tms", scratch, hash)) return Status::StorageUnavailable;
                    if (std::memcmp(hash, current.base_hash, 32)) return Status::Conflict;
                    if (!hash_file(app::sd_tms::webWorkingCandidatePath(), scratch, hash, current.total) ||
                        std::memcmp(hash, current.hash, 32)) return Status::HashMismatch;
                    if (!app::sd_tms::commitWebWorkingCandidate()) return Status::StorageUnavailable;
                }
                current.phase = 2;
                if (!save(current, scratch)) return Status::StorageUnavailable;
            }
            geocaching::protocol::CmpWriter writer(scratch, capacity);
            if (!writer.array(3) || !writer.binary({current.id, 16}) || !writer.binary({current.hash, 32}) ||
                !writer.unsignedInteger(app::sd_tms::webConfigurationRestartPending() ? 1 : 0)) return Status::Internal;
            body_size = writer.size();
            return Status::Ok;
        }
        else if (op != Operation::TransferStatus) return Status::Unsupported;
    }
    return reply(current, scratch, capacity, body_size) ? Status::Ok : Status::Internal;
}
} // namespace hostlink::management::web_config
