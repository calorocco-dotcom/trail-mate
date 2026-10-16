#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include <array>
#include <cassert>

// Only initialization is a fixture; copying uses the production identity
// class, member layout and inline method. No real keys or NVS are accessed.
bool chat::lxmf::LxmfIdentity::init()
{
    enc_priv_.fill(11);
    sig_priv_.fill(22);
    author_generation_ = 7;
    ready_ = true;
    return true;
}
int main()
{
    chat::lxmf::LxmfIdentity identity;
    std::array<uint8_t, 100> output;
    output.fill(99);
    assert(!identity.copyNativePrivateKey(7, output.data(), output.size()));
    for (size_t i = 0; i < 96; ++i) assert(output[i] == 0);
    assert(output[96] == 99);
    assert(identity.init());
    assert(identity.copyNativePrivateKey(7, output.data(), output.size()));
    for (size_t i = 0; i < 32; ++i) assert(output[i] == 11);
    for (size_t i = 32; i < 96; ++i) assert(output[i] == 22);
    assert(output[96] == 99);
    assert(!identity.copyNativePrivateKey(8, output.data(), output.size()));
    for (size_t i = 0; i < 96; ++i) assert(output[i] == 0);
    output.fill(99);
    assert(!identity.copyNativePrivateKey(7, output.data(), 95));
    for (size_t i = 0; i < 95; ++i) assert(output[i] == 0);
    assert(output[95] == 99);
    assert(!identity.copyNativePrivateKey(7, reinterpret_cast<uint8_t*>(&identity), sizeof(identity)));
    assert(identity.isReady() && identity.authorGeneration() == 7);
    assert(identity.copyNativePrivateKey(7, output.data(), output.size()));
}
