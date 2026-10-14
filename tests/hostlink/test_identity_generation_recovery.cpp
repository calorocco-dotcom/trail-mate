#include "platform/esp/common/identity_generation_recovery.h"
#include "platform/esp/common/identity_generation_store.h"
#include <array>
#include <cassert>
int main()
{
    using namespace platform::esp::common;
    std::array<uint8_t, 16> old_hash{}, new_hash{};
    old_hash.fill(1);
    new_hash.fill(2);
    uint32_t generation = 99;
    assert(syncIdentityGeneration(old_hash.data(), generation) && generation == 1);
    assert(recoverIdentityGeneration(new_hash.data(), 2, generation) && generation == 2);
    const unsigned writes = fake_nvs::commits;
    assert(recoverIdentityGeneration(new_hash.data(), 2, generation) && generation == 2 && writes == fake_nvs::commits);
    assert(!recoverIdentityGeneration(old_hash.data(), 2, generation) && generation == 0);
    assert(!recoverIdentityGeneration(old_hash.data(), 1, generation) && generation == 0);
    assert(syncIdentityGeneration(new_hash.data(), generation, true) && generation == 3);
    assert(recoverIdentityGeneration(new_hash.data(), 2, generation) && generation == 3);
    auto& epoch = std::get<std::vector<uint8_t>>(fake_nvs::values["tm_author"]["epoch"]);
    epoch.resize(2);
    const auto corrupted = fake_nvs::values;
    assert(!recoverIdentityGeneration(new_hash.data(), 4, generation) && generation == 0);
    assert(fake_nvs::values == corrupted);
    fake_nvs::values["tm_author"].clear();
    assert(recoverIdentityGeneration(new_hash.data(), 8, generation) && generation == 8);
    fake_nvs::fail_commit = true;
    assert(!recoverIdentityGeneration(new_hash.data(), 9, generation) && generation == 0);
    fake_nvs::fail_commit = false;
    assert(recoverIdentityGeneration(new_hash.data(), 9, generation) && generation == 9);
    assert(fake_nvs::handles.empty());
}
