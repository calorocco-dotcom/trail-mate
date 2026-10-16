#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/identity_restore_request.h"
#include <array>
#include <cassert>
int main()
{
    using namespace hostlink::management;
    using geocaching::protocol::CmpWriter;
    std::array<uint8_t, 64> pub{}, signature{};
    std::array<uint8_t, 96> priv{};
    std::array<uint8_t, 32> seed{};
    seed.fill(17);
    pub.fill(3);
    priv.fill(4);
    ed25519_create_keypair(pub.data() + 32, priv.data() + 32, seed.data());
    std::array<uint8_t, 16> hash{};
    hash.fill(9);
    std::array<uint8_t, 320> payload{};
    CmpWriter p(payload.data(), payload.size());
    assert(p.array(8) && p.unsignedInteger(1) && p.text("trailmate.identity.backup.v1") && p.unsignedInteger(0) && p.binary({pub.data(), 64}) && p.binary({priv.data(), 96}) && p.binary({hash.data(), 16}) && p.binary({hash.data(), 16}) && p.unsignedInteger(3));
    std::array<uint8_t, 352> work{};
    constexpr char domain[] = "TrailMate.Identity.Backup.v1";
    std::memcpy(work.data(), domain, sizeof(domain));
    std::memcpy(work.data() + sizeof(domain), payload.data(), p.size());
    ed25519_sign(signature.data(), work.data(), sizeof(domain) + p.size(), pub.data() + 32, priv.data() + 32);
    std::array<uint8_t, 448> request{};
    CmpWriter writer(request.data(), request.size());
    assert(writer.array(5) && writer.unsignedInteger(7) && writer.binary({pub.data(), 64}) && writer.binary({payload.data(), p.size()}) && writer.binary({signature.data(), 64}));
    const size_t length = writer.size() + 1;
    request[length - 1] = 0xc3;
    IdentityRestoreView view;
    assert(parseIdentityRestoreRequest({request.data(), length}, view) == Status::Ok);
    assert(view.expected_generation == 7 && view.backup_generation == 3 && view.private_key.size == 96);
    assert(verifyIdentityRestoreSignature(view, work.data(), work.size()));
    for (size_t i = 0; i < sizeof(domain) + p.size(); ++i) assert(work[i] == 0);
    const_cast<uint8_t*>(view.signature.data)[0] ^= 1;
    assert(!verifyIdentityRestoreSignature(view, work.data(), work.size()));
    const_cast<uint8_t*>(view.signature.data)[0] ^= 1;
    assert(!verifyIdentityRestoreSignature(view, const_cast<uint8_t*>(view.payload.data), 352));
    request[length - 1] = 0xc2;
    assert(parseIdentityRestoreRequest({request.data(), length}, view) == Status::Unauthorized && view.private_key.data == nullptr);
    request[length - 1] = 0xc3;
    request[length] = 0;
    assert(parseIdentityRestoreRequest({request.data(), length + 1}, view) == Status::InvalidField);
    for (size_t i = 0; i < length; ++i) assert(parseIdentityRestoreRequest({request.data(), i}, view) != Status::Ok);
}
