#pragma once
#include "geocaching/storage/pending_request.h"

namespace geocaching::storage
{
struct PublicationRecoveryFilter
{
    Destination local, remote;
    GeocacheId cache;
    RevisionHash hash;
    std::array<uint8_t, 16> operation{};
    bool has_remote = false, has_cache = false, has_hash = false;
    bool has_operation : 1;
    bool include_terminal : 1;
    PublicationRecoveryFilter() : has_operation(false), include_terminal(false) {}

    bool matchesRequest(ByteView key) const
    {
        return key.data && key.size == 48 && !std::memcmp(key.data, local.bytes.data(), 16) &&
               (!has_remote || !std::memcmp(key.data + 16, remote.bytes.data(), 16)) &&
               (!has_operation || !std::memcmp(key.data + 32, operation.data(), operation.size()));
    }
    bool same(const PublicationRecoveryFilter& other) const
    {
        return local.bytes == other.local.bytes && has_remote == other.has_remote && has_cache == other.has_cache && has_hash == other.has_hash &&
               has_operation == other.has_operation && include_terminal == other.include_terminal && (!has_operation || operation == other.operation) &&
               (!has_remote || remote.bytes == other.remote.bytes) && (!has_cache || cache.bytes == other.cache.bytes) && (!has_hash || hash.bytes == other.hash.bytes);
    }
};
static_assert(sizeof(PublicationRecoveryFilter) <= 116, "Recovery filter must not grow beyond the operation binding");
// Metadata is owned. Request/response borrow the read lease until released.
struct PublicationRecoveryView
{
    std::array<uint8_t, 48> key{};
    std::array<uint8_t, 16> task{};
    GeocacheId cache;
    RevisionHash hash;
    StoredTime created;
    ByteView request, response;
    bool confirmed = false;
    uint8_t outgoing_state = 0, task_state = 0;
    bool continue_intent = false;
};
static_assert(sizeof(PublicationRecoveryView) == ((offsetof(PublicationRecoveryView, confirmed) + 1 + alignof(PublicationRecoveryView) - 1) / alignof(PublicationRecoveryView)) * alignof(PublicationRecoveryView),
              "History evidence must fit the existing recovery metadata padding");
enum class PublicationRecoveryResult : uint8_t
{
    None,
    Ready,
    Invalid
};

inline PublicationRecoveryResult publicationRecoveryCandidate(const PublicationRecoveryFilter& filter, ByteView key,
                                                              const OutgoingView& outgoing, const TaskView& task,
                                                              PublicationRecoveryView& out)
{
    // Terminal rows are available only in explicit, operation-bound read-only
    // history mode. Background recovery must never turn a stop into a resend.
    if (filter.include_terminal && !filter.has_operation) return PublicationRecoveryResult::Invalid;
    if (!filter.matchesRequest(key) || task.kind != 1) return PublicationRecoveryResult::None;
    if (filter.has_operation && (!outgoing.task_id.data || outgoing.task_id.size != 16 ||
                                 std::memcmp(outgoing.task_id.data, filter.operation.data(), 16)))
        return PublicationRecoveryResult::Invalid;
    if (!requestBelongsToTask(outgoing.task_id, task, key, outgoing) || task.cache_id.size != 32 || task.revision_hash.size != 32)
        return PublicationRecoveryResult::Invalid;
    if ((filter.has_cache && std::memcmp(task.cache_id.data, filter.cache.bytes.data(), 32)) ||
        (filter.has_hash && std::memcmp(task.revision_hash.data, filter.hash.bytes.data(), 32))) return PublicationRecoveryResult::None;
    if (outgoing.state > 5 || task.state > 5) return PublicationRecoveryResult::Invalid;
    if (!filter.include_terminal && (outgoing.state > 4 || (outgoing.state == 4 && !filter.has_cache && !filter.has_operation) ||
                                     (outgoing.state < 4 && (!outgoing.continue_intent || !task.continue_intent || task.state == 3 || task.state == 5))))
        return PublicationRecoveryResult::None;
    std::memcpy(out.key.data(), key.data, 48);
    std::memcpy(out.task.data(), outgoing.task_id.data, 16);
    std::memcpy(out.cache.bytes.data(), task.cache_id.data, 32);
    std::memcpy(out.hash.bytes.data(), task.revision_hash.data, 32);
    out.created = outgoing.created;
    out.confirmed = outgoing.state == 4;
    out.outgoing_state = outgoing.state;
    out.task_state = task.state;
    out.continue_intent = outgoing.continue_intent && task.continue_intent;
    out.request = out.response = {};
    return PublicationRecoveryResult::Ready;
}
inline bool preferPublicationRecovery(const PublicationRecoveryView& candidate, const PublicationRecoveryView& selected)
{
    if (candidate.confirmed != selected.confirmed) return candidate.confirmed;
    return candidate.key < selected.key;
}
// Filter terminal/no-intent requests before opening their task. Exact cache
// lookups retain confirmed receipts; background recovery only resumes work.
inline bool publicationRecoveryRequestEligible(const PublicationRecoveryFilter& filter, const OutgoingView& outgoing)
{
    if (filter.include_terminal) return filter.has_operation && outgoing.state <= 5;
    return outgoing.state <= 4 && (outgoing.state == 4 ? filter.has_cache || filter.has_operation : outgoing.continue_intent);
}
template <class View>
PublicationRecoveryResult readLogicalPublicationRecovery(const View& view, const PublicationRecoveryFilter& filter,
                                                         PublicationRecoveryView& out)
{
    out = {};
    if (filter.include_terminal && !filter.has_operation) return PublicationRecoveryResult::Invalid;
    size_t cursor = 0;
    MutationView row;
    bool found = false;
    while (view.next(cursor, row))
    {
        if (row.table != 5 || !filter.matchesRequest(row.key)) continue;
        OutgoingView outgoing;
        TaskView task;
        ByteView value;
        if (!decodeOutgoing(row.key, row.value, outgoing)) return PublicationRecoveryResult::Invalid;
        if (!publicationRecoveryRequestEligible(filter, outgoing)) continue;
        if (!view.find(10, outgoing.task_id, value) ||
            !decodeTask(outgoing.task_id, value, task)) return PublicationRecoveryResult::Invalid;
        PublicationRecoveryView candidate;
        const auto result = publicationRecoveryCandidate(filter, row.key, outgoing, task, candidate);
        if (result == PublicationRecoveryResult::Invalid) return result;
        if (result == PublicationRecoveryResult::Ready && (!found || preferPublicationRecovery(candidate, out)))
        {
            out = candidate;
            out.request = outgoing.request;
            out.response = candidate.confirmed ? outgoing.terminal_data : ByteView{};
            found = true;
        }
    }
    return found ? PublicationRecoveryResult::Ready : PublicationRecoveryResult::None;
}
} // namespace geocaching::storage
