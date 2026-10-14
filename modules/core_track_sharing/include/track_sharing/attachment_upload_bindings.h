#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "track_sharing/sign_manifest.h"
namespace track_sharing
{
constexpr size_t kAttachmentBindingsBytes = 1100;
struct AttachmentUploadBindingsView
{
    geocaching::ByteView index_id, ids;
};
// [1,index_upload_id:bin16,packed_resource_ids:bin(count*16)]. Zero slots
// explicitly omit optional resources; indexed order is bound by index SHA.
inline bool decodeAttachmentUploadBindings(geocaching::ByteView raw, uint8_t count, AttachmentUploadBindingsView& output)
{
    if (signingSpansOverlap(raw.data, raw.size, reinterpret_cast<const uint8_t*>(&output), sizeof(output))) return false;
    output = {};
    if (!raw.data || !raw.size || raw.size > kAttachmentBindingsBytes || !count || count > 64) return false;
    geocaching::protocol::CmpReader reader(raw);
    size_t fields = 0;
    uint64_t version = 0;
    if (!reader.array(fields, 3) || fields != 3 || !reader.unsignedInteger(version) || version != 1 ||
        !reader.binary(output.index_id, 16) || output.index_id.size != 16 || !reader.binary(output.ids, 1024) ||
        output.ids.size != size_t(count) * 16 || !reader.finished()) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < 16; ++i) bits |= output.index_id.data[i];
    return bits != 0;
}
} // namespace track_sharing
