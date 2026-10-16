#include "hostlink/identity_backup_request.h"
#include <array>
#include <cassert>
struct Adapter
{
    mutable unsigned calls = 0;
    chat::DeviceAuthorResult result = chat::DeviceAuthorResult::Ok;
    chat::DeviceAuthorResult exportDeviceIdentityBackup(uint32_t generation, uint8_t* output, size_t, size_t& written) const
    {
        assert(generation == 7);
        ++calls;
        output[0] = 0x90;
        written = result == chat::DeviceAuthorResult::Ok ? 1 : 0;
        return result;
    }
};
int main()
{
    using namespace hostlink::management;
    Adapter adapter;
    std::array<uint8_t, 471> output{};
    uint8_t session[16]{};
    uint8_t body[] = {0x92, 7, 0xc3, 0};
    size_t written = 0;
    EnvelopeView request{session, nullptr, uint16_t(Operation::IdentityBackupExport), body, 3};
    assert(processIdentityBackupExport(adapter, request, output.data(), output.size(), written) == Status::Ok && written == 1 && adapter.calls == 1);
    body[2] = 0xc2;
    assert(processIdentityBackupExport(adapter, request, output.data(), output.size(), written) == Status::Unauthorized && adapter.calls == 1);
    body[2] = 1;
    assert(processIdentityBackupExport(adapter, request, output.data(), output.size(), written) == Status::InvalidField && adapter.calls == 1);
    body[2] = 0xc3;
    request.body_size = 4;
    assert(processIdentityBackupExport(adapter, request, output.data(), output.size(), written) == Status::InvalidField && adapter.calls == 1);
    request.body_size = 3;
    adapter.result = chat::DeviceAuthorResult::StaleIdentity;
    assert(processIdentityBackupExport(adapter, request, output.data(), output.size(), written) == Status::StaleIdentity);
    adapter.result = chat::DeviceAuthorResult::Ok;
    std::memcpy(output.data(), body, 3);
    request.body = output.data();
    assert(processIdentityBackupExport(adapter, request, output.data(), output.size(), written) == Status::Ok);
}
