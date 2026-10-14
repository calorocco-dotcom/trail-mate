#pragma once
#include "track_sharing/directory_capabilities.h"
#include "track_sharing/read_query_response.h"
#include "track_sharing/verify_read_manifest.h"
#include "track_sharing/region_read_response.h"

namespace track_sharing
{
// Borrowed expected context must remain immutable for this admitted operation.
struct ReadValidationContext
{
    geocaching::ByteView directory_public, directory_epoch;
    geocaching::ByteView object, revision, file_hash, query_binding;
    const ReadQuery* query = nullptr;
    const RegionReadScope* region = nullptr;
    uint32_t file_bytes = 0, offset = 0;
    uint16_t length = 0;
};

template <class Crypto, class Hash>
class ReadResponseValidator
{
  public:
    ReadResponseValidator(const ReadValidationContext& context, Crypto& crypto, Hash& hash,
                          VerifiedManifestView& manifest, ReadQueryRow& row, ReadQueryResponse& page,
                          uint8_t* workspace, size_t capacity, RegionReplyWorkspace* regions = nullptr)
        : context_(context), crypto_(crypto), hash_(hash), manifest_(manifest), row_(row), page_(page), workspace_(workspace), capacity_(capacity), regions_(regions) {}
    bool validate(uint8_t operation, geocaching::ByteView request, const ReadResponseView& envelope, geocaching::ByteView raw)
    {
        ReadResponseView checked;
        if (!decodeTrackReadEnvelope(raw, request, operation, checked) || checked.status != envelope.status ||
            checked.body.data != envelope.body.data || checked.body.size != envelope.body.size) return false;
        // Error diagnostics are fully decoded by the envelope validator.
        if (checked.status != 200) return true;
        if (operation == 0)
        {
            TrackDirectoryCapabilities caps;
            return readSpan(context_.directory_public, 64) && readSpan(context_.directory_epoch, 16) &&
                   decodeTrackCapabilities(raw, request, caps) && !std::memcmp(caps.public_key.data, context_.directory_public.data, 64) &&
                   !std::memcmp(caps.epoch.data, context_.directory_epoch.data, 16) && (caps.operations & 0x107) == 0x107;
        }
        if (operation == 1) return context_.query && decodeTrackReadQueryResponse(raw, request, *context_.query, context_.query_binding, row_, page_);
        if (operation == 2) return verifyReadManifest(raw, request, context_.object, context_.revision, crypto_, hash_, workspace_, capacity_, manifest_) == ReadManifestVerification::Valid;
        if (operation == 8 || operation == 12)
        {
            if (operation == 12 && (!context_.region || !regionVersion(context_.region->version) || !regionInNamespace(context_.region->id,context_.region->name_space))) return false;
            ReadFileResponse file;
            return decodeTrackReadFileResponse(raw, request, context_.file_hash, context_.file_bytes, context_.offset, context_.length, file, operation);
        }
        if (operation == 9) return regions_ && decodeRegionCapabilities(raw, request, regions_->capabilities);
        if (operation >= 10 && operation <= 13)
        {
            if (!context_.region) return false;
            const auto& region = *context_.region;
            if (operation == 10) return regions_ && context_.query && decodeRegionChildren(raw,request,region.version,region.name_space,region.parent,region.after,
                region.parent_level,region.depth,context_.query->limit,regions_->row,regions_->page);
            if (operation == 11) return regions_ && decodeRegionDescriptor(raw,request,region.id,region.version,region.name_space,region.depth,regions_->descriptor);
            if (operation == 13) return context_.query && regionVersion(region.version) && regionInNamespace(region.id,region.name_space) &&
                decodeRegionQueryResponse(raw,request,*context_.query,context_.query_binding,row_,page_);
        }
        return false;
    }

  private:
    const ReadValidationContext& context_;
    Crypto& crypto_;
    Hash& hash_;
    VerifiedManifestView& manifest_;
    ReadQueryRow& row_;
    ReadQueryResponse& page_;
    uint8_t* workspace_;
    size_t capacity_;
    RegionReplyWorkspace* regions_;
};
} // namespace track_sharing
