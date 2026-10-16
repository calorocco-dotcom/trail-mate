#include "platform/esp/common/radio_identity_record.h"
#include <array>
#include <cassert>
#include <vector>
using namespace platform::esp::common;
int main()
{
    std::array<uint8_t, 32> pub{};
    pub.fill(2);
    std::array<uint8_t, 64> priv{};
    priv.fill(3);
    std::array<uint8_t, kRadioIdentityWorkBytes> work{};
    const auto validate = [](const uint8_t*, const uint8_t*, uint8_t*, size_t)
    { return true; };
    for (const auto kind : {RadioIdentityKind::MeshCore, RadioIdentityKind::Meshtastic})
    {
        fake_nvs::values.clear();
        assert(readRadioIdentityRecord("fixture", kind, work.data(), work.size()) == IdentityRecordResult::Absent);
        fake_nvs::values["fixture"]["old_private"] = std::vector<uint8_t>{9};
        assert(commitRadioIdentityRecord("fixture", kind, 0, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Committed);
        for (auto byte : work) assert(!byte);
        assert(readRadioIdentityRecord("fixture", kind, work.data(), work.size()) == IdentityRecordResult::Ready);
        assert(identityRecordU32(work.data() + 8) == 1 && work[5] == 0);
        assert(!std::memcmp(work.data() + 12, pub.data(), 32));
        assert(!std::memcmp(work.data() + 44, priv.data(), radioIdentityPrivateBytes(kind)));
        const auto before = fake_nvs::values;
        assert(commitRadioIdentityRecord("fixture", kind, 0, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::TargetChanged);
        assert(fake_nvs::values == before);
        fake_nvs::fail_commit = true;
        assert(commitRadioIdentityRecord("fixture", kind, 1, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Indeterminate);
        fake_nvs::fail_commit = false;
        assert(readRadioIdentityRecord("fixture", kind, work.data(), work.size()) == IdentityRecordResult::Ready);
        assert(identityRecordU32(work.data() + 8) == 2);
        assert(commitRadioIdentityRecord("fixture", kind, 2, nullptr, nullptr, work.data(), work.size(), validate, true) == IdentityRecordResult::Committed);
        assert(readRadioIdentityRecord("fixture", kind, work.data(), work.size()) == IdentityRecordResult::Ready);
        assert(work[5] == 1 && identityRecordU32(work.data() + 8) == 3);
        for (size_t i = 12; i < 108; ++i) assert(!work[i]);
        assert(fake_nvs::values["fixture"]["old_private"] == before.at("fixture").at("old_private"));
        auto& bytes = std::get<std::vector<uint8_t>>(fake_nvs::values["fixture"][kRadioIdentityRecordKey]);
        bytes[12] = 1;
        identityRecordPutU32(bytes.data() + 108, identityRecordCrc(bytes.data(), 108));
        assert(readRadioIdentityRecord("fixture", kind, work.data(), work.size()) == IdentityRecordResult::Invalid);
        const auto corrupted = fake_nvs::values;
        assert(commitRadioIdentityRecord("fixture", kind, 3, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Invalid);
        assert(fake_nvs::values == corrupted);
        assert(commitRadioIdentityRecord("fixture", kind, UINT32_MAX, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Invalid);
    }
    fake_nvs::values.clear();
    assert(readLegacyRadioIdentity("legacy", "pub", "priv", RadioIdentityKind::MeshCore, work.data(), work.size(), "ver") == IdentityRecordResult::Absent);
    fake_nvs::values["legacy"]["priv"] = std::vector<uint8_t>(64, 7);
    assert(readLegacyRadioIdentity("legacy", "pub", "priv", RadioIdentityKind::MeshCore, work.data(), work.size(), "ver") == IdentityRecordResult::Ready);
    fake_nvs::values["legacy"]["ver"] = uint8_t(2);
    assert(readLegacyRadioIdentity("legacy", "pub", "priv", RadioIdentityKind::MeshCore, work.data(), work.size(), "ver") == IdentityRecordResult::Invalid);
    fake_nvs::values["legacy"]["pub"] = uint8_t(1);
    assert(!radioIdentityLegacyAbsent("legacy", "pub", "priv"));
    assert(readLegacyRadioIdentity("legacy", "pub", "priv", RadioIdentityKind::MeshCore, work.data(), work.size()) == IdentityRecordResult::Invalid);
    fake_nvs::open_error = 7;
    assert(!radioIdentityLegacyAbsent("empty", "pub", "priv"));
    assert(readLegacyRadioIdentity("empty", "pub", "priv", RadioIdentityKind::Meshtastic, work.data(), work.size()) == IdentityRecordResult::StorageError);
    assert(fake_nvs::handles.empty());
}
