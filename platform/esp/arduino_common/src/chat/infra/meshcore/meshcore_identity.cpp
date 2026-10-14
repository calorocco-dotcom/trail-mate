/**
 * @file meshcore_identity.cpp
 * @brief MeshCore identity key management (Ed25519 + ECDH)
 */

#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_identity.h"
#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "platform/esp/common/radio_identity_work.h"

#include "esp_random.h"

#include <cstring>

namespace chat
{
namespace meshcore
{

namespace
{
constexpr const char* kIdentityPrefsNs = "mc_ident";
constexpr const char* kIdentityPrefsPriv = "priv64";
constexpr const char* kIdentityPrefsPub = "pub32";
constexpr const char* kIdentityPrefsVer = "ver";

void fillRandomBytes(uint8_t* out, size_t len)
{
    if (!out || len == 0)
    {
        return;
    }

    size_t index = 0;
    while (index < len)
    {
        const uint32_t rnd = static_cast<uint32_t>(esp_random());
        const size_t chunk = (len - index >= sizeof(rnd)) ? sizeof(rnd) : (len - index);
        memcpy(out + index, &rnd, chunk);
        index += chunk;
    }
}

} // namespace

bool MeshCoreIdentity::isValidPublicHash(uint8_t hash)
{
    return meshcoreIsValidPublicHash(hash);
}

bool MeshCoreIdentity::init()
{
    if (ready_)
    {
        return true;
    }
    bool absent = false;
    if (loadFromPrefs(absent))
    {
        ready_ = true;
        return true;
    }
    memset(priv_.data(), 0, priv_.size());
    memset(pub_.data(), 0, pub_.size());
    return absent && generateAndPersist();
}

bool MeshCoreIdentity::loadFromPrefs(bool& absent)
{
    using namespace platform::esp::common;
    absent = false;
    RadioIdentityWork work;
    if (!work.data()) return false;
    const auto loaded = readRadioIdentityRecord(kIdentityPrefsNs, RadioIdentityKind::MeshCore, work.data(), work.size());
    if (loaded == IdentityRecordResult::Ready)
    {
        if (work.data()[5])
        {
            absent = true;
            return false;
        }
        if (meshcoreIsZeroBytes(work.data() + 44, 32) || !meshcoreDerivePublicKey(work.data() + 44, work.data() + kRadioIdentityRecordBytes) ||
            std::memcmp(work.data() + 12, work.data() + kRadioIdentityRecordBytes, 32)) return false;
        std::memcpy(pub_.data(), work.data() + 12, pub_.size());
        std::memcpy(priv_.data(), work.data() + 44, priv_.size());
        return true;
    }
    if (loaded != IdentityRecordResult::Absent) return false;
    const auto legacy = readLegacyRadioIdentity(kIdentityPrefsNs, kIdentityPrefsPub, kIdentityPrefsPriv,
                                                RadioIdentityKind::MeshCore, work.data(), work.size(), kIdentityPrefsVer);
    absent = legacy == IdentityRecordResult::Absent;
    if (legacy != IdentityRecordResult::Ready) return false;
    memcpy(priv_.data(), work.data() + 44, priv_.size());
    memcpy(pub_.data(), work.data() + 12, pub_.size());

    uint8_t derived_pub[kPubKeySize] = {};
    if (meshcoreIsZeroBytes(priv_.data(), 32) || !meshcoreDerivePublicKey(priv_.data(), derived_pub))
    {
        memset(priv_.data(), 0, priv_.size());
        return false;
    }

    const bool needs_save = memcmp(pub_.data(), derived_pub, sizeof(derived_pub)) != 0;
    memcpy(pub_.data(), derived_pub, sizeof(derived_pub));

    if (needs_save)
    {
        if (!saveToPrefs())
        {
            memset(priv_.data(), 0, priv_.size());
            memset(pub_.data(), 0, pub_.size());
            return false;
        }
    }
    return true;
}

bool MeshCoreIdentity::saveToPrefs() const
{
    using namespace platform::esp::common;
    RadioIdentityWork work;
    if (!work.data()) return false;
    const auto prior = readRadioIdentityRecord(kIdentityPrefsNs, RadioIdentityKind::MeshCore, work.data(), work.size());
    if (prior != IdentityRecordResult::Ready && prior != IdentityRecordResult::Absent) return false;
    const auto generation = prior == IdentityRecordResult::Ready ? identityRecordU32(work.data() + 8) : 0;
    return commitRadioIdentityRecord(kIdentityPrefsNs, RadioIdentityKind::MeshCore, generation, pub_.data(), priv_.data(), work.data(), work.size(),
                                     [](const uint8_t* pub, const uint8_t* priv, uint8_t* scratch, size_t)
                                     { return !meshcoreIsZeroBytes(priv, 32) && meshcoreDerivePublicKey(priv, scratch) && !std::memcmp(pub, scratch, 32); }) == IdentityRecordResult::Committed;
}

bool MeshCoreIdentity::generateAndPersist()
{
    uint8_t seed[kMeshCoreSeedSize] = {};
    bool ok = false;

    for (size_t attempt = 0; attempt < 16; ++attempt)
    {
        fillRandomBytes(seed, sizeof(seed));
        if (meshcoreCreateKeypair(seed, pub_.data(), priv_.data()))
        {
            ok = true;
            break;
        }
    }

    memset(seed, 0, sizeof(seed));
    if (!ok)
    {
        memset(pub_.data(), 0, pub_.size());
        memset(priv_.data(), 0, priv_.size());
        return false;
    }

    if (!saveToPrefs())
    {
        memset(pub_.data(), 0, pub_.size());
        memset(priv_.data(), 0, priv_.size());
        return false;
    }

    ready_ = true;
    return true;
}

bool MeshCoreIdentity::sign(const uint8_t* message, size_t message_len,
                            uint8_t out_signature[kSignatureSize]) const
{
    if (!ready_ || !message || !out_signature)
    {
        return false;
    }

    return meshcoreSign(priv_.data(), pub_.data(), message, message_len, out_signature);
}

bool MeshCoreIdentity::verify(const uint8_t pubkey[kPubKeySize],
                              const uint8_t signature[kSignatureSize],
                              const uint8_t* message, size_t message_len)
{
    return meshcoreVerify(pubkey, signature, message, message_len);
}

bool MeshCoreIdentity::deriveSharedSecret(const uint8_t peer_pubkey[kPubKeySize],
                                          uint8_t out_secret[kPubKeySize]) const
{
    if (!ready_ || !peer_pubkey || !out_secret)
    {
        return false;
    }

    return meshcoreDeriveSharedSecret(priv_.data(), peer_pubkey, out_secret);
}

bool MeshCoreIdentity::exportPrivateKey(uint8_t out_priv[kPrivKeySize]) const
{
    if (!ready_ || !out_priv)
    {
        return false;
    }
    memcpy(out_priv, priv_.data(), priv_.size());
    return true;
}

bool MeshCoreIdentity::importPrivateKey(const uint8_t in_priv[kPrivKeySize])
{
    if (!in_priv)
    {
        return false;
    }

    uint8_t derived_pub[kPubKeySize] = {};
    if (meshcoreIsZeroBytes(in_priv, 32) || !meshcoreDerivePublicKey(in_priv, derived_pub))
    {
        return false;
    }

    using namespace platform::esp::common;
    RadioIdentityWork work;
    if (!work.data()) return false;
    const auto prior = readRadioIdentityRecord(kIdentityPrefsNs, RadioIdentityKind::MeshCore, work.data(), work.size());
    if (prior != IdentityRecordResult::Ready && prior != IdentityRecordResult::Absent) return false;
    const auto generation = prior == IdentityRecordResult::Ready ? identityRecordU32(work.data() + 8) : 0;
    const auto saved = commitRadioIdentityRecord(kIdentityPrefsNs, RadioIdentityKind::MeshCore, generation,
                                                 derived_pub, in_priv, work.data(), work.size(),
                                                 [](const uint8_t* pub, const uint8_t* priv, uint8_t* scratch, size_t)
                                                 { return !meshcoreIsZeroBytes(priv, 32) && meshcoreDerivePublicKey(priv, scratch) && !std::memcmp(pub, scratch, 32); });
    if (saved != IdentityRecordResult::Committed)
    {
        if (saved == IdentityRecordResult::Indeterminate)
        {
            ready_ = false;
            memset(priv_.data(), 0, priv_.size());
            memset(pub_.data(), 0, pub_.size());
        }
        return false;
    }
    memcpy(priv_.data(), in_priv, priv_.size());
    memcpy(pub_.data(), derived_pub, sizeof(derived_pub));
    ready_ = true;
    return true;
}

} // namespace meshcore
} // namespace chat
