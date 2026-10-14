#include "platform/esp/common/identity_generation_store.h"
#include "platform/esp/common/identity_key_state.h"
#include <cassert>

int main()
{
    using platform::esp::common::syncIdentityGeneration;
    uint8_t first[16]{}, second[16]{};
    first[0] = 1;
    second[0] = 2;
    uint32_t generation = 99;
    assert(platform::esp::common::lxmfIdentityKeysAbsent());
    fake_nvs::values["lxmf_ident"];
    assert(platform::esp::common::lxmfIdentityKeysAbsent());
    fake_nvs::values["lxmf_ident"]["enc_pub"] = std::vector<uint8_t>{};
    assert(!platform::esp::common::lxmfIdentityKeysAbsent());
    fake_nvs::values["lxmf_ident"]["enc_pub"] = uint32_t{1};
    assert(!platform::esp::common::lxmfIdentityKeysAbsent());
    fake_nvs::values["lxmf_ident"].clear();
    fake_nvs::values["lxmf_ident"]["sig_priv"] = std::vector<uint8_t>(63, 1);
    assert(!platform::esp::common::lxmfIdentityKeysAbsent());
    assert(fake_nvs::handles.empty());
    assert(syncIdentityGeneration(first, generation) && generation == 1);
    assert(fake_nvs::commits == 1);
    assert(syncIdentityGeneration(first, generation) && generation == 1 && fake_nvs::commits == 1);
    assert(syncIdentityGeneration(second, generation) && generation == 2);
    assert(syncIdentityGeneration(first, generation) && generation == 3);
    assert(syncIdentityGeneration(first, generation, true) && generation == 4);
    const auto good = std::get<std::vector<uint8_t>>(fake_nvs::values["tm_author"]["epoch"]);
    assert(good.size() == 20 && good[0] == 4 && !std::memcmp(good.data() + 4, first, 16));
    auto invalid = good;
    invalid[0] = 0;
    fake_nvs::values["tm_author"]["epoch"] = invalid;
    assert(!syncIdentityGeneration(first, generation) && generation == 0);
    assert(std::get<std::vector<uint8_t>>(fake_nvs::values["tm_author"]["epoch"]) == invalid);
    invalid.resize(2);
    fake_nvs::values["tm_author"]["epoch"] = invalid;
    assert(!syncIdentityGeneration(first, generation) && generation == 0);
    auto exhausted = good;
    for (unsigned index = 0; index < 4; ++index) exhausted[index] = 0xff;
    fake_nvs::values["tm_author"]["epoch"] = exhausted;
    assert(!syncIdentityGeneration(first, generation, true) && generation == 0);
    fake_nvs::values["tm_author"]["epoch"] = good;
    fake_nvs::fail_commit = true;
    assert(!syncIdentityGeneration(second, generation) && generation == 0);
    assert(fake_nvs::handles.empty());
}
