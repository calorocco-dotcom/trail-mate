#include "track_sharing/read_response_validator.h"
#include <array>
#include <cassert>

struct Crypto
{
    track_sharing::ManifestVerification verify(geocaching::ByteView, geocaching::ByteView, geocaching::ByteView) { return track_sharing::ManifestVerification::CryptoUnavailable; }
};
struct Hash
{
    bool start() { return false; }
    bool update(const uint8_t*, size_t) { return false; }
    bool finish(uint8_t*) { return false; }
    void reset() {}
};
static track_sharing::VerifiedManifestView manifest;
static track_sharing::ReadQueryRow row;
static track_sharing::ReadQueryResponse page;
static std::array<uint8_t, 4200> workspace;
static track_sharing::RegionReplyWorkspace regional_workspace;
static track_sharing::RegionReadScope regional_scope;
static track_sharing::ReadQuery query;
int main()
{
    std::array<uint8_t, 16> request{}, epoch{};
    std::array<uint8_t, 64> public_key{};
    std::array<uint8_t, 32> file_hash{};
    std::array<uint8_t, 512> raw{};
    request.fill(1);
    epoch.fill(2);
    public_key.fill(3);
    file_hash.fill(4);
    const geocaching::ByteView id{request.data(), request.size()};
    track_sharing::ReadValidationContext context;
    context.directory_public = {public_key.data(), public_key.size()};
    context.directory_epoch = {epoch.data(), epoch.size()};
    context.file_hash = {file_hash.data(), file_hash.size()};
    context.file_bytes = 3;
    context.length = 3;
    Crypto crypto;
    Hash hash;
    track_sharing::ReadResponseValidator<Crypto, Hash> validator(context, crypto, hash, manifest, row, page, workspace.data(), workspace.size());
    auto envelope = [&](geocaching::protocol::CmpWriter& writer, unsigned operation, unsigned status, unsigned fields)
    { return writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(operation) && writer.binary(id) && writer.unsignedInteger(status) && writer.array(fields); };
    auto caps = [&]
    {geocaching::protocol::CmpWriter writer(raw.data(),raw.size());assert(envelope(writer,0,200,8) && writer.unsignedInteger(1) && writer.text(track_sharing::kApplicationType) && writer.binary(context.directory_public) && writer.binary(context.directory_epoch) && writer.unsignedInteger(8388608) && writer.unsignedInteger(1024) && writer.unsignedInteger(32) && writer.array(4));for(const unsigned op:{0,1,2,8})assert(writer.unsignedInteger(op));return writer.size(); };
    size_t length = caps();
    track_sharing::ReadResponseView decoded;
    assert(track_sharing::decodeTrackReadEnvelope({raw.data(), length}, id, 0, decoded) && validator.validate(0, id, decoded, {raw.data(), length}));
    public_key[0] ^= 1;
    assert(!validator.validate(0, id, decoded, {raw.data(), length}));
    public_key[0] ^= 1;
    epoch[0] ^= 1;
    assert(!validator.validate(0, id, decoded, {raw.data(), length}));
    epoch[0] ^= 1;
    geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
    const uint8_t data[] = {1, 2, 3};
    assert(envelope(writer, 8, 200, 4) && writer.binary(context.file_hash) && writer.unsignedInteger(3) && writer.unsignedInteger(0) && writer.binary({data, 3}));
    length = writer.size();
    assert(track_sharing::decodeTrackReadEnvelope({raw.data(), length}, id, 8, decoded) && validator.validate(8, id, decoded, {raw.data(), length}));
    context.offset = 1;
    assert(!validator.validate(8, id, decoded, {raw.data(), length}));
    context.offset = 0;
    decoded.status = 422;
    assert(!validator.validate(8, id, decoded, {raw.data(), length}));
    track_sharing::ReadResponseValidator<Crypto, Hash> regional(context, crypto, hash, manifest, row, page, workspace.data(), workspace.size(), &regional_workspace);
    {
        geocaching::protocol::CmpWriter response(raw.data(), raw.size());
        assert(envelope(response, 9, 200, 8) && response.unsignedInteger(1) && response.text("trailmate.track.regions.v1") && response.text("test") && response.binary(context.file_hash) &&
               response.unsignedInteger(3) && response.unsignedInteger(32) && response.unsignedInteger(1024) && response.array(5));
        for (unsigned op = 9; op <= 13; op++) assert(response.unsignedInteger(op));
        length = response.size();
        assert(track_sharing::decodeTrackReadEnvelope({raw.data(), length}, id, 9, decoded));
        assert(!validator.validate(9, id, decoded, {raw.data(), length}));
        assert(regional.validate(9, id, decoded, {raw.data(), length}) && regional_workspace.capabilities.depth == 3);
    }
    regional_scope.name_space = "test";
    regional_scope.id = "test:r";
    regional_scope.version = context.file_hash;
    regional_scope.depth = 3;
    context.region = &regional_scope;
    context.query = &query;
    {
        geocaching::protocol::CmpWriter response(raw.data(), raw.size());
        assert(envelope(response, 10, 200, 3) && response.binary(context.file_hash) && response.nil() && response.array(0));
        length = response.size();
        assert(track_sharing::decodeTrackReadEnvelope({raw.data(), length}, id, 10, decoded) && regional.validate(10, id, decoded, {raw.data(), length}));
        context.region = nullptr;
        assert(!regional.validate(10, id, decoded, {raw.data(), length}));
        context.region = &regional_scope;
    }
    {
        geocaching::protocol::CmpWriter response(raw.data(), raw.size());
        assert(envelope(response, 11, 200, 3) && response.binary(context.file_hash) && response.array(7) && response.text("test:r") && response.nil() && response.unsignedInteger(0) && response.text("Root") &&
               response.array(0) && response.array(4) && response.signedInteger(0) && response.signedInteger(0) && response.signedInteger(1) && response.signedInteger(1) && response.unsignedInteger(0) &&
               response.array(3) && response.text("region-e7-json-v1") && response.binary(context.file_hash) && response.unsignedInteger(100));
        length = response.size();
        assert(track_sharing::decodeTrackReadEnvelope({raw.data(), length}, id, 11, decoded) && regional.validate(11, id, decoded, {raw.data(), length}));
        regional_scope.id = "test:other";
        assert(!regional.validate(11, id, decoded, {raw.data(), length}));
        regional_scope.id = "test:r";
    }
    {
        geocaching::protocol::CmpWriter response(raw.data(), raw.size());
        assert(envelope(response, 12, 200, 4) && response.binary(context.file_hash) && response.unsignedInteger(3) && response.unsignedInteger(0) && response.binary({data, 3}));
        length = response.size();
        assert(track_sharing::decodeTrackReadEnvelope({raw.data(), length}, id, 12, decoded) && regional.validate(12, id, decoded, {raw.data(), length}));
        context.region = nullptr;
        assert(!regional.validate(12, id, decoded, {raw.data(), length}));
        context.region = &regional_scope;
    }
    {
        geocaching::protocol::CmpWriter response(raw.data(), raw.size());
        context.query_binding = context.file_hash;
        assert(envelope(response, 13, 200, 3) && response.unsignedInteger(9007199254740993ULL) && response.nil() && response.array(0));
        length = response.size();
        assert(track_sharing::decodeTrackReadEnvelope({raw.data(), length}, id, 13, decoded) && regional.validate(13, id, decoded, {raw.data(), length}));
        query.has_snapshot = true;
        query.snapshot = 9007199254740994ULL;
        assert(!regional.validate(13, id, decoded, {raw.data(), length}));
    }
}
