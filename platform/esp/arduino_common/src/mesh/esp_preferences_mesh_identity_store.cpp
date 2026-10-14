#include "platform/esp/arduino_common/mesh/esp_preferences_mesh_identity_store.h"

#include "../chat/internal/blob_store_io.h"
#include "platform/esp/common/radio_identity_work.h"
#include <Curve25519.h>

#include <cstring>
#include <vector>

namespace platform::esp::arduino_common::mesh
{
namespace
{

bool isZero(const uint8_t* data, size_t size)
{
    if (!data)
    {
        return true;
    }
    for (size_t index = 0; index < size; ++index)
    {
        if (data[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool validIdentity(const ::mesh::LocalIdentity& identity, uint8_t* scratch)
{
    return identity.valid &&
           scratch && !isZero(identity.private_key, sizeof(identity.private_key)) &&
           (identity.private_key[0] & 7) == 0 && (identity.private_key[31] & 0xc0) == 0x40 &&
           Curve25519::eval(scratch, identity.private_key, nullptr) &&
           !std::memcmp(scratch, identity.public_key, sizeof(identity.public_key));
}
bool isDefaultIdentityOptions(const EspPreferencesLocalIdentityStore::Options& options)
{
    return options.ns && options.public_key && options.private_key &&
           !std::strcmp(options.ns, "chat") && !std::strcmp(options.public_key, "pki_pub") &&
           !std::strcmp(options.private_key, "pki_priv");
}

} // namespace

EspPreferencesLocalIdentityStore::EspPreferencesLocalIdentityStore()
    : options_(Options{})
{
}

EspPreferencesLocalIdentityStore::EspPreferencesLocalIdentityStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult EspPreferencesLocalIdentityStore::load(::mesh::LocalIdentity& out)
{
    out = ::mesh::LocalIdentity{};
    using namespace platform::esp::common;
    RadioIdentityWork work;
    if (!work.data()) return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    const auto loaded = isDefaultIdentityOptions(options_)
                            ? readRadioIdentityRecord(options_.ns, RadioIdentityKind::Meshtastic, work.data(), work.size())
                            : IdentityRecordResult::Absent;
    if (loaded == IdentityRecordResult::Ready)
    {
        if (work.data()[5]) return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
        std::memcpy(out.public_key, work.data() + 12, sizeof(out.public_key));
        std::memcpy(out.private_key, work.data() + 44, sizeof(out.private_key));
        out.valid = true;
        if (validIdentity(out, work.data() + kRadioIdentityRecordBytes)) return ::mesh::StoreResult::success();
        out = {};
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }
    if (loaded != IdentityRecordResult::Absent)
        return ::mesh::StoreResult::fail(loaded == IdentityRecordResult::Invalid ? ::mesh::StoreFailure::Corrupt : ::mesh::StoreFailure::IoError);
    if (isDefaultIdentityOptions(options_))
    {
        const auto legacy = readLegacyRadioIdentity(options_.ns, options_.public_key, options_.private_key,
                                                    RadioIdentityKind::Meshtastic, work.data(), work.size());
        if (legacy != IdentityRecordResult::Ready)
            return ::mesh::StoreResult::fail(legacy == IdentityRecordResult::Absent    ? ::mesh::StoreFailure::NotFound
                                             : legacy == IdentityRecordResult::Invalid ? ::mesh::StoreFailure::Corrupt
                                                                                       : ::mesh::StoreFailure::IoError);
        std::memcpy(out.public_key, work.data() + 12, sizeof(out.public_key));
        std::memcpy(out.private_key, work.data() + 44, sizeof(out.private_key));
        out.valid = true;
        if (validIdentity(out, work.data() + kRadioIdentityRecordBytes)) return ::mesh::StoreResult::success();
        out = {};
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    std::vector<uint8_t> public_blob;
    std::vector<uint8_t> private_blob;
    const bool have_public =
        ::chat::infra::loadRawBlobFromPreferences(options_.ns, options_.public_key, public_blob);
    const bool have_private =
        ::chat::infra::loadRawBlobFromPreferences(options_.ns, options_.private_key, private_blob);
    if (!have_public && !have_private)
    {
        return ::mesh::StoreResult::fail(radioIdentityLegacyAbsent(options_.ns, options_.public_key, options_.private_key)
                                             ? ::mesh::StoreFailure::NotFound
                                             : ::mesh::StoreFailure::IoError);
    }
    if (!have_public || !have_private ||
        public_blob.size() != sizeof(out.public_key) ||
        private_blob.size() != sizeof(out.private_key))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    std::memcpy(out.public_key, public_blob.data(), sizeof(out.public_key));
    std::memcpy(out.private_key, private_blob.data(), sizeof(out.private_key));
    out.valid = true;
    if (!validIdentity(out, work.data() + kRadioIdentityRecordBytes))
    {
        out = {};
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult EspPreferencesLocalIdentityStore::save(
    const ::mesh::LocalIdentity& identity)
{
    using namespace platform::esp::common;
    RadioIdentityWork work;
    if (!work.data()) return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    if (!validIdentity(identity, work.data() + kRadioIdentityRecordBytes))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }
    // Preserve explicitly configured legacy storage; native_rf1 belongs only
    // to the default PKI owner, not arbitrary key pairs sharing a namespace.
    if (!isDefaultIdentityOptions(options_))
    {
        const bool public_ok = ::chat::infra::saveRawBlobToPreferences(options_.ns, options_.public_key,
                                                                       identity.public_key, sizeof(identity.public_key));
        const bool private_ok = ::chat::infra::saveRawBlobToPreferences(options_.ns, options_.private_key,
                                                                        identity.private_key, sizeof(identity.private_key));
        return public_ok && private_ok ? ::mesh::StoreResult::success() : ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    }

    const auto prior = readRadioIdentityRecord(options_.ns, RadioIdentityKind::Meshtastic, work.data(), work.size());
    if (prior != IdentityRecordResult::Ready && prior != IdentityRecordResult::Absent)
        return ::mesh::StoreResult::fail(prior == IdentityRecordResult::Invalid ? ::mesh::StoreFailure::Corrupt : ::mesh::StoreFailure::IoError);
    const auto generation = prior == IdentityRecordResult::Ready ? identityRecordU32(work.data() + 8) : 0;
    const auto saved = commitRadioIdentityRecord(options_.ns, RadioIdentityKind::Meshtastic, generation,
                                                 identity.public_key, identity.private_key, work.data(), work.size(),
                                                 [](const uint8_t* pub, const uint8_t* priv, uint8_t* scratch, size_t)
                                                 { return Curve25519::eval(scratch, priv, nullptr) && !std::memcmp(pub, scratch, 32); });
    return saved == IdentityRecordResult::Committed ? ::mesh::StoreResult::success()
                                                    : ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
}

::mesh::StoreResult EspPreferencesLocalIdentityStore::clear()
{
    if (!isDefaultIdentityOptions(options_))
    {
        ::chat::infra::clearPreferencesKeys(options_.ns, options_.public_key, options_.private_key);
        return ::mesh::StoreResult::success();
    }
    using namespace platform::esp::common;
    RadioIdentityWork work;
    if (!work.data()) return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    const auto prior = readRadioIdentityRecord(options_.ns, RadioIdentityKind::Meshtastic, work.data(), work.size());
    if (prior != IdentityRecordResult::Ready && prior != IdentityRecordResult::Absent)
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    const auto generation = prior == IdentityRecordResult::Ready ? identityRecordU32(work.data() + 8) : 0;
    const auto cleared = commitRadioIdentityRecord(
        options_.ns, RadioIdentityKind::Meshtastic, generation, nullptr, nullptr,
        work.data(), work.size(), [](const uint8_t*, const uint8_t*, uint8_t*, size_t)
        { return false; },
        true);
    return cleared == IdentityRecordResult::Committed ? ::mesh::StoreResult::success()
                                                      : ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
}

} // namespace platform::esp::arduino_common::mesh
