#include "platform/esp/arduino_common/hostlink/publication_ticket_store.h"
#include <array>
#include <cassert>

int main()
{
    using namespace hostlink::management;
    using namespace platform::esp::arduino_common::storage;
    std::array<uint8_t, 16> owner{}, token{}, boot{}, draft{}, directory{}, epoch{};
    std::array<uint8_t, 64> author{};
    std::array<uint8_t, 32> hash{};
    owner[0] = 1;
    token[0] = 2;
    boot[0] = 3;
    draft[0] = 4;
    author[0] = 5;
    directory[0] = 6;
    epoch[0] = 7;
    hash[0] = 8;
    // A monotonic 64-bit clock cannot resurrect a ticket after 32-bit millis wrap.
    const uint64_t clock = uint64_t(UINT32_MAX) + 50;
    PublicationTicketFields fields{owner.data(), token.data(), boot.data(), draft.data(), author.data(),
                                   directory.data(), epoch.data(), hash.data(), UINT64_MAX, 9, 3, 4,
                                   UINT32_MAX - 30, 120};
    std::array<uint8_t, 512> scratch{};
    assert(!encodePublicationTicket(fields, scratch.data(), kPublicationTicketBytes - 1));
    assert(encodePublicationTicket(fields, scratch.data(), scratch.size()));
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, clock, true) == Status::Ok);
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, clock + 40, true) == Status::Expired);
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, clock + 39, true) == Status::Ok);
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 50, true) == Status::Expired);
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, clock + (uint64_t(1) << 32), true) == Status::Expired);
    owner[0] ^= 1;
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 50, true) == Status::Unauthorized);
    owner[0] ^= 1;
    token[0] ^= 1;
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 50, true) == Status::Unauthorized);
    token[0] ^= 1;
    boot[0] ^= 1;
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 50, true) == Status::Conflict);
    boot[0] ^= 1;
    author[0] ^= 1;
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 50, true) == Status::StaleIdentity);
    author[0] ^= 1;
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 10, 50, true) == Status::StaleIdentity);
    const auto original = scratch;
    for (size_t i = 0; i < kPublicationTicketBytes; ++i)
    {
        scratch = original;
        scratch[i] ^= 1;
        assert(!validatePublicationTicket(scratch.data(), kPublicationTicketBytes));
    }
    scratch = original;
    assert(!advancePublicationTicket(scratch.data(), kPublicationTicketBytes, PublicationTicketPhase::Accepted));
    assert(savePublicationTicket(scratch.data(), kPublicationTicketBytes));
    const auto prepared_disk = fake_files.at(kPublicationTicketPath);
    assert(advancePublicationTicket(scratch.data(), kPublicationTicketBytes, PublicationTicketPhase::Intent));
    fake_write_failure = true;
    assert(!savePublicationTicket(scratch.data(), kPublicationTicketBytes));
    assert(fake_files.at(kPublicationTicketPath) == prepared_disk);
    fake_write_failure = false;
    assert(savePublicationTicket(scratch.data(), kPublicationTicketBytes));
    scratch.fill(0);
    assert(loadPublicationTicket(scratch.data(), scratch.size()));
    assert(scratch[4] == 1);
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 1000, false) == Status::Ok);
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 50, true) == Status::Conflict);
    assert(!advancePublicationTicket(scratch.data(), kPublicationTicketBytes, PublicationTicketPhase::Prepared));
    assert(advancePublicationTicket(scratch.data(), kPublicationTicketBytes, PublicationTicketPhase::Accepted));
    assert(savePublicationTicket(scratch.data(), kPublicationTicketBytes));
    assert(advancePublicationTicket(scratch.data(), kPublicationTicketBytes, PublicationTicketPhase::Accepted));
    fake_files[kPublicationTicketPath][53] ^= 1;
    assert(!loadPublicationTicket(scratch.data(), scratch.size()));
    fake_files[kPublicationTicketPath] = prepared_disk.substr(0, 40);
    assert(!loadPublicationTicket(scratch.data(), scratch.size()));
    fake_available = false;
    assert(!loadPublicationTicket(scratch.data(), scratch.size()));
    fake_available = true;
    fields.to_revision = 5;
    assert(!encodePublicationTicket(fields, scratch.data(), scratch.size()));
    fields.to_revision = 4;
    fields.lifetime_ms = kPublicationTicketLifetimeMs + 1;
    assert(!encodePublicationTicket(fields, scratch.data(), scratch.size()));
    fields.lifetime_ms = 120;
    fields.author_generation = 0;
    assert(!encodePublicationTicket(fields, scratch.data(), scratch.size()));
    fields.author_generation = 9;
    fields.created_ms = 1000;
    assert(encodePublicationTicket(fields, scratch.data(), scratch.size(), PublicationTicketPhase::Checking));
    assert(!advancePublicationTicket(scratch.data(), kPublicationTicketBytes, PublicationTicketPhase::Prepared));
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 1050, true) == Status::Conflict);
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 1120, false) == Status::Expired);
    assert(completePublicationCheck(scratch.data(), kPublicationTicketBytes, 1050));
    assert(!completePublicationCheck(scratch.data(), kPublicationTicketBytes, 1050));
    assert(matchPublicationTicket(scratch.data(), kPublicationTicketBytes, owner.data(), token.data(), boot.data(), author.data(), 9, 1100, true) == Status::Ok);
}
