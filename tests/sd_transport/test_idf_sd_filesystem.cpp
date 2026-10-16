#include "platform/esp/arduino_common/storage/sd_transfer_policy.h"
#include "platform/esp/common/storage/mount_io_state.h"
#include <FsLib/FsLib.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace platform::esp::arduino_common::storage;
using esp_err_t = int;
constexpr int ESP_OK = 0;
#define ESP_LOGW(...) ((void)0)
struct sdmmc_card_t
{
    struct
    {
        uint32_t capacity;
    } csd;
    struct
    {
        int slot = 0;
        bool (*check_buffer_alignment)(int, const void*, size_t) = nullptr;
    } host;
};
namespace
{
std::unordered_map<uint32_t, std::array<uint8_t, 512>> disk;
size_t read_calls = 0;
size_t max_read = 0;
size_t max_write = 0;
int fail_on_read = 0;
} // namespace

int sdmmc_read_sectors(sdmmc_card_t* card, void* output, size_t sector, size_t count)
{
    assert(count > 0 && sector <= card->csd.capacity && count <= card->csd.capacity - sector);
    assert(reinterpret_cast<uintptr_t>(output) % 64 == 0);
    ++read_calls;
    if (fail_on_read != 0 && read_calls == static_cast<size_t>(fail_on_read)) return -1;
    max_read = std::max(max_read, count);
    auto* bytes = static_cast<uint8_t*>(output);
    for (size_t index = 0; index < count; ++index)
    {
        auto found = disk.find(static_cast<uint32_t>(sector + index));
        if (found == disk.end()) std::memset(bytes + index * 512, 0, 512);
        else std::memcpy(bytes + index * 512, found->second.data(), 512);
    }
    return ESP_OK;
}

int sdmmc_write_sectors(sdmmc_card_t* card, const void* input, size_t sector, size_t count)
{
    assert(count > 0 && sector <= card->csd.capacity && count <= card->csd.capacity - sector);
    assert(reinterpret_cast<uintptr_t>(input) % 64 == 0);
    max_write = std::max(max_write, count);
    auto* bytes = static_cast<const uint8_t*>(input);
    for (size_t index = 0; index < count; ++index)
    {
        const auto key = static_cast<uint32_t>(sector + index);
        const auto* block = bytes + index * 512;
        if (std::all_of(block, block + 512, [](uint8_t byte)
                        { return byte == 0; })) disk.erase(key);
        else std::memcpy(disk[key].data(), block, 512);
    }
    return ESP_OK;
}

namespace platform::esp::arduino_common::storage::detail
{
constexpr size_t kSdSectorSize = 512;
alignas(64) uint8_t s_dma_sector[SdmmcSdTransferPolicy::dma_buffer_bytes];
platform::esp::storage::MountIoState s_mounted;
#include "idf_sd_block_actual.inc"
} // namespace platform::esp::arduino_common::storage::detail

void round_trip(uint8_t filesystem, uint32_t capacity)
{
    using namespace platform::esp::arduino_common::storage::detail;
    disk.clear();
    sdmmc_card_t card{{capacity}, {}};
    SdmmcBlockDevice device;
    device.setCard(&card);
    std::array<uint8_t, 512> scratch{};
    if (filesystem == 64)
    {
        ExFatFormatter formatter;
        assert(formatter.format(&device, scratch.data()));
    }
    else
    {
        FatFormatter formatter;
        assert(formatter.format(&device, scratch.data()));
    }
    FsVolume volume;
    assert(volume.begin(&device) && volume.fatType() == filesystem);
    auto file = volume.open("batch.bin", O_CREAT | O_RDWR | O_TRUNC);
    auto neighbor = volume.open("neighbor.bin", O_CREAT | O_RDWR | O_TRUNC);
    assert(file && neighbor);
    std::vector<uint8_t> expected(70 * 1024 + 37);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>((i * 13 + i / 512) % 251);
    max_write = 0;
    for (size_t offset = 0; offset < expected.size();)
    {
        const auto size = std::min(ActiveSdTransferPolicy::file_slice_bytes, expected.size() - offset);
        assert(file.write(expected.data() + offset, size) == size);
        assert(neighbor.write(scratch.data(), scratch.size()) == scratch.size());
        offset += size;
    }
    assert(file.sync() && file.close() && neighbor.close());
    assert(max_write > 1 && max_write <= 8);
    volume.end();
    assert(volume.begin(&device));
    size_t baseline_calls = 0;
    for (size_t slice : {size_t(512), ActiveSdTransferPolicy::file_slice_bytes})
    {
        file = volume.open("batch.bin", O_RDONLY);
        assert(file && file.fileSize() == expected.size());
        std::vector<uint8_t> result(expected.size() + 2, 0xA5);
        read_calls = max_read = 0;
        for (size_t offset = 0; offset < expected.size();)
        {
            const auto size = std::min(slice, expected.size() - offset);
            assert(file.read(result.data() + 1 + offset, size) == static_cast<int>(size));
            offset += size;
        }
        assert(result.front() == 0xA5 && result.back() == 0xA5);
        assert(std::memcmp(result.data() + 1, expected.data(), expected.size()) == 0);
        if (slice == 512) baseline_calls = read_calls;
        else
        {
            assert(max_read > 1 && max_read <= 8 && read_calls < baseline_calls);
            std::cout << "IDF FAT " << static_cast<int>(filesystem) << ": "
                      << baseline_calls << " -> " << read_calls << " physical reads\n";
        }
        for (uint32_t offset : {1U, 511U, 8191U, 16383U, 65535U})
        {
            assert(file.seekSet(offset));
            std::array<uint8_t, 4096> partial{};
            assert(file.read(partial.data(), partial.size()) == static_cast<int>(partial.size()));
            assert(std::memcmp(partial.data(), expected.data() + offset, partial.size()) == 0);
        }
        assert(file.close());
    }
    volume.end();
    // A failure on the second bounce batch is surfaced and stops the transfer.
    std::vector<uint8_t> result(19 * 512 + 1);
    read_calls = 0;
    fail_on_read = 2;
    platform::esp::storage::MountIoErrorScope scope(s_mounted);
    assert(!device.readSectors(0, result.data() + 1, 19));
    assert(scope.error() && read_calls == 2);
    fail_on_read = 0;
}

int main()
{
    static_assert(ActiveSdTransferPolicy::file_slice_bytes == 16384);
    round_trip(16, 65536);
    round_trip(32, 8 * 1024 * 1024);
    round_trip(64, 1024 * 1024);
}
