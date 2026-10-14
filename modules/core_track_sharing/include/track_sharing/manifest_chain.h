#pragma once
#include "track_sharing/manifest.h"
#include <cstring>
namespace track_sharing
{
// Structural predecessor rules only. Both views must have been decoded and
// signature-verified; parent_hash must come from the domain hash of its raw
// bytes. This function does not confer signature authenticity/currentness.
inline bool validateSuccessor(const ManifestView& parent, const ManifestView& child, const uint8_t parent_hash[32])
{
    if (!parent_hash || !parent.raw.data || !child.raw.data || parent.author.size != 64 || child.author.size != 64 ||
        parent.nonce.size != 16 || child.nonce.size != 16 || child.previous.size != 32 ||
        !parent.author.data || !child.author.data || !parent.nonce.data || !child.nonce.data || !child.previous.data ||
        parent.revision == UINT32_MAX || child.revision != parent.revision + 1 ||
        std::memcmp(parent.author.data, child.author.data, 64) || std::memcmp(parent.nonce.data, child.nonce.data, 16) ||
        std::memcmp(child.previous.data, parent_hash, 32) || parent.created != child.created || child.updated < parent.updated ||
        (parent.state == 1 && child.state == 0) || child.version < parent.version) return false;
    if (child.state == 1 && (parent.file_hash.size != 32 || child.file_hash.size != 32 || !parent.file_hash.data || !child.file_hash.data ||
                             std::memcmp(parent.file_hash.data, child.file_hash.data, 32) || parent.file_bytes != child.file_bytes)) return false;
    if (child.state == 1 && child.version == 2)
    {
        if (!child.extensions_offset || child.extensions_offset >= child.raw.size) return false;
        // After skipping metadata, compare the canonical assets binding alone;
        // no automatic extension views or attachment table are needed.
        geocaching::protocol::CmpReader next({child.raw.data + child.extensions_offset, child.raw.size - child.extensions_offset});
        const geocaching::ByteView next_tail{child.raw.data + child.extensions_offset, child.raw.size - child.extensions_offset};
        if (!readManifestMetadata(next, next_tail, child.points)) return false;
        const geocaching::ByteView next_assets{next_tail.data + next.position(), next_tail.size - next.position()};
        if (parent.version == 1)
        {
            if (next_assets.size != 1 || next_assets.data[0] != 0xc0) return false;
        }
        else
        {
            if (!parent.extensions_offset || parent.extensions_offset >= parent.raw.size) return false;
            const geocaching::ByteView old_tail{parent.raw.data + parent.extensions_offset, parent.raw.size - parent.extensions_offset};
            geocaching::protocol::CmpReader old(old_tail);
            if (!readManifestMetadata(old, old_tail, parent.points) || old_tail.size - old.position() != next_assets.size ||
                std::memcmp(old_tail.data + old.position(), next_assets.data, next_assets.size)) return false;
        }
    }
    return true;
}
} // namespace track_sharing
