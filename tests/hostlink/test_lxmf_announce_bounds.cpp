#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
constexpr uint8_t kPropagationMetaName = 1;
#include "lxmf_announce_actual.inc"
int main()
{
    for (const size_t length : {size_t(0), size_t(1), size_t(31), size_t(63), size_t(95), size_t(96), size_t(255), size_t(256), size_t(65535), size_t(65536)})
    {
        const std::string name(length, 'x');
        for (size_t capacity = 0; capacity <= 96; ++capacity)
        {
            std::vector<uint8_t> output(capacity + 32, 0xa5);
            size_t used = capacity;
            const bool ok = packPropagationAnnounceAppData(UINT32_MAX, true, UINT32_MAX, UINT32_MAX, name.c_str(), output.data() + 16, &used);
            for (size_t i = 0; i < 16; ++i) assert(output[i] == 0xa5 && output[capacity + 16 + i] == 0xa5);
            if (ok) assert(used <= capacity && used > 0 && output[16] == 0x97);
            else assert(used == capacity);
        }
    }
    uint8_t output[8]{}, value = 7;
    size_t used = std::numeric_limits<size_t>::max();
    assert(!appendMsgpackBytes(&value, 1, output, sizeof(output), used));
    used = 1;
    assert(!appendMsgpackBytes(&value, std::numeric_limits<size_t>::max(), output, sizeof(output), used));
    assert(used == 1);
}
