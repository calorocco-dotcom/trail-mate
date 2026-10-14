#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
#include <vector>

namespace sd = platform::esp::arduino_common::storage;
static bool card_ready = true;
static std::map<std::string, std::vector<uint8_t>> files;

namespace platform::esp::arduino_common::storage
{
class SdRuntimeFile::Impl
{
  public:
    std::string path;
    size_t offset = 0;
    bool opened = false;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char* mode)
{
    if (!card_ready) return false;
    impl_->path = path;
    if (mode[0] == 'r' && !files.count(path)) return false;
    auto& content = files[path];
    if (mode[0] == 'w') content.clear();
    impl_->offset = mode[0] == 'a' ? content.size() : 0;
    impl_->opened = true;
    return true;
}
void SdRuntimeFile::close() { impl_->opened = false; }
bool SdRuntimeFile::is_open() const { return impl_->opened; }
bool SdRuntimeFile::flush() { return impl_->opened && card_ready; }
size_t SdRuntimeFile::write(const void* input, size_t bytes)
{
    if (!impl_->opened || !card_ready) return 0;
    auto& content = files[impl_->path];
    content.resize(impl_->offset + bytes);
    std::memcpy(content.data() + impl_->offset, input, bytes);
    impl_->offset += bytes;
    return bytes;
}
int SdRuntimeFile::read(void* output, size_t bytes)
{
    if (!impl_->opened || !card_ready) return 0;
    const auto& content = files.at(impl_->path);
    bytes = std::min(bytes, content.size() - impl_->offset);
    std::memcpy(output, content.data() + impl_->offset, bytes);
    impl_->offset += bytes;
    return static_cast<int>(bytes);
}
size_t SdRuntimeFile::print(const char* text) { return write(text, std::strlen(text)); }
size_t SdRuntimeFile::printf(const char* format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    const int count = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    assert(count >= 0 && static_cast<size_t>(count) < sizeof(buffer));
    return write(buffer, static_cast<size_t>(count));
}
bool sd_card_ready() { return card_ready; }
bool sd_exists(const char* path) { return files.count(path) != 0; }
bool sd_is_directory(const char*) { return card_ready; }
bool sd_mkdir(const char*) { return card_ready; }
bool sd_remove(const char* path) { return files.erase(path) != 0; }
SdRuntimeDir::SdRuntimeDir() : impl_(nullptr) {}
SdRuntimeDir::~SdRuntimeDir() = default;
bool SdRuntimeDir::open(const char*) { return false; }
void SdRuntimeDir::close() {}
bool SdRuntimeDir::read_next(char*, size_t, bool*) { return false; }
} // namespace platform::esp::arduino_common::storage

static std::string content(const std::string& path)
{
    const auto& bytes = files.at(path);
    return {bytes.begin(), bytes.end()};
}

struct SettingWriter
{
    gps::TrackRecorder* recorder;
    unsigned worker;
};
#if defined(_WIN32)
static DWORD WINAPI write_settings(void* context)
#else
static void write_settings(void* context)
#endif
{
    const auto& writer = *static_cast<SettingWriter*>(context);
    for (unsigned update = 0; update < 1000; ++update)
    {
        writer.recorder->setFormat(static_cast<gps::TrackFormat>((writer.worker + update) % 3));
        writer.recorder->setAutoRecording((writer.worker + update) % 2);
    }
#if defined(_WIN32)
    return 0;
#endif
}

int main()
{
    auto& recorder = gps::TrackRecorder::getInstance();
    recorder.setAutoRecording(false);
    assert(recorder.tryAcquireRead(true));
    recorder.setFormat(gps::TrackFormat::CSV);
    recorder.setFormat(gps::TrackFormat::Binary);
    recorder.setAutoRecording(true);
    recorder.setAutoRecording(false);
    assert(!recorder.isRecording());
    recorder.releaseRead();
    assert(recorder.start());
    assert(recorder.currentPath().find(".bin") != std::string::npos);
    assert(content(recorder.currentPath()).substr(0, 4) == "TRK1");
    recorder.stop();

    // Hold the actual recorder read lock while concurrent setting writers run.
    assert(recorder.tryAcquireRead(true));
    SettingWriter requests[4];
#if defined(_WIN32)
    HANDLE writers[4];
#else
    std::vector<std::thread> writers;
#endif
    for (unsigned worker = 0; worker < 4; ++worker)
    {
        requests[worker] = {&recorder, worker};
#if defined(_WIN32)
        writers[worker] = CreateThread(nullptr, 0, write_settings, &requests[worker], 0, nullptr);
        assert(writers[worker]);
#else
        writers.emplace_back(write_settings, &requests[worker]);
#endif
    }
#if defined(_WIN32)
    assert(WaitForMultipleObjects(4, writers, TRUE, INFINITE) == WAIT_OBJECT_0);
    for (const auto writer : writers) assert(CloseHandle(writer));
#else
    for (auto& writer : writers) writer.join();
#endif
    recorder.setFormat(gps::TrackFormat::CSV);
    recorder.setAutoRecording(false);
    recorder.releaseRead();
    assert(recorder.start());
    assert(recorder.currentPath().find(".csv") != std::string::npos);
    recorder.stop();

    card_ready = false;
    recorder.setAutoRecording(true);
    assert(!recorder.isRecording());
    card_ready = true;
    test_clock += 999;
    recorder.applyPendingSettings();
    assert(!recorder.isRecording());
    test_clock += 1;
    recorder.applyPendingSettings();
    assert(recorder.isRecording());
    const std::string automatic_path = recorder.currentPath();
    recorder.setAutoRecording(true);
    recorder.applyPendingSettings();
    assert(recorder.currentPath() == automatic_path);
    recorder.setAutoRecording(false);
    assert(!recorder.isRecording());

    recorder.setFormat(gps::TrackFormat::GPX);
    assert(recorder.start());
    const std::string old_path = recorder.currentPath();
    gps::TrackPoint point;
    point.lat = 30.0;
    point.lon = 120.0;
    point.satellites = 6;
    point.timestamp = 1700000000;
    recorder.appendPoint(point); // remains pending until format rotation
    assert(content(old_path).find("<trkpt") == std::string::npos);
    recorder.setFormat(gps::TrackFormat::CSV);
    assert(content(old_path).find("<trkpt lat=\"30.000000\"") != std::string::npos);
    assert(content(old_path).find("</gpx>") != std::string::npos);
    assert(content(recorder.currentPath()).find("lat,lon,ts,sat\n") == 0);
    recorder.stop();
    std::puts("Actual recorder deferred settings, concurrent writers, retry, and old-format pending-point preservation passed.");
}
