#include "hostlink/hostlink_codec.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

int main()
{
    std::array<uint8_t, hostlink::kMaxFrameLen + hostlink::kHeaderSize + hostlink::kCrcSize + 2> arena;
    std::array<uint8_t, hostlink::kMaxFrameLen> payload;
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);
    for (size_t len = 0; len <= payload.size(); ++len)
    {
        arena.fill(0xA5);
        size_t written = 0;
        const size_t capacity = hostlink::kHeaderSize + len + hostlink::kCrcSize;
        assert(hostlink::encode_frame_into(0x85, 0x1234, payload.data(), len,
                                           arena.data() + 1, capacity, written));
        std::vector<uint8_t> reference;
        assert(hostlink::encode_frame(0x85, 0x1234, payload.data(), len, reference));
        assert(written == reference.size());
        assert(std::equal(reference.begin(), reference.end(), arena.begin() + 1));
        assert(arena[0] == 0xA5 && arena[capacity + 1] == 0xA5);
        arena.fill(0xA5);
        assert(!hostlink::encode_frame_into(0x85, 0, payload.data(), len,
                                            arena.data() + 1, capacity - 1, written));
        assert(written == 0);
        assert(std::all_of(arena.begin(), arena.end(), [](uint8_t b)
                           { return b == 0xA5; }));
    }
    size_t written = 100;
    assert(!hostlink::encode_frame_into(1, 1, nullptr, 1, arena.data(), arena.size(), written));
    assert(written == 0);
    std::copy(payload.begin(), payload.end(), arena.begin());
    assert(hostlink::encode_frame_into(0x85, 4, arena.data(), payload.size(),
                                       arena.data(), arena.size(), written));
    hostlink::Decoder decoder;
    decoder.push(arena.data(), written);
    hostlink::Frame frame;
    assert(decoder.next(frame));
    assert(std::equal(frame.payload.begin(), frame.payload.end(), payload.begin()));
}
