#pragma once
#include "track_sharing/prepare_request.h"
#include "track_sharing/upload_paths.h"
#include "track_sharing/upload_journal_store.h"
namespace track_sharing
{
// Caller lends three distinct metadata slots, two 64-byte canonical path slots
// and >=256 command scratch, all disjoint from request/identity and each other.
// Media-bound I/O and exclusive immutable upload lease are platform-owned.
template <class Io>
JournalStoreResult loadPrepareUploadInputs(Io& io, const PrepareRequest& request, uint32_t generation, const uint8_t* author_hash,
                                         UploadJournal& kml, UploadJournal& manifest, UploadJournal& verify,
                                         char* kml_path, char* manifest_path, uint8_t* scratch, size_t capacity)
{
    if (&kml == &manifest || &kml == &verify || &manifest == &verify || !kml_path || !manifest_path || kml_path == manifest_path ||
        !scratch || capacity < 256 || !author_hash || !request.kml_upload.data || request.kml_upload.size != 16 ||
        !request.manifest_upload.data || request.manifest_upload.size != 16 || !generation || request.generation != generation) return JournalStoreResult::Invalid;
    const auto overlap = [](const void* left, size_t left_size, const void* right, size_t right_size)
    { const auto a = reinterpret_cast<uintptr_t>(left), b = reinterpret_cast<uintptr_t>(right); return a <= b ? b - a < left_size : a - b < right_size; };
    const auto protectedInput = [&](const void* input, size_t size)
    { return overlap(input, size, scratch, capacity) || overlap(input, size, kml_path, 64) || overlap(input, size, manifest_path, 64) ||
             overlap(input, size, &kml, sizeof(kml)) || overlap(input, size, &manifest, sizeof(manifest)) || overlap(input, size, &verify, sizeof(verify)); };
    const auto pathOverlapsMetadata = [&](const char* path)
    { return overlap(path, 64, &kml, sizeof(kml)) || overlap(path, 64, &manifest, sizeof(manifest)) || overlap(path, 64, &verify, sizeof(verify)); };
    if (protectedInput(&io, sizeof(io)) || protectedInput(&request, sizeof(request)) || protectedInput(request.kml_upload.data, 16) || protectedInput(request.manifest_upload.data, 16) || protectedInput(author_hash, 32) ||
        (request.operation.data && protectedInput(request.operation.data, request.operation.size)) ||
        (request.expected_parent.data && protectedInput(request.expected_parent.data, request.expected_parent.size)) ||
        pathOverlapsMetadata(kml_path) || pathOverlapsMetadata(manifest_path) ||
        overlap(&kml, sizeof(kml), &manifest, sizeof(manifest)) || overlap(&kml, sizeof(kml), &verify, sizeof(verify)) || overlap(&manifest, sizeof(manifest), &verify, sizeof(verify)) ||
        overlap(kml_path, 64, manifest_path, 64) || overlap(kml_path, 64, scratch, capacity) || overlap(manifest_path, 64, scratch, capacity) ||
        overlap(&kml, sizeof(kml), scratch, capacity) || overlap(&manifest, sizeof(manifest), scratch, capacity) || overlap(&verify, sizeof(verify), scratch, capacity)) return JournalStoreResult::Invalid;
    kml = {}; manifest = {}; verify = {};
    kml_path[0] = manifest_path[0] = 0;
    auto* a = reinterpret_cast<char*>(scratch + capacity - 128);
    auto* b = a + 64;
    const auto load = [&](UploadKind kind, geocaching::ByteView id, UploadJournal& output, char* path)
    {
        if (!resolveUploadPath(id.data, id.size, UploadFile::Candidate, path, 64, kind) ||
            !resolveUploadPath(id.data, id.size, UploadFile::JournalA, a, 64, kind) ||
            !resolveUploadPath(id.data, id.size, UploadFile::JournalB, b, 64, kind)) return JournalStoreResult::Invalid;
        UploadJournalStore<Io> store(io, a, b);
        const auto result = store.load(output, verify, scratch, capacity - 128);
        if (result != JournalStoreResult::Ready) return result;
        return output.phase == UploadPhase::Sealed && output.total <= uploadByteLimit(kind) && output.generation == generation &&
                       !std::memcmp(output.id, id.data, 16) && !std::memcmp(output.author_hash, author_hash, 32)
                   ? JournalStoreResult::Ready : JournalStoreResult::Conflict;
    };
    auto result = load(UploadKind::Kml, request.kml_upload, kml, kml_path);
    if (result == JournalStoreResult::Ready) result = load(UploadKind::Manifest, request.manifest_upload, manifest, manifest_path);
    if (result != JournalStoreResult::Ready) { kml = {}; manifest = {}; verify = {}; kml_path[0] = manifest_path[0] = 0; }
    return result;
}
} // namespace track_sharing
