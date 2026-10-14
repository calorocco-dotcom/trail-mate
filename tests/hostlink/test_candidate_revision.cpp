#include "geocaching/protocol/cmp_writer.h"
#include "track_sharing/candidate_revision.h"
#include <cassert>
int main()
{
    uint8_t raw[512], author[64] = {1}, nonce[16] = {2}, hash[32] = {3}, file[32] = {4};
    track_sharing::VerifiedManifestView parent;
    parent.verified = true;
    parent.manifest.raw = {file, 1};
    parent.manifest.author = {author, 64};
    parent.manifest.nonce = {nonce, 16};
    parent.manifest.revision = 1;
    parent.manifest.created = 100;
    parent.manifest.updated = 100;
    parent.manifest.file_hash = {file, 32};
    parent.manifest.file_bytes = 200;
    std::memcpy(parent.revision_hash, hash, 32);
    track_sharing::ManifestView candidate;
    for (unsigned mode = 0; mode < 8; ++mode)
    {
        const unsigned revision = mode < 3 ? mode + 1 : 2;
        const unsigned state = mode >= 3 && mode <= 5 ? 1 : 0;
        parent.manifest.state = mode == 6 ? 1 : 0;
        uint8_t child_file[32], previous[32];
        std::memcpy(child_file, file, 32);
        std::memcpy(previous, hash, 32);
        if (mode == 4) child_file[31] ^= 1;
        if (mode == 7) previous[31] ^= 1;
        geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
        assert(writer.array(14) && writer.unsignedInteger(1) && writer.binary({author, 64}) && writer.binary({nonce, 16}) && writer.unsignedInteger(0) && writer.unsignedInteger(revision));
        assert(revision == 1 ? writer.nil() : writer.binary({previous, 32}));
        assert(writer.unsignedInteger(state) && writer.unsignedInteger(100) && writer.unsignedInteger(101) && writer.text("Track") && writer.text(""));
        assert(writer.array(4));
        for (unsigned i = 0; i < 4; ++i) assert(writer.unsignedInteger(0));
        assert(writer.array(4) && writer.unsignedInteger(0) && writer.nil() && writer.nil() && writer.unsignedInteger(1));
        assert(writer.array(4) && writer.binary({child_file, 32}) && writer.unsignedInteger(mode == 5 ? 201 : 200) && writer.text("application/vnd.google-earth.kml+xml") && writer.unsignedInteger(1));
        const geocaching::ByteView bytes{raw, writer.size()};
        assert(track_sharing::validateCandidateRevision(bytes, {author, 64}, nullptr, candidate) == (mode == 0));
        assert(track_sharing::validateCandidateRevision(bytes, {author, 64}, &parent, candidate) == (mode == 1 || mode == 3));
        assert(!track_sharing::validateCandidateRevision(bytes, {author, 64}, &parent, parent.manifest) && parent.manifest.revision == 1);
        parent.verified = false;
        assert(!track_sharing::validateCandidateRevision(bytes, {author, 64}, &parent, candidate));
        parent.verified = true;
        author[63] ^= 1;
        assert(!track_sharing::validateCandidateRevision(bytes, {author, 64}, &parent, candidate));
        author[63] ^= 1;
        assert(!track_sharing::validateCandidateRevision({raw, writer.size() - 1}, {author, 64}, &parent, candidate));
    }
}
