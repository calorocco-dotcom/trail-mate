#include "platform/esp/common/storage/mount_io_state.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
using Sector_t = uint32_t;
using esp_err_t = int;
constexpr int ESP_OK = 0;
#define ESP_LOGW(...) ((void)0)
struct sdmmc_card_t
{
    struct
    {
        uint32_t capacity = 128;
    } csd;
    struct
    {
        int slot = 0;
        bool (*check_buffer_alignment)(int, const void*, size_t) = [](int, const void*, size_t)
        { return true; };
    } host;
};
struct FsBlockDeviceInterface
{
    virtual ~FsBlockDeviceInterface() = default;
    virtual void end() = 0;
    virtual bool isBusy() = 0;
    virtual bool readSector(Sector_t, uint8_t*) = 0;
    virtual bool readSectors(Sector_t, uint8_t*, size_t) = 0;
    virtual Sector_t sectorCount() = 0;
    virtual bool syncDevice() = 0;
    virtual bool writeSector(Sector_t, const uint8_t*) = 0;
    virtual bool writeSectors(Sector_t, const uint8_t*, size_t) = 0;
};
static bool fail = false;
static unsigned transfers = 0;
int sdmmc_read_sectors(sdmmc_card_t*, void* output, Sector_t, size_t count)
{
    ++transfers;
    if (fail) return 1;
    std::memset(output, 0x55, count * 512);
    return 0;
}
int sdmmc_write_sectors(sdmmc_card_t*, const void*, Sector_t, size_t)
{
    ++transfers;
    return fail ? 1 : 0;
}
namespace platform::esp::arduino_common::storage::detail
{
static platform::esp::storage::MountIoState s_mounted;
constexpr size_t kSdSectorSize = 512;
static uint8_t s_dma_sector[512];
#include "idf_sd_block_error_actual.inc"
} // namespace platform::esp::arduino_common::storage::detail
static uint8_t buffer[1024];
int main()
{
    using namespace platform::esp::arduino_common::storage::detail;
    sdmmc_card_t card;
    SdmmcBlockDevice device;
    device.setCard(&card);
    s_mounted = true;
    {
        platform::esp::storage::MountIoErrorScope scope(s_mounted);
        assert(device.readSector(0, buffer) && !scope.error());
    }
    fail = true;
    for (int operation = 0; operation < 4; ++operation)
    {
        platform::esp::storage::MountIoErrorScope scope(s_mounted);
        const auto before = transfers;
        bool result = false;
        switch (operation)
        {
        case 0:
            result = device.readSector(0, buffer);
            break;
        case 1:
            result = device.readSectors(0, buffer, 2);
            break;
        case 2:
            result = device.writeSector(0, buffer);
            break;
        case 3:
            result = device.writeSectors(0, buffer, 2);
            break;
        }
        assert(!result && scope.error() && transfers == before + 1 && s_mounted);
    }
    device.end();
    {
        platform::esp::storage::MountIoErrorScope scope(s_mounted);
        assert(!device.syncDevice() && scope.error());
    }
    return 0;
}
