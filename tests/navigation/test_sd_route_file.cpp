#include "platform/esp/arduino_common/navigation/sd_route_file.h"
#include <array>
int main()
{
    namespace sd = platform::esp::arduino_common::storage;
    using File = platform::esp::navigation::SdRouteFile;
    using Status = tmap::Status;
    File output, other;
    const uint8_t bytes[] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::array<uint8_t, 8> result{};
    assert(output.openNew("/route.tmp", 1));
    sd::partial_write = true;
    assert(output.writeAt(0, bytes, 8) == Status::Busy && output.size() == 4);
    assert(output.writeAt(0, bytes, 8) == Status::Ok && output.size() == 8);
    assert(!other.openNew("/route.tmp", 1));
    assert(sd::files.at("/route.tmp").size() == 8);
    sd::partial_read = true;
    assert(output.readAt(0, result.data(), 8) == Status::Busy);
    assert(output.readAt(0, result.data(), 8) == Status::Ok && !std::memcmp(bytes, result.data(), 8));
    // Reverse leg-spool writes and aligned archive pages can start beyond EOF.
    // Physical SdFat seek cannot create a hole, unlike desktop stdio.
    sd::partial_write = true;
    auto grown = output.writeAt(4096, bytes, 8);
    assert(grown == Status::Busy && output.size() > 8 && output.size() < 4096);
    unsigned growth_steps = 0;
    while (grown == Status::Busy || grown == Status::More)
    {
        assert(++growth_steps < 32);
        const auto previous_size = output.size();
        grown = output.writeAt(4096, bytes, 8);
        assert(output.size() - previous_size <= 256);
    }
    assert(grown == Status::Ok && output.size() == 4104);
    const auto& stored = sd::files.at("/route.tmp");
    assert(std::all_of(stored.begin() + 8, stored.begin() + 4096, [](uint8_t byte)
                       { return byte == 0; }));
    assert(output.readAt(4096, result.data(), 8) == Status::Ok && !std::memcmp(bytes, result.data(), 8));
    assert(output.flush());
    output.close();
    assert(output.openRead("/route.tmp", 1) == Status::Ok && output.size() == 4104);
    ++sd::media;
    assert(output.readAt(0, result.data(), 8) != Status::Ok);
    assert(output.writeAt(0, bytes, 8) != Status::Ok && !output.flush());
    assert(!other.openNew("/replacement.tmp", 1));
    output.close();
    assert(output.openRead("/route.tmp", 2) == Status::Ok);
    sd::ready = false;
    assert(output.readAt(0, result.data(), 8) != Status::Ok);
}
