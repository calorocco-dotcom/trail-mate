#pragma once
#include "track_sharing/upload_journal.h"
namespace track_sharing
{
enum class HeadPhase : uint8_t
{
    Signed,
    Committed
};
struct ObjectHead
{
    uint8_t object[32]{}, hash[32]{}, parent[32]{}, operation[16]{};
    uint64_t sequence = 0;
    uint32_t generation = 0, revision = 0;
    HeadPhase phase = HeadPhase::Signed;
    bool archived = false;
};
static_assert(sizeof(ObjectHead) <= 144, "Object head cannot own signed manifest bytes");
constexpr size_t kObjectHeadBytes = 136;
inline bool objectHeadValid(const ObjectHead& head)
{
    uint8_t object = 0, hash = 0, parent = 0, operation = 0;
    for (unsigned i = 0; i < 32; ++i)
    {
        object |= head.object[i];
        hash |= head.hash[i];
        parent |= head.parent[i];
    }
    for (auto byte : head.operation) operation |= byte;
    return object && hash && operation && head.sequence && head.generation && head.revision &&
           (head.revision == 1 ? !parent && !head.archived : parent != 0) && static_cast<uint8_t>(head.phase) <= 1;
}
// Structural transition only. Caller must verify signed manifest and durable
// task admission separately. Committed is local admission, not network success.
inline bool objectHeadAdvanceValid(const ObjectHead& current, const ObjectHead& next)
{
    if (!objectHeadValid(current) || !objectHeadValid(next) || current.sequence == UINT64_MAX || next.sequence != current.sequence + 1 ||
        std::memcmp(current.object, next.object, 32)) return false;
    // A restored identity may adopt its own verified committed history. Only
    // metadata changes here; the caller must authenticate the original author.
    if (current.generation != next.generation)
        return current.phase == HeadPhase::Committed && next.phase == HeadPhase::Committed && current.revision == next.revision &&
               current.archived == next.archived && !std::memcmp(current.hash, next.hash, 32) && !std::memcmp(current.parent, next.parent, 32) &&
               !std::memcmp(current.operation, next.operation, 16);
    if (current.revision == next.revision)
        return current.archived == next.archived && !std::memcmp(current.hash, next.hash, 32) && !std::memcmp(current.parent, next.parent, 32) &&
               !std::memcmp(current.operation, next.operation, 16) && (current.phase == next.phase || (current.phase == HeadPhase::Signed && next.phase == HeadPhase::Committed));
    return current.phase == HeadPhase::Committed && next.phase == HeadPhase::Signed && current.revision != UINT32_MAX && next.revision == current.revision + 1 &&
           !(current.archived && !next.archived) && !std::memcmp(next.parent, current.hash, 32) &&
           std::memcmp(next.hash, current.hash, 32) && std::memcmp(next.operation, current.operation, 16);
}
// TMHD/v1/object32/hash32/parent32/op16/generationLE32/revisionLE32/
// flags(phase bit0, archived bit1)/sequenceLE64/CRC16. CRC is not authenticity.
inline bool encodeObjectHead(const ObjectHead& head, uint8_t* output, size_t capacity)
{
    if (!output || capacity < kObjectHeadBytes || !objectHeadValid(head)) return false;
    const auto a = reinterpret_cast<uintptr_t>(&head), b = reinterpret_cast<uintptr_t>(output);
    if (a <= b ? b - a < sizeof(head) : a - b < kObjectHeadBytes) return false;
    std::memcpy(output, "TMHD", 4);
    output[4] = 1;
    std::memcpy(output + 5, head.object, 32);
    std::memcpy(output + 37, head.hash, 32);
    std::memcpy(output + 69, head.parent, 32);
    std::memcpy(output + 101, head.operation, 16);
    for (unsigned i = 0; i < 4; ++i)
    {
        output[117 + i] = uint8_t(head.generation >> (8 * i));
        output[121 + i] = uint8_t(head.revision >> (8 * i));
    }
    output[125] = uint8_t(head.phase) | (head.archived ? 2 : 0);
    for (unsigned i = 0; i < 8; ++i) output[126 + i] = uint8_t(head.sequence >> (8 * i));
    const auto crc = uploadJournalCrc(output, 134);
    output[134] = uint8_t(crc);
    output[135] = uint8_t(crc >> 8);
    return true;
}
inline bool decodeObjectHead(const uint8_t* input, size_t size, ObjectHead& head)
{
    const auto a = reinterpret_cast<uintptr_t>(input), b = reinterpret_cast<uintptr_t>(&head);
    if (input && (a <= b ? b - a < size : a - b < sizeof(head))) return false;
    head = {};
    if (!input || size != kObjectHeadBytes || std::memcmp(input, "TMHD", 4) || input[4] != 1 || input[125] > 3 ||
        uploadJournalCrc(input, 134) != uint16_t(input[134] | uint16_t(input[135]) << 8)) return false;
    std::memcpy(head.object, input + 5, 32);
    std::memcpy(head.hash, input + 37, 32);
    std::memcpy(head.parent, input + 69, 32);
    std::memcpy(head.operation, input + 101, 16);
    for (unsigned i = 0; i < 4; ++i)
    {
        head.generation |= uint32_t(input[117 + i]) << (8 * i);
        head.revision |= uint32_t(input[121 + i]) << (8 * i);
    }
    for (unsigned i = 0; i < 8; ++i) head.sequence |= uint64_t(input[126 + i]) << (8 * i);
    head.phase = static_cast<HeadPhase>(input[125] & 1);
    head.archived = (input[125] & 2) != 0;
    if (!objectHeadValid(head))
    {
        head = {};
        return false;
    }
    return true;
}
} // namespace track_sharing
