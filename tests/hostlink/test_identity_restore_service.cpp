#include "hostlink/identity_restore_service.h"
#include <array>
#include <cassert>
struct Adapter
{
    unsigned calls = 0;
    chat::DeviceIdentityRestoreResult result = chat::DeviceIdentityRestoreResult::CommittedRebootRequired;
    chat::DeviceIdentityRestoreResult restoreDeviceIdentity(const hostlink::management::IdentityRestoreView& view, uint8_t*, size_t capacity)
    {
        assert(view.expected_generation == 7 && capacity == 352);
        ++calls;
        return result;
    }
};
int main()
{
    using namespace hostlink::management;
    using geocaching::protocol::CmpWriter;
    std::array<uint8_t, 64> pub{}, sig{};
    std::array<uint8_t, 96> priv{};
    std::array<uint8_t, 16> hash{};
    std::array<uint8_t, 320> payload{};
    CmpWriter p(payload.data(), payload.size());
    assert(p.array(8) && p.unsignedInteger(1) && p.text("trailmate.identity.backup.v1") && p.unsignedInteger(0) && p.binary({pub.data(), 64}) && p.binary({priv.data(), 96}) && p.binary({hash.data(), 16}) && p.binary({hash.data(), 16}) && p.unsignedInteger(3));
    std::array<uint8_t, 448> body{};
    CmpWriter request(body.data(), body.size());
    assert(request.array(5) && request.unsignedInteger(7) && request.binary({pub.data(), 64}) && request.binary({payload.data(), p.size()}) && request.binary({sig.data(), 64}));
    body[request.size()] = 0xc3;
    std::array<uint8_t, 16> session{}, output{};
    EnvelopeView envelope{session.data(), nullptr, uint16_t(Operation::IdentityBackupRestore), body.data(), request.size() + 1};
    IdentityRestoreWork work;
    IdentityRestoreActivity activity;
    Adapter adapter;
    size_t written = 99;
    for (bool IdentityRestoreActivity::*field : {&IdentityRestoreActivity::configuration, &IdentityRestoreActivity::geocaching, &IdentityRestoreActivity::snapshot, &IdentityRestoreActivity::upload, &IdentityRestoreActivity::prepare, &IdentityRestoreActivity::publication, &IdentityRestoreActivity::network, &IdentityRestoreActivity::restart})
    {
        activity.*field = true;
        assert(processIdentityRestore(adapter, envelope, activity, work, output.data(), output.size(), written) == Status::Busy && written == 0 && adapter.calls == 0);
        activity.*field = false;
    }
    assert(processIdentityRestore(adapter, envelope, activity, work, output.data(), output.size(), written) == Status::Ok && written == 3 && output[0] == 0x92 && output[1] == 0 && output[2] == 8);
    adapter.result = chat::DeviceIdentityRestoreResult::IndeterminateRebootRequired;
    assert(processIdentityRestore(adapter, envelope, activity, work, output.data(), output.size(), written) == Status::Ok && output[1] == 1);
    adapter.result = chat::DeviceIdentityRestoreResult::StaleIdentity;
    assert(processIdentityRestore(adapter, envelope, activity, work, output.data(), output.size(), written) == Status::StaleIdentity && written == 0);
    const unsigned calls = adapter.calls;
    body[request.size()] = 0xc2;
    assert(processIdentityRestore(adapter, envelope, activity, work, output.data(), output.size(), written) == Status::Unauthorized && adapter.calls == calls);
    for (size_t i = 0; i < sizeof(work); ++i) assert(reinterpret_cast<uint8_t*>(&work)[i] == 0);
}
