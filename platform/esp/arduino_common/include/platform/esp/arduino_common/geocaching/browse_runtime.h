#pragma once
#include <cstddef>
#include <cstdint>
namespace chat
{
class MeshAdapterRouter;
}
class LoraBoard;
namespace platform::esp::arduino_common::geocaching::browse_runtime
{
void configure(chat::MeshAdapterRouter& router, LoraBoard& board);
bool workPending();
enum class WebReadResult : uint8_t
{
    Pending,
    Ready,
    Invalid,
    Conflict,
    Unavailable,
    NotFound,
    Forbidden,
    AlreadySaved,
    HashMismatch,
    StaleIdentity,
    Absent // Authenticated selected directory explicitly returned cache not_found.
};
// A metadata-only lease; UI and browser share the one storage runtime.
bool webLease(bool enabled, bool require_storage = true);
// Runs under the existing session mutex only when persistence/network work is
// idle. Callback returns true after an identity write requiring reboot, causing
// the idle browse session to be discarded. False leaves its cached state intact.
bool withIdentityMaintenance(bool (*action)(void*), void* context);
// Copies the existing boot identifier and 64-bit monotonic clock; no new state.
bool publicationContext(uint8_t boot_id[16], uint64_t& monotonic_ms);
// One row from the existing authenticated directory discovery cache.
WebReadResult readWebDirectories(uint32_t offset, uint8_t* output, size_t capacity, size_t& written);
// Requests current signed record from the selected authenticated directory.
// Reuses the sole detail slot. Output: revision LE32 followed by revision hash[32].
// Ready releases the detail; explicit not_found returns Absent and zero output.
// Timeout/failure/unknown directory never qualifies as cache absence.
WebReadResult readWebLatestRevision(const uint8_t cache_id[32], const uint8_t directory[16], uint8_t* output, size_t capacity);
inline constexpr const char* kWebDraftCandidatePath = "/trailmate/geocaching.web.draft";
// Loads the candidate through the storage owner into the existing save arena.
WebReadResult queueWebDraftFile(const uint8_t id[16], uint32_t total, uint64_t expected_generation,
                                const uint8_t hash[32], const uint8_t author[64], uint32_t author_generation);
// Releases only a web job proven not to have entered a storage transaction.
bool releaseWebDraftSave();
// Checks the committed row with the shared read job. Caller supplies the
// authoritative device key; a missing row is allowed only for generation zero.
// Optional edit hash recognises a retry at expected+1 after a committed save.
WebReadResult checkWebDraftEditOwner(const uint8_t id[16], uint64_t expected_generation, const uint8_t author[64],
                                     const uint8_t* edit_hash = nullptr);
// One borrowed catalog row per response [catalog_generation, offset, known_count, row|null].
// Reuses the existing four-row UI projection; known_count is a lower bound.
WebReadResult readWebDraftCatalog(uint32_t offset, bool has_expected, uint64_t expected_generation,
                                  uint8_t* output, size_t capacity, size_t& written);
// Response [id16, generation64, total, offset, chunk]. No retained caller buffer.
WebReadResult readWebDraftChunk(const uint8_t id[16], uint32_t offset, uint16_t count, uint64_t expected_generation,
                                uint8_t* output, size_t capacity, size_t& written);
// Called only after explicit publication confirmation. Copies exact SignedCache
// bytes; verification and persistence run on the storage maintenance owner.
bool queuePublication(const uint8_t* signed_cache, size_t size);
// Local publication preview only; it does not prove the latest network revision.
// The selected directory must belong to the authenticated, capability-verified cache.
bool webPublicationPlan(const uint8_t draft_id[16], uint64_t generation, const uint8_t directory[16],
                        uint8_t author[64], uint32_t& from_revision, uint32_t& to_revision);
// Current in-memory operation only; NotFound does not prove absence from SD history.
// [operation_id, phase, cache|null, hash|null, revision, durable|null, transported|null, error|null].
// phase: 0 preparing,1 persisting,2 waiting,3 confirmed,4 stopped,5 failed,6 stopping.
WebReadResult readWebPublicationJob(const uint8_t operation_id[16], const uint8_t expected_author[64],
                                    uint8_t* output, size_t capacity, size_t& written);
// Verified, read-only SD projection; may return Pending while the shared
// reader advances. NotFound is not permission to repeat a prepare/commit.
WebReadResult readWebPublicationHistory(const uint8_t operation_id[16], const uint8_t expected_author[64],
                                        uint8_t* output, size_t capacity, size_t& written);
// Command-consumer stop of a matching current or verified saved attempt. Pending
// means the existing maintenance owner must finish submit/stop persistence.
// Ready preserves an already confirmed result or proves local stopping done.
WebReadResult requestWebPublicationStop(const uint8_t operation_id[16], const uint8_t expected_author[64]);
// Confirmation binding from the committed draft through the shared reader.
// Output: SHA256(draft bytes)[32], directory epoch[16], from/to revision LE32.
// Requires the selected draft's current catalog window; versions are local only.
WebReadResult readWebPublicationBinding(const uint8_t draft_id[16], uint64_t generation,
                                        const uint8_t directory[16], const uint8_t expected_author[64],
                                        uint8_t* output, size_t capacity);
// Internal commit primitive: HostLink must first validate its durable confirmation ticket.
// Reuses the sole publication job and signs on the existing maintenance owner.
// A supplied 56-byte binding is rechecked before admission; false can mean the
// shared draft reader is still pending. HostLink must always supply its binding.
// A nonzero operation_id pins both durable task and request identifiers; it
// occupies 16 bytes only in the existing on-demand PSRAM publication job.
bool queueWebDraftPublication(const uint8_t draft_id[16], uint64_t generation, const uint8_t directory[16],
                              const uint8_t expected_author[64], uint32_t expected_revision,
                              const uint8_t* expected_binding = nullptr, const uint8_t* operation_id = nullptr);
// Local draft transaction; expected_generation is zero for a new draft.
// editor_fields excludes caller-supplied author/base and restores both through editDraft.
bool queueDraftSave(const uint8_t draft_id[16], const uint8_t* encoded, size_t size, uint64_t expected_generation,
                    bool editor_fields = false);
// Exactly one operation slice, called by the existing storage maintenance owner.
void step();
WebReadResult queueWebSignature(const uint8_t operation[16], const uint8_t owner[16], const uint8_t transfer[16],
                                const uint8_t hash[32], uint32_t total, const uint8_t author[64], uint32_t generation,
                                const uint8_t* draft = nullptr, uint64_t draft_generation = 0);
WebReadResult readWebSignature(const uint8_t operation[16], const uint8_t owner[16], bool status_only,
                               uint32_t offset, uint16_t maximum, uint8_t* output, size_t capacity, size_t& written);
WebReadResult releaseWebSignature(const uint8_t owner[16], const uint8_t* operation = nullptr);
// Read-only recovery may expose correlation metadata only while no signing or
// confirmation job is using the shared input slot.
WebReadResult sharedSignatureUploadIdle();
// Runs under the existing storage mutex with signing/confirmation excluded;
// never applies identity-restoration side effects.
bool withSignatureInputRecovery(bool (*action)(void*), void* context);
WebReadResult queueWebConfirmation(const uint8_t transfer[16], const uint8_t owner[16], const uint8_t hash[32], uint32_t total,
                                   const uint8_t author[64], uint32_t generation, const uint8_t* proof, size_t proof_size);
WebReadResult readWebConfirmation(const uint8_t transfer[16], const uint8_t owner[16], uint8_t* output, size_t capacity, size_t& written);
WebReadResult releaseWebConfirmation(const uint8_t owner[16], const uint8_t* transfer = nullptr);
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
