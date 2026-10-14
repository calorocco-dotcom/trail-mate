#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/identity_restore_request.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/common/identity_generation_recovery.h"
#include "platform/esp/common/identity_generation_store.h"
#include "platform/esp/common/identity_key_state.h"
#include "platform/esp/common/identity_native_key_validation.h"
#include "platform/esp/common/identity_seed_key_conversion.h"
#include <array>
#include <cassert>
struct RestoreConsole
{
    void println(const char*) {}
} Serial;
// Address primitives are fixtures; real signing, pairing, storage policy and
// actual identity class/methods are exercised. Address algorithms have separate tests.
namespace chat::reticulum
{
void computeIdentityHash(const uint8_t*, uint8_t* out) { std::memset(out, 9, 16); }
void computeNameHash(const char*, const char*, uint8_t* out) { std::memset(out, 8, 10); }
void computeDestinationHash(const uint8_t*, const uint8_t*, uint8_t* out) { std::memset(out, 10, 16); }
} // namespace chat::reticulum
namespace chat::lxmf
{
bool LxmfIdentity::loadFromPrefs() { return false; }
bool LxmfIdentity::generateAndPersist()
{
    enc_priv_.fill(24);
    enc_priv_[31] = 0x58;
    assert(Curve25519::eval(enc_pub_.data(), enc_priv_.data(), nullptr));
    std::array<uint8_t, 32> seed{};
    seed.fill(17);
    ed25519_create_keypair(sig_pub_.data(), sig_priv_.data(), seed.data());
    identity_hash_.fill(9);
    destination_hash_.fill(10);
    author_generation_ = 7;
    ready_ = true;
    return true;
}
#include "identity_init_actual.inc"
#include "identity_restore_actual.inc"
} // namespace chat::lxmf
void testIdentityRestoreActual()
{
    using namespace hostlink::management;
    using Result = chat::DeviceIdentityRestoreResult;
    using geocaching::protocol::CmpWriter;
    const auto run = [](unsigned scenario)
    {
        fake_nvs::values.clear();
        fake_nvs::fail_commit = false;
        chat::lxmf::LxmfIdentity identity;
        assert(identity.init());
        std::array<uint8_t, 64> pub{}, signature{};
        std::array<uint8_t, 96> priv{};
        std::memcpy(pub.data(), identity.encryptionPublicKey(), 32);
        std::memcpy(pub.data() + 32, identity.signingPublicKey(), 32);
        assert(identity.copyNativePrivateKey(7, priv.data(), priv.size()));
        const auto expected_private = priv;
        std::array<uint8_t,64> seeds{};
        std::memcpy(seeds.data(),priv.data(),32);
        std::memset(seeds.data()+32,17,32);
        if(scenario==6)seeds[5]^=1;
        if(scenario==7)seeds[35]^=1;
        const bool seed_format=scenario>=5;
        std::array<uint8_t, 16> hash{}, delivery{};
        hash.fill(scenario == 3 ? 11 : 9);
        delivery.fill(10);
        std::array<uint8_t, 320> payload{};
        CmpWriter writer(payload.data(), payload.size());
        assert(writer.array(8) && writer.unsignedInteger(1) && writer.text("trailmate.identity.backup.v1") && writer.unsignedInteger(seed_format?1:0) && writer.binary({pub.data(), 64}) && writer.binary(seed_format?geocaching::ByteView{seeds.data(),64}:geocaching::ByteView{priv.data(),96}) && writer.binary({hash.data(), 16}) && writer.binary({delivery.data(), 16}) && writer.unsignedInteger(2));
        std::array<uint8_t, 352> work{};
        constexpr char domain[] = "TrailMate.Identity.Backup.v1";
        std::memcpy(work.data(), domain, sizeof(domain));
        std::memcpy(work.data() + sizeof(domain), payload.data(), writer.size());
        ed25519_sign(signature.data(), work.data(), sizeof(domain) + writer.size(), pub.data() + 32, priv.data() + 32);
        if (scenario == 2) signature[0] ^= 1;
        std::array<uint8_t, 448> body{};
        CmpWriter request(body.data(), body.size());
        assert(request.array(5) && request.unsignedInteger(scenario == 1 ? 6 : 7) && request.binary({pub.data(), 64}) && request.binary({payload.data(), writer.size()}) && request.binary({signature.data(), 64}));
        body[request.size()] = 0xc3;
        IdentityRestoreView parsed;
        assert(parseIdentityRestoreRequest({body.data(), request.size() + 1}, parsed) == Status::Ok);
        if (scenario == 4 || scenario == 8) fake_nvs::fail_commit = true;
        const auto before = fake_nvs::values;
        const auto result = identity.restoreNativeIdentity(parsed, work.data(), work.size());
        for (auto byte : work) assert(byte == 0);
        if (scenario == 0 || scenario == 4 || scenario == 5 || scenario == 8)
        {
            assert(result == (scenario == 0 || scenario == 5 ? Result::CommittedRebootRequired : Result::IndeterminateRebootRequired));
            assert(!identity.isReady() && !identity.init());
            priv.fill(99);
            assert(!identity.copyNativePrivateKey(7, priv.data(), priv.size()));
            for (auto byte : priv) assert(byte == 0);
            fake_nvs::fail_commit = false;
            assert(platform::esp::common::readIdentityRecord(work.data(), work.size()) == platform::esp::common::IdentityRecordResult::Ready);
            assert(platform::esp::common::identityRecordU32(work.data() + 8) == 8);
            assert(!std::memcmp(work.data()+76,expected_private.data(),expected_private.size()));
        }
        else
        {
            assert(result == (scenario == 1 ? Result::StaleIdentity : Result::InvalidInput));
            assert(identity.isReady());
            assert(fake_nvs::values == before);
        }
        assert(fake_nvs::handles.empty());
    };
    for (unsigned scenario = 0; scenario < 9; ++scenario) run(scenario);
}
