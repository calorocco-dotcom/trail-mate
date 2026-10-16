#include "platform/esp/arduino_common/hostlink/hostlink_identity_restore.h"
#include <array>
#include <cassert>
int main()
{
    using namespace hostlink::management;
    using geocaching::protocol::CmpWriter;
    chat::IMeshAdapter adapter;
    IdentityRestoreActivity activity;
    std::array<uint8_t, 448> body{};
    std::array<uint8_t, 320> payload{};
    std::array<uint8_t, 64> pub{}, signature{};
    std::array<uint8_t, 96> priv{};
    std::array<uint8_t, 16> hash{}, session{}, output{};
    CmpWriter p(payload.data(), payload.size());
    assert(p.array(8) && p.unsignedInteger(1) && p.text("trailmate.identity.backup.v1") && p.unsignedInteger(0) && p.binary({pub.data(), 64}) && p.binary({priv.data(), 96}) && p.binary({hash.data(), 16}) && p.binary({hash.data(), 16}) && p.unsignedInteger(3));
    CmpWriter writer(body.data(), body.size());
    assert(writer.array(5) && writer.unsignedInteger(7) && writer.binary({pub.data(), 64}) && writer.binary({payload.data(), p.size()}) && writer.binary({signature.data(), 64}));
    body[writer.size()] = 0xc3;
    EnvelopeView request{session.data(), nullptr, uint16_t(Operation::IdentityBackupRestore), body.data(), writer.size() + 1};
    size_t written = 0;
    auto run = [&]()
    { return restoreIdentityOperation(adapter, request, activity, output.data(), output.size(), written); };
    activity.upload = true;
    assert(run() == Status::Busy && restore_fake::allocations == 0);
    activity.upload = false;
    restore_fake::allocation_fails = true;
    assert(run() == Status::Busy);
    restore_fake::allocation_fails = false;
    restore_fake::loaded = true;
    restore_fake::phase = 1;
    assert(run() == Status::Busy && adapter.calls == 0);
    restore_fake::loaded = false;
    restore_fake::journal_exists = true;
    assert(run() == Status::StorageUnavailable && adapter.calls == 0);
    restore_fake::journal_exists = false;
    restore_fake::admitted = false;
    assert(run() == Status::Busy && adapter.calls == 0);
    restore_fake::admitted = true;
    assert(run() == Status::Ok && written == 3 && output[1] == 0 && output[2] == 8 && restore_fake::dropped);
    restore_fake::loaded = true;
    restore_fake::phase = 2;
    adapter.result = chat::DeviceIdentityRestoreResult::IndeterminateRebootRequired;
    assert(run() == Status::Ok && output[1] == 1 && restore_fake::dropped);
    adapter.result = chat::DeviceIdentityRestoreResult::InvalidInput;
    assert(run() == Status::InvalidField && !restore_fake::dropped);
    assert(restore_fake::allocations == restore_fake::frees && restore_fake::allocation_size <= 512);
}
