#include "hostlink/route_install_authorization.h"
#include <cassert>
using namespace hostlink::management;
int main()
{
    uint8_t id[16]{1}, digest[32]{2}, author[32]{3};
    RouteInstallView request{{id, 16}, {digest, 32}, 1024, 7};
    track_sharing::UploadJournal journal;
    std::memcpy(journal.id, id, 16);
    std::memcpy(journal.file_hash, digest, 32);
    std::memcpy(journal.author_hash, author, 32);
    journal.generation = 7;
    journal.sequence = 1;
    journal.total = journal.durable_offset = 1024;
    journal.phase = track_sharing::UploadPhase::Sealed;
    const auto authorize = [&](bool session = true, bool media = true, uint32_t generation = 7)
    {
        return authorizeRouteInstallUpload(request, journal, track_sharing::UploadKind::Kml, generation, {author, 32}, session, media);
    };
    assert(authorize() == Status::Ok);
    assert(authorize(false) == Status::Unauthorized);
    assert(authorize(true, false) == Status::StorageUnavailable);
    assert(authorize(true, true, 8) == Status::StaleIdentity);
    author[0] ^= 1;
    assert(authorize() == Status::StaleIdentity);
    author[0] ^= 1;
    journal.generation = 8;
    assert(authorize() == Status::StaleIdentity);
    journal.generation = 7;
    journal.phase = track_sharing::UploadPhase::Receiving;
    assert(authorize() == Status::Conflict);
    journal.phase = track_sharing::UploadPhase::Sealed;
    journal.id[0] ^= 2;
    assert(authorize() == Status::Conflict);
    journal.id[0] ^= 2;
    digest[0] ^= 1;
    assert(authorize() == Status::HashMismatch);
    digest[0] ^= 1;
    request.bytes = 1023;
    assert(authorize() == Status::Conflict);
    request.bytes = 1024;
    assert(authorizeRouteInstallUpload(request, journal, track_sharing::UploadKind::Manifest, 7, {author, 32}, true, true) == Status::InvalidField);
    journal.sequence = 0;
    assert(authorize() == Status::InvalidField);
    journal.sequence = 1;
    assert(authorize() == Status::Ok && journal.sequence == 1 && journal.total == 1024 && request.generation == 7);
}
