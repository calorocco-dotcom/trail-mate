#include "platform/esp/common/radio_identity_management.h"
#include <array>
#include <cassert>
#include <iostream>
#include <string>
using namespace platform::esp::common;
using namespace hostlink::management;
struct Identity
{
    std::array<uint8_t, 64> public_key{}, private_key{};
    uint32_t generation = 7;
    bool ready = true, stale_on_sign = false;
    Identity(uint8_t seed_value)
    {
        std::array<uint8_t, 32> seed{};
        seed.fill(seed_value);
        public_key.fill(25);
        ed25519_create_keypair(public_key.data() + 32, private_key.data(), seed.data());
    }
    bool isReady() const { return ready; }
    bool identityStorageError() const { return false; }
    uint32_t authorGeneration() const { return generation; }
    void combinedPublicKey(uint8_t* out) const { std::memcpy(out, public_key.data(), 64); }
    bool sign(const uint8_t* message, size_t size, uint8_t* out) const
    {
        ed25519_sign(out, message, size, public_key.data() + 32, private_key.data());
        if (stale_on_sign) ++const_cast<Identity*>(this)->generation;
        return true;
    }
};
struct Adapter
{
    Identity& identity;
    chat::DeviceAuthorResult readRadioIdentityInfo(uint8_t kind, uint32_t generation, uint8_t* out, size_t capacity, size_t& written) const { return platform::esp::common::readRadioIdentityInfo(identity, kind, generation, out, capacity, written); }
    chat::DeviceAuthorResult exportRadioIdentityBackup(uint8_t kind, uint32_t generation, uint8_t* out, size_t capacity, size_t& written, const uint8_t* session, const uint8_t* request) const { return platform::esp::common::exportRadioIdentityBackup(identity, kind, generation, out, capacity, written, session, request); }
    chat::DeviceIdentityRestoreResult restoreRadioIdentity(const RadioIdentityRestoreView& request, uint8_t* work, size_t size) { return platform::esp::common::restoreRadioIdentity(identity, request, work, size); }
};
int main(int argc, char** argv)
{
    const bool exporting = argc == 2 && std::string(argv[1]) == "--export";
    const bool restoring = argc == 2 && std::string(argv[1]) == "--restore";
    Identity source(19), target(restoring ? 19 : 21);
    Adapter adapter{target};
    std::array<uint8_t, 448> exported{}, request_bytes{};
    std::array<uint8_t, 352> scratch{};
    std::array<uint8_t, 16> session{};
    std::array<uint8_t, 16> correlation{};
    session.fill(2);
    correlation.fill(3);
    std::array<uint8_t, 32> pub{}, seed{};
    std::array<uint8_t, 64> priv{};
    for (uint8_t kind_value : {1, 2})
    {
        fake_nvs::values.clear();
        radio_identity_write_state.store(0);
        auto kind = RadioIdentityKind(kind_value);
        seed.fill(19);
        if (kind_value == 1) ed25519_create_keypair(pub.data(), priv.data(), seed.data());
        else
        {
            for (size_t i = 0; i < 32; ++i) priv[i] = uint8_t(i + 1);
            priv[0] &= 0xf8;
            priv[31] = (priv[31] & 0x3f) | 0x40;
            assert(Curve25519::eval(pub.data(), priv.data(), nullptr));
        }
        assert(commitRadioIdentityRecord(radioIdentityNamespace(kind), kind, 0, pub.data(), priv.data(), scratch.data(), scratch.size(),
                                         [kind](const uint8_t* p, const uint8_t* k, uint8_t* w, size_t size)
                                         { return validateRadioIdentityPair(kind, p, k, w, size); }) == IdentityRecordResult::Committed);
        size_t written = 0;
        // Decode and answer in the same command buffer, as the ESP dispatcher does.
        const std::array<uint8_t, 5> export_body{{0x93, 7, kind_value, 0xc3, 0}};
        assert(encodeEnvelope(session.data(), correlation.data(), uint16_t(Operation::RadioIdentityBackupExport), export_body.data(), 4, exported.data(), exported.size(), written));
        EnvelopeView export_request;
        assert(decodeEnvelope(exported.data(), written, export_request));
        assert(radioIdentityReadOperation(Adapter{source}, export_request, exported.data(), exported.size(), written) == Status::Ok);
        if (exporting)
        {
            std::array<uint8_t, 512> response{};
            size_t response_size = 0;
            assert(encodeEnvelope(session.data(), correlation.data(), uint16_t(Status::Ok), exported.data(), written, response.data(), response.size(), response_size));
            const char* digits = "0123456789abcdef";
            for (size_t i = 0; i < response_size; ++i) std::cout << digits[response[i] >> 4] << digits[response[i] & 15];
            std::cout << '\n';
            continue;
        }
        if (restoring)
        {
            std::string hex;
            assert(std::getline(std::cin, hex) && hex.size() <= 1024 && hex.size() % 2 == 0);
            std::array<uint8_t, 512> request_frame{};
            const auto nibble = [](char c)
            { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                           : -1; };
            for (size_t i = 0; i < hex.size() / 2; ++i)
            {
                const int high = nibble(hex[2 * i]), low = nibble(hex[2 * i + 1]);
                assert(high >= 0 && low >= 0);
                request_frame[i] = uint8_t(high * 16 + low);
            }
            EnvelopeView input;
            assert(decodeEnvelope(request_frame.data(), hex.size() / 2, input));
            RadioIdentityRestoreWork work;
            size_t response_size = 0;
            const auto result = processRadioIdentityRestore(adapter, input, IdentityRestoreActivity{}, work, request_bytes.data(), request_bytes.size(), response_size);
            assert(result == Status::Ok && request_bytes[1] == 0 && request_bytes[2] == kind_value && request_bytes[3] == 2);
            assert(target.generation == 7 && radio_identity_write_state.load() == 2);
            RadioIdentityWork stored;
            uint32_t generation = 0;
            bool present = false;
            assert(readManagedRadioIdentity(kind, stored, generation, present) == IdentityRecordResult::Ready && present && generation == 2);
            assert(std::memcmp(stored.data() + 12, pub.data(), 32) == 0);
            const char* digits = "0123456789abcdef";
            for (size_t i = 0; i < response_size; ++i) std::cout << digits[request_bytes[i] >> 4] << digits[request_bytes[i] & 15];
            std::cout << '\n';
            continue;
        }
        geocaching::protocol::CmpReader reader({exported.data(), written});
        size_t count = 0;
        geocaching::ByteView payload, signature;
        assert(reader.array(count, 2) && reader.binary(payload, 320) && reader.binary(signature, 64) && reader.finished());
        assert(signature.size == 64);
        RadioIdentityRestoreView content;
        assert(parseRadioIdentityBackupPayload(payload, content) == Status::Ok);
        assert(std::memcmp(content.export_session.data, session.data(), 16) == 0 && std::memcmp(content.export_request.data, correlation.data(), 16) == 0);
        content.signature = signature;
        assert(verifyRadioIdentityRestoreSignature(content, scratch.data(), scratch.size()));
        const auto exported_values = fake_nvs::values;
        const auto make_request = [&](uint32_t expected, uint32_t radio_generation, geocaching::ByteView current, bool consent)
        {
            geocaching::protocol::CmpWriter writer(request_bytes.data(), request_bytes.size());
            assert(writer.array(7) && writer.unsignedInteger(expected) && writer.binary({target.public_key.data(), 64}) && writer.binary(payload) && writer.binary(signature) && writer.unsignedInteger(radio_generation) &&
                   (current.size ? writer.binary(current) : writer.nil()));
            request_bytes[writer.size()] = consent ? 0xc3 : 0xc2;
            return EnvelopeView{session.data(), nullptr, uint16_t(Operation::RadioIdentityBackupRestore), request_bytes.data(), writer.size() + 1};
        };
        size_t response_size = 0;
        RadioIdentityRestoreWork work;
        IdentityRestoreActivity activity;
        auto input = make_request(7, 1, {pub.data(), 32}, false);
        std::array<uint8_t, 512> frame{};
        size_t frame_size = 0;
        assert(encodeEnvelope(session.data(), session.data(), input.code, input.body, input.body_size, frame.data(), frame.size(), frame_size));
        EnvelopeView decoded_frame;
        assert(decodeEnvelope(frame.data(), frame_size, decoded_frame));
        assert(decoded_frame.code == input.code && decoded_frame.body_size == input.body_size);
        RadioIdentityRestoreView decoded_request;
        assert(parseRadioIdentityRestoreRequest({decoded_frame.body, decoded_frame.body_size}, decoded_request) == Status::Unauthorized);
        assert(processRadioIdentityRestore(adapter, input, activity, work, request_bytes.data(), request_bytes.size(), response_size) == Status::Unauthorized);
        assert(fake_nvs::values == exported_values);
        input = make_request(8, 1, {pub.data(), 32}, true);
        assert(processRadioIdentityRestore(adapter, input, activity, work, request_bytes.data(), request_bytes.size(), response_size) == Status::StaleIdentity);
        input = make_request(7, 0, {pub.data(), 32}, true);
        assert(processRadioIdentityRestore(adapter, input, activity, work, request_bytes.data(), request_bytes.size(), response_size) == Status::Conflict);
        input = make_request(7, 1, {pub.data(), 32}, true);
        assert(encodeEnvelope(session.data(), session.data(), input.code, input.body, input.body_size, frame.data(), frame.size(), frame_size));
        assert(decodeEnvelope(frame.data(), frame_size, decoded_frame));
        assert(parseRadioIdentityRestoreRequest({decoded_frame.body, decoded_frame.body_size}, decoded_request) == Status::Ok);
        activity.upload = true;
        assert(processRadioIdentityRestore(adapter, input, activity, work, request_bytes.data(), request_bytes.size(), response_size) == Status::Busy);
        activity.upload = false;
        assert(parseRadioIdentityRestoreRequest({input.body, input.body_size}, work.request) == Status::Ok);
        const auto original = work.request;
        work.request.public_key = {seed.data(), 32};
        assert(platform::esp::common::restoreRadioIdentity(target, work.request, scratch.data(), scratch.size()) == chat::DeviceIdentityRestoreResult::InvalidInput);
        work.request = original;
        assert(fake_nvs::values == exported_values);
        input = make_request(7, 1, {pub.data(), 32}, true);
        assert(processRadioIdentityRestore(adapter, input, activity, work, request_bytes.data(), request_bytes.size(), response_size) == Status::Ok);
        assert(response_size >= 37 && request_bytes[1] == 0 && request_bytes[2] == kind_value && request_bytes[3] == 2);
        assert(radio_identity_write_state.load() == 2);
        assert(target.generation == 7); // RF restore did not change the account.
        RadioIdentityWork stored;
        uint32_t generation = 0;
        bool present = false;
        assert(readManagedRadioIdentity(kind, stored, generation, present) == IdentityRecordResult::Ready && present && generation == 2);
        assert(commitRadioIdentityRecord(radioIdentityNamespace(kind), kind, 2, pub.data(), priv.data(), scratch.data(), scratch.size(),
                                         [](const uint8_t*, const uint8_t*, uint8_t*, size_t)
                                         { return true; }) == IdentityRecordResult::TargetChanged);
        radio_identity_write_state.store(0); // Simulated application reboot, no firmware reset invoked.
        input = make_request(7, 2, {pub.data(), 32}, true);
        fake_nvs::fail_commit = true;
        assert(processRadioIdentityRestore(adapter, input, activity, work, request_bytes.data(), request_bytes.size(), response_size) == Status::Ok);
        assert(request_bytes[1] == 1 && request_bytes[3] == 3 && radio_identity_write_state.load() == 2);
        fake_nvs::fail_commit = false;
        radio_identity_write_state.store(0);
        assert(readManagedRadioIdentity(kind, stored, generation, present) == IdentityRecordResult::Ready && generation == 3);
        source.stale_on_sign = true;
        assert(platform::esp::common::exportRadioIdentityBackup(source, kind_value, 7, exported.data(), exported.size(), written, session.data(), session.data()) == chat::DeviceAuthorResult::StaleIdentity);
        source.stale_on_sign = false;
        source.generation = 7;
        for (auto byte : exported) assert(!byte);
    }
    fake_nvs::values.clear();
    size_t written = 0;
    assert(platform::esp::common::readRadioIdentityInfo(target, 1, 7, exported.data(), exported.size(), written) == chat::DeviceAuthorResult::Ok);
    assert(exported[1] == 1 && exported[2] == 0 && exported[3] == 0 && exported[4] == 0xc0);
    assert(platform::esp::common::exportRadioIdentityBackup(target, 1, 7, exported.data(), exported.size(), written, session.data(), session.data()) == chat::DeviceAuthorResult::Unavailable);
    assert(fake_nvs::handles.empty());
}
