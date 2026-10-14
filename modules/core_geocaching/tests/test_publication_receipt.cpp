#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/protocol/publication_receipt.h"
#include "geocaching/storage/confirmed_publication.h"
#include "geocaching/storage/publication_confirmation.h"
#include <array>
#include <fstream>
#include <iterator>
#include <vector>
struct Crypto : geocaching::protocol::RecordCrypto
{
    bool sha256(geocaching::ByteView bytes, uint8_t output[32]) override
    {
        chat::reticulum::fullHash(bytes.data, bytes.size, output);
        return true;
    }
    geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView key, geocaching::ByteView signature, geocaching::ByteView message) override
    {
        return ed25519_verify(signature.data, message.data, message.size, key.data) ? geocaching::protocol::VerificationResult::Valid : geocaching::protocol::VerificationResult::InvalidSignature;
    }
};
int main(int argc, char** argv)
{
    using namespace geocaching;
    using namespace geocaching::protocol;
    if (argc != 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> signed_record((std::istreambuf_iterator<char>(file)), {}), scratch(8192);
    Crypto crypto;
    VerifiedRecordView content;
    if (verifyGeocache({signed_record.data(), signed_record.size()}, crypto, scratch.data(), scratch.size(), content) != VerificationResult::Valid) return 2;
    std::array<uint8_t, 64> public_key{}, private_key{}, signature{};
    std::array<uint8_t, 32> seed{}, hash{};
    for (unsigned i = 0; i < 32; ++i)
    {
        seed[i] = static_cast<uint8_t>(100 + i);
        public_key[i] = static_cast<uint8_t>(i + 1);
    }
    ed25519_create_keypair(public_key.data() + 32, private_key.data(), seed.data());
    std::array<uint8_t, 16> delivery{}, epoch{};
    epoch.fill(3);
    crypto.sha256({public_key.data(), 64}, hash.data());
    std::memcpy(scratch.data() + 10, hash.data(), 16);
    constexpr char name[] = "lxmf.delivery";
    crypto.sha256({reinterpret_cast<const uint8_t*>(name), sizeof(name) - 1}, hash.data());
    std::memcpy(scratch.data(), hash.data(), 10);
    crypto.sha256({scratch.data(), 26}, hash.data());
    std::memcpy(delivery.data(), hash.data(), 16);
    std::array<uint8_t, 256> raw{};
    CmpWriter writer(raw.data(), raw.size());
    if (!writer.array(8) || !writer.unsignedInteger(1) || !writer.binary({delivery.data(), 16}) || !writer.binary({epoch.data(), 16}) ||
        !writer.binary({content.id.bytes.data(), 32}) || !writer.unsignedInteger(content.record.revision) || !writer.binary({content.hash.bytes.data(), 32}) ||
        !writer.unsignedInteger(static_cast<uint8_t>(content.record.state)) || !writer.unsignedInteger(1800000000)) return 3;
    constexpr char domain[] = "trailmate.geocache/receipt/v1";
    std::memcpy(scratch.data(), domain, sizeof(domain));
    std::memcpy(scratch.data() + sizeof(domain), raw.data(), writer.size());
    ed25519_sign(signature.data(), scratch.data(), sizeof(domain) + writer.size(), public_key.data() + 32, private_key.data());
    PublicationReceiptView output;
    const auto verify = [&](uint8_t* workspace, size_t capacity)
    { return verifyPublicationReceipt({raw.data(), writer.size()}, {signature.data(), 64}, {public_key.data(), 64}, {delivery.data(), 16}, {epoch.data(), 16}, content, crypto, workspace, capacity, output); };
    if (verify(scratch.data(), 320) != VerificationResult::Valid || output.revision != content.record.revision) return 4;
    signature[0] ^= 1;
    if (verify(scratch.data(), 320) != VerificationResult::InvalidSignature || output.cache.data) return 5;
    signature[0] ^= 1;
    epoch[0] ^= 1;
    if (verify(scratch.data(), 320) != VerificationResult::IdentityMismatch) return 6;
    epoch[0] ^= 1;
    public_key[0] ^= 1;
    if (verify(scratch.data(), 320) != VerificationResult::IdentityMismatch) return 7;
    public_key[0] ^= 1;
    content.hash.bytes[0] ^= 1;
    if (verify(scratch.data(), 320) != VerificationResult::IdentityMismatch) return 8;
    content.hash.bytes[0] ^= 1;
    if (verify(scratch.data(), 319) != VerificationResult::WorkspaceTooSmall || verify(raw.data(), 320) != VerificationResult::InvalidRecord) return 9;
    output.revision = 123;
    if (verifyPublicationReceipt({reinterpret_cast<const uint8_t*>(&output), sizeof(output)}, {signature.data(), 64}, {public_key.data(), 64},
                                 {delivery.data(), 16}, {epoch.data(), 16}, content, crypto, scratch.data(), 320, output) != VerificationResult::InvalidRecord ||
        output.revision != 123) return 10;
    std::array<uint8_t, 384> stored{};
    size_t stored_size = 0;
    storage::PublicationConfirmationView proof{{raw.data(), writer.size()}, {signature.data(), 64}, {public_key.data(), 64}}, decoded;
    if (!storage::encodePublicationConfirmation(proof, stored.data(), stored.size(), stored_size) ||
        !storage::decodePublicationConfirmation({stored.data(), stored_size}, decoded) || decoded.raw.size != writer.size()) return 11;
    if (storage::verifyPublicationConfirmation({stored.data(), stored_size}, {delivery.data(), 16}, content, crypto, scratch.data(), 320, output) != VerificationResult::Valid) return 12;
    stored[stored_size - 1] ^= 1;
    if (storage::verifyPublicationConfirmation({stored.data(), stored_size}, {delivery.data(), 16}, content, crypto, scratch.data(), 320, output) != VerificationResult::IdentityMismatch) return 13;
    if (storage::decodePublicationConfirmation({stored.data(), stored_size - 1}, decoded) ||
        storage::encodePublicationConfirmation(proof, raw.data(), raw.size(), stored_size) || stored_size) return 14;
    if (!storage::encodePublicationConfirmation(proof, stored.data(), stored.size(), stored_size)) return 15;
    RequestId request_id;
    request_id.bytes.fill(9);
    std::array<uint8_t, 48> request_key{};
    std::memcpy(request_key.data() + 16, delivery.data(), 16);
    std::memcpy(request_key.data() + 32, request_id.bytes.data(), 16);
    std::vector<uint8_t> request_bytes(8192);
    size_t request_size = 0;
    if (!encodePublishRequest(request_id, content.record.encoded, content.signature, 8192, request_bytes.data(), request_bytes.size(), request_size)) return 16;
    VerifiedRecordView recovered;
    if (storage::verifyConfirmedPublication({request_key.data(), 48}, {request_bytes.data(), request_size}, {stored.data(), stored_size}, crypto, scratch.data(), scratch.size(), recovered) != VerificationResult::Valid) return 17;
    request_key[16] ^= 1;
    if (storage::verifyConfirmedPublication({request_key.data(), 48}, {request_bytes.data(), request_size}, {stored.data(), stored_size}, crypto, scratch.data(), scratch.size(), recovered) != VerificationResult::IdentityMismatch || recovered.signature.data) return 18;
    request_key[16] ^= 1;
    request_bytes[request_size - 1] ^= 1;
    if (storage::verifyConfirmedPublication({request_key.data(), 48}, {request_bytes.data(), request_size}, {stored.data(), stored_size}, crypto, scratch.data(), scratch.size(), recovered) != VerificationResult::InvalidSignature) return 19;
    return 0;
}
