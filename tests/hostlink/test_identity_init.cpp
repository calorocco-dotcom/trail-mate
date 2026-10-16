#include "platform/esp/common/identity_generation_store.h"
#include "platform/esp/common/identity_key_state.h"
#include <array>
#include <cassert>
struct FakeConsole
{
    void println(const char*) {}
} Serial;
class LxmfIdentity
{
  public:
    bool init();
    bool initExisting();
    bool loaded = false, ready_ = false, identity_storage_error_ = false;
    uint32_t author_generation_ = 0;
    std::array<uint8_t, 16> identity_hash_{1};
    unsigned loads = 0, generates = 0;
    bool loadFromPrefs()
    {
        ++loads;
        return loaded;
    }
    bool generateAndPersist()
    {
        ++generates;
        ready_ = true;
        return true;
    }
};
// The production init policy, not a replacement implementation. Crypto/key
// loading is simulated; NVS absence and generation checks use their real code.
#include "identity_init_actual.inc"
int main()
{
    LxmfIdentity absent;
    assert(!absent.initExisting() && absent.loads == 1 && absent.generates == 0);
    assert(fake_nvs::commits == 0);
    LxmfIdentity existing;
    existing.loaded = true;
    assert(existing.initExisting() && existing.ready_ && existing.author_generation_ == 1 && existing.generates == 0);
    const auto existing_loads = existing.loads;
    assert(existing.initExisting() && existing.loads == existing_loads);
    LxmfIdentity reboot_required;
    reboot_required.identity_storage_error_ = true;
    reboot_required.author_generation_ = UINT32_MAX;
    assert(!reboot_required.initExisting() && reboot_required.loads == 0 && reboot_required.generates == 0);
    fake_nvs::values.clear();
    LxmfIdentity first;
    assert(first.init() && first.generates == 1);
    fake_nvs::values["lxmf_ident"]["enc_pub"] = std::vector<uint8_t>(32, 1);
    const auto original = fake_nvs::values["lxmf_ident"];
    LxmfIdentity partial;
    assert(!partial.init() && partial.generates == 0 && partial.identity_storage_error_);
    assert(fake_nvs::values["lxmf_ident"] == original);
    fake_nvs::open_error = 42;
    LxmfIdentity unavailable;
    assert(!unavailable.init() && unavailable.generates == 0 && unavailable.identity_storage_error_);
    fake_nvs::open_error = 0;
    partial.loaded = true;
    assert(partial.init() && partial.ready_ && !partial.identity_storage_error_ && partial.generates == 0);
    assert(partial.author_generation_ == 1);
    const auto loads = partial.loads, commits = fake_nvs::commits;
    assert(partial.init() && partial.loads == loads && fake_nvs::commits == commits);
    fake_nvs::values["tm_author"].clear();
    fake_nvs::fail_commit = true;
    LxmfIdentity epoch_failure;
    epoch_failure.loaded = true;
    assert(!epoch_failure.init() && !epoch_failure.ready_ && epoch_failure.author_generation_ == 0 && epoch_failure.identity_storage_error_);
    assert(epoch_failure.generates == 0 && fake_nvs::handles.empty());
}
