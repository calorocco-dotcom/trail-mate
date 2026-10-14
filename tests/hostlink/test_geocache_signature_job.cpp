#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/geocache_signature_job.h"
#include <array>
#include <cassert>
struct Port : geocaching::AuthorIssuePort
{
    unsigned signatures = 0, reservations = 0, cancels = 0;
    bool durable = false;
    bool getAuthorKey(uint8_t* output) override
    {
        std::memset(output, 3, 64);
        return true;
    }
    geocaching::AuthorReservationResult reserve(geocaching::ByteView, uint8_t*, size_t) override
    {
        ++reservations;
        return durable ? geocaching::AuthorReservationResult::Reserved : geocaching::AuthorReservationResult::Pending;
    }
    void cancelReservation() override { ++cancels; }
    bool sign(geocaching::ByteView record, uint8_t*, size_t, uint8_t* output, size_t capacity, size_t& written) override
    {
        ++signatures;
        assert(durable);
        std::array<uint8_t, 64> signature{};
        geocaching::protocol::CmpWriter writer(output, capacity);
        const bool ok = writer.array(2) && writer.binary(record) && writer.binary({signature.data(), signature.size()});
        written = writer.size();
        return ok;
    }
};
int main()
{
    std::array<uint8_t, 64> author;
    author.fill(3);
    std::array<uint8_t, 16> nonce{};
    std::array<uint8_t, 4096> raw{}, work{};
    std::array<char, 1025> description{};
    description.fill('a');
    description.back() = 0;
    geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
    assert(writer.array(16) && writer.unsignedInteger(1) && writer.binary({author.data(), author.size()}) && writer.binary({nonce.data(), nonce.size()}) &&
           writer.unsignedInteger(1) && writer.nil() && writer.unsignedInteger(0) && writer.signedInteger(1) && writer.signedInteger(1) &&
           writer.text("Cache") && writer.text(description.data()) && writer.text("") && writer.unsignedInteger(2) && writer.unsignedInteger(2) && writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.unsignedInteger(1));
    const geocaching::ByteView record{raw.data(), writer.size()};
    Port port;
    hostlink::management::GeocacheSignatureJob job(port);
    assert(job.begin(record, work.data(), work.size(), author.data(), 7));
    geocaching::ByteView chunk;
    size_t total = 0;
    assert(!job.read(0, 320, author.data(), 7, true, chunk, total));
    job.advance(author.data(), 7, true);
    job.advance(author.data(), 7, true);
    assert(port.signatures == 0 && port.reservations == 1);
    port.durable = true;
    job.advance(author.data(), 7, true);
    job.advance(author.data(), 7, true);
    assert(port.signatures == 1 && job.phase() == geocaching::AuthorIssuePhase::Signed);
    assert(job.read(0, 320, author.data(), 7, true, chunk, total) && total > 1024);
    const size_t expected = total;
    size_t offset = 0;
    std::array<uint8_t, 4096> copied{};
    while (offset < expected)
    {
        assert(job.read(offset, 17, author.data(), 7, true, chunk, total));
        assert(chunk.size <= 17 && total == expected);
        std::memcpy(copied.data() + offset, chunk.data, chunk.size);
        offset += chunk.size;
    }
    assert(offset == expected && !std::memcmp(copied.data(), work.data(), expected));
    assert(!job.read(expected, 320, author.data(), 7, true, chunk, total) && !chunk.data && total == 0);
    assert(!job.read(0, 321, author.data(), 7, true, chunk, total));
    assert(!job.read(0, 320, author.data(), 8, true, chunk, total));
    assert(job.phase() == geocaching::AuthorIssuePhase::Cancelled);
    assert(!job.read(0, 320, author.data(), 7, true, chunk, total));
    Port cancelled;
    hostlink::management::GeocacheSignatureJob pending(cancelled);
    assert(pending.begin(record, work.data(), work.size(), author.data(), 7));
    pending.advance(author.data(), 7, true);
    pending.advance(author.data(), 7, true);
    pending.advance(author.data(), 7, false);
    assert(cancelled.signatures == 0 && cancelled.cancels == 1);
}
