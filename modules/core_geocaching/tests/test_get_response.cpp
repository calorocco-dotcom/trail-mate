#include "geocaching/protocol/get_response.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 3);
    std::ifstream response_file(argv[1], std::ios::binary), signed_file(argv[2], std::ios::binary);
    assert(response_file.good() && signed_file.good());
    std::vector<std::uint8_t> response((std::istreambuf_iterator<char>(response_file)), {});
    std::vector<std::uint8_t> signed_cache((std::istreambuf_iterator<char>(signed_file)), {});
    geocaching::RequestId id;
    id.bytes.fill(4);
    geocaching::protocol::GetResponseView out;
    using geocaching::protocol::decodeGetResponse;
    assert(decodeGetResponse({response.data(), response.size()}, id, 8192, out));
    assert(out.is_current && !out.has_conflict);
    assert(out.signed_cache.size == signed_cache.size());
    assert(std::memcmp(out.signed_cache.data, signed_cache.data(), signed_cache.size()) == 0);
    for (std::size_t n = 0; n < response.size(); ++n)
    {
        assert(!decodeGetResponse({response.data(), n}, id, 8192, out));
        assert(out.signed_cache.data == nullptr);
    }
    response.back() = 2;
    assert(!decodeGetResponse({response.data(), response.size()}, id, 8192, out));
    response.back() = 0;
    id.bytes[0] = 5;
    assert(!decodeGetResponse({response.data(), response.size()}, id, 8192, out));
    id.bytes.fill(4);
    std::vector<uint8_t> missing{0x96, 1, 1, 3, 0xc4, 16};
    missing.insert(missing.end(), id.bytes.begin(), id.bytes.end());
    missing.insert(missing.end(), {0xcd, 1, 0x94, 0x93, 0xa9, 'n', 'o', 't', '_', 'f', 'o', 'u', 'n', 'd', 0xc0, 0xc0});
    using geocaching::protocol::decodeGetNotFound;
    assert(decodeGetNotFound({missing.data(), missing.size()}, id, 8192));
    for (size_t n = 0; n < missing.size(); ++n) assert(!decodeGetNotFound({missing.data(), n}, id, 8192));
    auto malformed = missing;
    malformed.push_back(0);
    assert(!decodeGetNotFound({malformed.data(), malformed.size()}, id, 8192));
    malformed = missing;
    malformed.back() = 0;
    assert(!decodeGetNotFound({malformed.data(), malformed.size()}, id, 8192));
    for (const uint16_t status : {uint16_t(304), uint16_t(400), uint16_t(409), uint16_t(410), uint16_t(429), uint16_t(503)})
    {
        malformed = missing;
        malformed[23] = static_cast<uint8_t>(status >> 8);
        malformed[24] = static_cast<uint8_t>(status);
        assert(!decodeGetNotFound({malformed.data(), malformed.size()}, id, 8192));
    }
    id.bytes[0] ^= 1;
    assert(!decodeGetNotFound({missing.data(), missing.size()}, id, 8192));
    id.bytes[0] ^= 1;
    missing[3] = 2;
    assert(!decodeGetNotFound({missing.data(), missing.size()}, id, 8192));
    missing[3] = 3;
    missing[missing.size() - 8] = 'x';
    assert(!decodeGetNotFound({missing.data(), missing.size()}, id, 8192));
}
