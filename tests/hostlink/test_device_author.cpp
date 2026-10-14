#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/ports/device_author.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include <array>
#include <cassert>
#include <cstdio>

static void hex(const uint8_t* bytes, size_t size)
{
    for (size_t index = 0; index < size; ++index) std::printf("%02x", bytes[index]);
}
int main()
{
    static_assert(sizeof(chat::lxmf::LxmfIdentity) == 200, "Author generation added RAM");
    std::array<uint8_t, 16> session;
    std::array<uint8_t, 32> nonce, seed;
    std::array<uint8_t, 64> public_key, private_key, signature;
    std::array<uint8_t, 160> message;
    session.fill(1);
    nonce.fill(2);
    public_key.fill(3);
    for (size_t index = 0; index < seed.size(); ++index) seed[index] = static_cast<uint8_t>(index);
    ed25519_create_keypair(public_key.data() + 32, private_key.data(), seed.data());
    size_t size = 0;
    message.fill(0xaa);
    assert(!chat::deviceAuthorProofMessage(session.data(), nonce.data(), public_key.data(), 7, message.data(), 1, size));
    assert(size == 0 && message[0] == 0xaa);
    assert(!chat::deviceAuthorProofMessage(message.data(), nonce.data(), public_key.data(), 7, message.data(), message.size(), size));
    assert(!chat::deviceAuthorProofMessage(session.data(), nonce.data(), public_key.data(), 0, message.data(), message.size(), size));
    for (uint32_t generation : std::array<uint32_t, 8>{1, 127, 128, 255, 256, 65535, 65536, UINT32_MAX})
    {
        assert(chat::deviceAuthorProofMessage(session.data(), nonce.data(), public_key.data(), generation, message.data(), message.size(), size));
        assert(size <= 160);
    }
    assert(chat::deviceAuthorProofMessage(session.data(), nonce.data(), public_key.data(), 7, message.data(), message.size(), size));
    ed25519_sign(signature.data(), message.data(), size, public_key.data() + 32, private_key.data());
    assert(ed25519_verify(signature.data(), message.data(), size, public_key.data() + 32));
    message[30] ^= 1;
    assert(!ed25519_verify(signature.data(), message.data(), size, public_key.data() + 32));
    message[30] ^= 1;
    std::printf("{\"publicKey\":\"");
    hex(public_key.data(), public_key.size());
    std::printf("\",\"message\":\"");
    hex(message.data(), size);
    std::printf("\",\"signature\":\"");
    hex(signature.data(), signature.size());
    std::printf("\"}\n");
}
