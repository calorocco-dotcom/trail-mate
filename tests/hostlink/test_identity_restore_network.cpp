#include "chat/ports/device_identity_restore.h"
#include "hostlink/identity_restore_request.h"
#include <array>
#include <cassert>
#include <vector>
namespace chat::lxmf
{
namespace runtime
{
enum class PropagationSyncStage
{
    Idle,
    Waiting
};
}
enum class LinkState
{
    Pending,
    Handshake,
    Active,
    Stale,
    Closed
};
struct LinkSession
{
    LinkState state;
};
struct Ledger
{
    size_t count = 0;
    size_t size() const { return count; }
};
struct PageClient
{
    bool pending = false;
    bool empty() const { return !pending; }
};
struct Propagation
{
    bool uploads = false;
    size_t deliveries = 0;
    struct State
    {
        runtime::PropagationSyncStage sync_stage = runtime::PropagationSyncStage::Idle;
    } value;
    bool hasPendingUploads() const { return uploads; }
    size_t pendingDeliveryCount() const { return deliveries; }
    const State& state() const { return value; }
};
struct Links
{
    std::vector<LinkSession> sessions;
    template <typename F>
    void forEachSession(F function)
    {
        for (const auto& item : sessions) function(item);
    }
};
struct Identity
{
    unsigned calls = 0;
    DeviceIdentityRestoreResult restoreNativeIdentity(const hostlink::management::IdentityRestoreView&, uint8_t*, size_t)
    {
        ++calls;
        return DeviceIdentityRestoreResult::CommittedRebootRequired;
    }
};
struct LxmfAdapter
{
    Ledger delivery_attempt_ledger_;
    PageClient network_page_client_;
    Propagation propagation_client_;
    Links link_manager_;
    Identity identity_;
    DeviceIdentityRestoreResult restoreDeviceIdentity(const hostlink::management::IdentityRestoreView&, uint8_t*, size_t);
};
// Exact production admission method; the owners below expose controlled states.
#include "identity_restore_network_actual.inc"
} // namespace chat::lxmf
int main()
{
    using namespace chat::lxmf;
    using Result = chat::DeviceIdentityRestoreResult;
    hostlink::management::IdentityRestoreView request;
    std::array<uint8_t, 352> work{};
    for (unsigned condition = 0; condition < 9; ++condition)
    {
        LxmfAdapter adapter;
        if (condition == 0) adapter.delivery_attempt_ledger_.count = 1;
        if (condition == 1) adapter.network_page_client_.pending = true;
        if (condition == 2) adapter.propagation_client_.uploads = true;
        if (condition == 3) adapter.propagation_client_.deliveries = 1;
        if (condition == 4) adapter.propagation_client_.value.sync_stage = runtime::PropagationSyncStage::Waiting;
        if (condition >= 5) adapter.link_manager_.sessions.push_back({static_cast<LinkState>(condition - 5)});
        assert(adapter.restoreDeviceIdentity(request, work.data(), work.size()) == Result::Unavailable && adapter.identity_.calls == 0);
    }
    LxmfAdapter idle;
    idle.link_manager_.sessions.push_back({LinkState::Closed});
    assert(idle.restoreDeviceIdentity(request, work.data(), work.size()) == Result::CommittedRebootRequired && idle.identity_.calls == 1);
}
