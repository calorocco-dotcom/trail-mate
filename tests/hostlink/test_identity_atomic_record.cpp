#include "platform/esp/common/identity_atomic_record.h"
#include <array>
#include <cassert>
#include <vector>
using namespace platform::esp::common;
int main()
{
    std::array<uint8_t, 64> pub{};
    pub.fill(2);
    std::array<uint8_t, 96> priv{};
    priv.fill(3);
    std::array<uint8_t, kIdentityRecordWorkBytes> work{};
    const auto validate = [](const uint8_t*, const uint8_t*, uint8_t*, size_t)
    { return true; };
    assert(readIdentityRecord(work.data(), work.size()) == IdentityRecordResult::Absent);
    fake_nvs::values["lxmf_ident"]["enc_pub"] = std::vector<uint8_t>(32, 99);
    const auto legacy = fake_nvs::values["lxmf_ident"]["enc_pub"];
    assert(commitIdentityRecord(7, 7, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Committed);
    for (auto value : work) assert(value == 0);
    assert(readIdentityRecord(work.data(), work.size()) == IdentityRecordResult::Ready);
    assert(identityRecordU32(work.data() + 8) == 8);
    assert(!std::memcmp(work.data() + 12, pub.data(), 64) && !std::memcmp(work.data() + 76, priv.data(), 96));
    assert(fake_nvs::values["lxmf_ident"]["enc_pub"] == legacy);
    const auto before = fake_nvs::values;
    assert(commitIdentityRecord(7, 8, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::TargetChanged);
    assert(commitIdentityRecord(7, 7, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::TargetChanged);
    assert(fake_nvs::values == before);
    assert(commitIdentityRecord(UINT32_MAX, UINT32_MAX, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Invalid);
    fake_nvs::fail_commit = true;
    assert(commitIdentityRecord(8, 8, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Indeterminate);
    fake_nvs::fail_commit = false;
    // Fake NVS models the conservative case: the full new value is visible
    // despite a commit error. Recovery must read, never assume a rollback.
    assert(readIdentityRecord(work.data(), work.size()) == IdentityRecordResult::Ready);
    assert(identityRecordU32(work.data() + 8) == 9);
    auto& record = std::get<std::vector<uint8_t>>(fake_nvs::values["lxmf_ident"]["native_v1"]);
    record[8] ^= 1;
    assert(readIdentityRecord(work.data(), work.size()) == IdentityRecordResult::Invalid);
    for (size_t i = 0; i < kIdentityRecordBytes; ++i) assert(work[i] == 0);
    const auto corrupt = fake_nvs::values;
    assert(commitIdentityRecord(9, 9, pub.data(), priv.data(), work.data(), work.size(), validate) == IdentityRecordResult::Invalid);
    assert(fake_nvs::values == corrupt);
}
