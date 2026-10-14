#include "track_sharing/upload_journal.h"
#include <cassert>
int main()
{
    using namespace track_sharing;
    UploadJournal value, decoded;
    value.id[0] = 1;
    value.author_hash[0] = 2;
    value.file_hash[0] = 3;
    value.sequence = 0x100000001ULL;
    value.generation = 7;
    value.total = 8388608;
    value.durable_offset = 320;
    uint8_t raw[108];
    assert(encodeUploadJournal(value, raw, sizeof(raw)) && decodeUploadJournal(raw, sizeof(raw), decoded));
    assert(decoded.sequence == value.sequence && decoded.total == value.total && decoded.durable_offset == 320);
    for (unsigned i = 0; i < sizeof(raw); ++i)
    {
        raw[i] ^= 1;
        assert(!decodeUploadJournal(raw, sizeof(raw), decoded));
        raw[i] ^= 1;
    }
    value.phase = UploadPhase::Checking;
    assert(!encodeUploadJournal(value, raw, sizeof(raw)));
    value.durable_offset = value.total;
    assert(encodeUploadJournal(value, raw, sizeof(raw)));
    value.phase = UploadPhase::Sealed;
    assert(encodeUploadJournal(value, raw, sizeof(raw)) && decodeUploadJournal(raw, sizeof(raw), decoded) && decoded.phase == UploadPhase::Sealed);
    value.generation = 0;
    assert(!encodeUploadJournal(value, raw, sizeof(raw)));
    assert(!decodeUploadJournal(raw, sizeof(raw) - 1, decoded));
}
