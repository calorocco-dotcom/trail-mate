#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include <cassert>
namespace runtime = platform::esp::arduino_common::storage;
static uint32_t session = 1;
static bool external = false, replace_on_open = false, replace_on_probe = false;
static unsigned opens = 0, publishes = 0;
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return true; }
bool sd_external_block_owner_active() { return external; }
uint32_t sd_media_session() { return session; }
SdFileReadResult sd_read_file(const char*, void*, size_t)
{
    if (replace_on_probe)
    {
        ++session;
        replace_on_probe = false;
    }
    return {SdFileReadStatus::Missing};
}
bool sd_rename(const char*, const char*, uint32_t expected)
{
    if (expected != session || external) return false;
    ++publishes;
    return true;
}
bool SdRuntimeFile::open(const char*, const char*, uint32_t expected)
{
    if (replace_on_open)
    {
        ++session;
        replace_on_open = false;
    }
    if (expected != session || external) return false;
    session_ = expected;
    ++opens;
    return true;
}
bool SdRuntimeFile::is_open() const { return session_ == session && !external; }
} // namespace platform::esp::arduino_common::storage
int main()
{
    runtime::SdRuntimeFile file;
    runtime::SdBorrowedRecordFileIo io(file, session);
    using Mode = runtime::SdBorrowedRecordFileIo::Mode;
    using Status = runtime::SdBorrowedRecordFileIo::OpenStatus;
    auto first = io.open("snapshot", Mode::Read);
    assert(first.status == Status::Ready && first.handle == &file);
    assert(!io.open("second", Mode::Read).handle && opens == 1);
    assert(io.close(first.handle) && !file.is_open());
    auto second = io.open("snapshot", Mode::Read);
    assert(second.handle == first.handle && opens == 2);
    ++session;
    uint8_t byte = 0;
    assert(io.read(second.handle, &byte, 1) == 0 && !io.sync(second.handle));
    assert(!io.close(second.handle));
    assert(io.open("snapshot", Mode::Read).status == Status::Unavailable);
    assert(!io.publish("pending", "snapshot"));
    runtime::SdBorrowedRecordFileIo current(file, session);
    replace_on_open = true;
    assert(current.open("snapshot", Mode::Read).status == Status::Unavailable);
    runtime::SdBorrowedRecordFileIo newest(file, session);
    replace_on_probe = true;
    assert(!newest.publish("pending", "snapshot") && publishes == 0);
    runtime::SdBorrowedRecordFileIo stable(file, session);
    external = true;
    assert(stable.open("snapshot", Mode::Read).status == Status::Unavailable);
    external = false;
    assert(stable.publish("pending", "snapshot") && publishes == 1);
    {
        runtime::SdBorrowedRecordFileIo scoped(file, session);
        assert(scoped.open("snapshot", Mode::Read).handle);
    }
    assert(!file.is_open());
}
