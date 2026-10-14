#pragma once
#include "track_sharing/sign_manifest.h"
#include "track_sharing/upload_journal_store.h"
#include "track_sharing/upload_paths.h"
namespace track_sharing
{
template <class Io>
JournalStoreResult loadBoundSealedUpload(Io& io, geocaching::ByteView id, UploadKind kind, uint32_t generation, const uint8_t* author,
                                         UploadJournal& output, UploadJournal& verify, char* path, uint8_t* scratch, size_t capacity)
{
    if (!id.data || id.size != 16 || !generation || !author || !path || !scratch || capacity < 256 || &output == &verify || !uploadByteLimit(kind)) return JournalStoreResult::Invalid;
    const auto overlaps = [](const void* a, size_t as, const void* b, size_t bs)
    { return signingSpansOverlap(static_cast<const uint8_t*>(a), as, static_cast<const uint8_t*>(b), bs); };
    for (const auto source : {id, geocaching::ByteView{author, 32}, geocaching::ByteView{reinterpret_cast<const uint8_t*>(&io), sizeof(io)}})
        if (overlaps(source.data, source.size, path, 64) || overlaps(source.data, source.size, scratch, capacity) ||
            overlaps(source.data, source.size, &output, sizeof(output)) || overlaps(source.data, source.size, &verify, sizeof(verify))) return JournalStoreResult::Invalid;
    if (overlaps(&output, sizeof(output), &verify, sizeof(verify)) || overlaps(path, 64, scratch, capacity) ||
        overlaps(&output, sizeof(output), path, 64) || overlaps(&verify, sizeof(verify), path, 64) ||
        overlaps(&output, sizeof(output), scratch, capacity) || overlaps(&verify, sizeof(verify), scratch, capacity)) return JournalStoreResult::Invalid;
    auto* a = reinterpret_cast<char*>(scratch + capacity - 128);
    auto* b = a + 64;
    if (!resolveUploadPath(id.data, 16, UploadFile::Candidate, path, 64, kind) ||
        !resolveUploadPath(id.data, 16, UploadFile::JournalA, a, 64, kind) || !resolveUploadPath(id.data, 16, UploadFile::JournalB, b, 64, kind)) return JournalStoreResult::Invalid;
    UploadJournalStore<Io> store(io, a, b);
    const auto loaded = store.load(output, verify, scratch, capacity - 128);
    if (loaded != JournalStoreResult::Ready) return loaded;
    return uploadJournalValid(output) && output.phase == UploadPhase::Sealed && output.total <= uploadByteLimit(kind) && output.generation == generation &&
                   !std::memcmp(output.id, id.data, 16) && !std::memcmp(output.author_hash, author, 32)
               ? JournalStoreResult::Ready
               : JournalStoreResult::Conflict;
}
} // namespace track_sharing
