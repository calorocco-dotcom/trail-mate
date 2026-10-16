#include "lvgl.h"
#include "ui/support/tmroute_map.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <vector>

namespace device
{
enum Operation
{
    Open,
    Read,
    Seek,
    Size,
    Tell,
    Count
};
std::array<unsigned, Count> pending{}, observed{};
std::vector<uint8_t> bytes;
unsigned handles = 0;
bool hard_read = false;
bool busy(Operation operation)
{
    if (!pending[operation]) return false;
    --pending[operation];
    ++observed[operation];
    return true;
}
} // namespace device

namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return true; }
class SdRuntimeFile
{
  public:
    ~SdRuntimeFile() { close(); }
    bool open(const char*, const char*)
    {
        busy_ = device::busy(device::Open);
        if (busy_) return false;
        opened_ = true;
        ++device::handles;
        return true;
    }
    void close()
    {
        if (opened_) --device::handles;
        opened_ = false;
    }
    int read(void* output, size_t count)
    {
        busy_ = device::busy(device::Read);
        if (device::hard_read) return -1;
        count = std::min(count, device::bytes.size() - position_);
        if (busy_) count /= 2;
        std::memcpy(output, device::bytes.data() + position_, count);
        position_ += count;
        return static_cast<int>(count);
    }
    size_t write(const void*, size_t) { return 0; }
    bool seek(uint64_t position)
    {
        busy_ = device::busy(device::Seek);
        if (busy_ || position > device::bytes.size()) return false;
        position_ = static_cast<size_t>(position);
        return true;
    }
    uint64_t size() const
    {
        busy_ = device::busy(device::Size);
        return busy_ ? 0 : device::bytes.size();
    }
    uint64_t position() const
    {
        busy_ = device::busy(device::Tell);
        return busy_ ? 0 : position_;
    }
    bool read_busy() const { return busy_; }

  private:
    size_t position_ = 0;
    mutable bool busy_ = false;
    bool opened_ = false;
};
} // namespace platform::esp::arduino_common::storage

unsigned busy_marks = 0;
void mark_external_font_fs_busy() { ++busy_marks; }
// Actual production callbacks, including open, read, seek and tell. Only the
// physical SD calls above are fault-injected; LVGL and the reader are real.
#include "lvgl_sd_route_actual.inc"

class ArchiveBytes final : public tmap::WritableRouteFile
{
  public:
    uint64_t size() const override { return device::bytes.size(); }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (offset > size() || count > size() - offset) return tmap::Status::Invalid;
        std::memcpy(output, device::bytes.data() + offset, count);
        return tmap::Status::Ok;
    }
    tmap::Status writeAt(uint64_t offset, const uint8_t* input, size_t count) override
    {
        if (offset + count > size()) device::bytes.resize(static_cast<size_t>(offset + count));
        std::memcpy(device::bytes.data() + offset, input, count);
        return tmap::Status::Ok;
    }
};

void checkDenseViewRecovery()
{
    device::hard_read = false;
    device::bytes.clear();
    ArchiveBytes file;
    tmap::RouteArchiveWorkspace workspace;
    tmap::RouteArchiveWriter writer(workspace);
    assert(writer.begin(file, {}, 0, 0, 1024 * 1024) == tmap::Result::Ok);
    for (uint32_t i = 0; i < 10000; ++i)
    {
        tmap::RouteArchivePoint point;
        point.point = {0, static_cast<int32_t>(i * 1000)};
        point.distance_cm = uint64_t(i) * 1113;
        assert(writer.append(point) == tmap::Result::Ok);
    }
    auto result = tmap::Result::More;
    while (result == tmap::Result::More) result = writer.finishStep(1);
    assert(result == tmap::Result::Ok);
    ui::support::TmRouteMap route;
    assert(route.begin("/routes/navigation/dense.tmroute"));
    uint32_t now = 1000;
    for (unsigned i = 0; i < 2000 && !route.ready(); ++i)
        route.poll(false, {}, now++, 0, false, nullptr, 64);
    assert(route.ready());
    route.setViewport({route.destination(), 0, 320, 200}, nullptr, nullptr);
    for (unsigned i = 0; i < 20000 && route.loadingView(); ++i)
        route.poll(false, {}, now++, 0, false, nullptr, 64);
    assert(!route.loadingView() && route.ready() && route.dense());
    assert(route.error() == tmap::Result::Ok && route.screenSegments() == 0);
    const auto reserved = route.readerBytes();
    route.setViewport({route.destination(), 18, 320, 200}, nullptr, nullptr);
    for (unsigned i = 0; i < 2000 && route.loadingView(); ++i)
        route.poll(false, {}, now++, 0, false, nullptr, 64);
    assert(!route.loadingView() && route.ready() && !route.dense());
    assert(route.error() == tmap::Result::Ok && route.screenSegments() > 0);
    assert(route.readerBytes() == reserved);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    device::bytes = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    assert(!device::bytes.empty());
    lv_init();
    lv_fs_drv_t driver;
    lv_fs_drv_init(&driver);
    driver.letter = 'A';
    driver.ready_cb = sd_fs_ready_cb;
    driver.open_cb = sd_fs_open;
    driver.close_cb = sd_fs_close;
    driver.read_cb = sd_fs_read;
    driver.seek_cb = sd_fs_seek;
    driver.tell_cb = sd_fs_tell;
    lv_fs_drv_register(&driver);
    device::pending = {2, 3, 2, 2, 2};
    {
        ui::support::TmRouteMap route;
        assert(route.begin("/routes/navigation/device.tmroute"));
        uint32_t now = 1000;
        for (unsigned i = 0; i < 500 && !route.ready(); ++i)
        {
            route.poll(false, {}, now++, 0, false, nullptr, 8);
            assert(route.error() == tmap::Result::Ok || route.error() == tmap::Result::More);
        }
        assert(route.ready());
        for (const auto count : device::observed) assert(count > 0);
        assert(busy_marks == 11);
        route.setViewport({route.destination(), 18, 320, 200}, nullptr, nullptr);
        for (unsigned i = 0; i < 500 && route.loadingView(); ++i)
            route.poll(false, {}, now++, 0, false, nullptr, 8);
        assert(route.ready() && route.error() == tmap::Result::Ok && route.screenSegments() > 0);
        // Genuine read errors still fail; Busy must never be required to hide one.
        device::hard_read = true;
        route.setViewport({route.destination(), 17, 320, 200}, nullptr, nullptr);
        for (unsigned i = 0; i < 100 && route.error() == tmap::Result::Ok; ++i)
            route.poll(false, {}, now++, 0, false, nullptr, 8);
        assert(route.error() == tmap::Result::IoError && !route.ready());
    }
    assert(device::handles == 0);
    checkDenseViewRecovery();
    assert(device::handles == 0);
}
