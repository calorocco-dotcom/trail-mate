#include "platform/esp/common/storage/track_snapshot_service.h"
#include <cassert>
#include <string>
#include <vector>
using namespace platform::esp::storage;
using namespace hostlink::management;
struct MemoryFile final : RecordFileIo
{
    std::vector<uint8_t> data;
    size_t position = 0;
    std::string path;
    bool closed = true, fail_close = false;
    OpenStatus state = OpenStatus::Ready;
    OpenResult open(const char* name, Mode mode) override
    {
        assert(mode == Mode::Read);
        path = name;
        if (state != OpenStatus::Ready) return {nullptr, state};
        closed = false;
        position = 0;
        return {this, state};
    }
    bool close(Handle) override
    {
        closed = true;
        return !fail_close;
    }
    bool size(Handle, uint64_t& bytes) override
    {
        bytes = data.size();
        return true;
    }
    bool seek(Handle, uint64_t offset) override
    {
        position = size_t(offset);
        return position <= data.size();
    }
    size_t read(Handle, void* output, size_t bytes) override
    {
        if (position > data.size()) return 0;
        bytes = std::min(bytes, data.size() - position);
        std::memcpy(output, data.data() + position, bytes);
        position += bytes;
        return bytes;
    }
    size_t write(Handle, const void* input, size_t bytes) override
    {
        if (position + bytes > data.size()) data.resize(position + bytes);
        std::memcpy(data.data() + position, input, bytes);
        position += bytes;
        return bytes;
    }
    bool sync(Handle) override { return true; }
    bool publish(const char*, const char*) override { return false; }
};
int main()
{
    MemoryFile io;
    TrackSnapshotMetadata metadata;
    metadata.id[0] = 1;
    metadata.hash[0] = 2;
    metadata.bytes = 750;
    metadata.format = 1;
    assert(writeSnapshotContainerHeader(io, &io, metadata));
    io.data.resize(814);
    for (size_t i = 64; i < io.data.size(); ++i) io.data[i] = uint8_t(i);
    uint8_t payload[96], output[471];
    const auto request = [&](uint32_t offset, bool hash)
    {geocaching::protocol::CmpWriter writer(payload,sizeof(payload));assert(writer.array(4)&&writer.binary({metadata.id,16})&&writer.unsignedInteger(offset)&&writer.unsignedInteger(320));assert(hash?writer.binary({metadata.hash,32}):writer.nil());EnvelopeView view;view.body=payload;view.body_size=writer.size();return view; };
    size_t written = 0;
    assert(readTrackSnapshotService(request(0, false), io, output, sizeof(output), written) == Status::Ok && written > 320 && io.closed);
    assert(io.path == "/trailmate/web-tracks/01000000000000000000000000000000.tsn");
    assert(readTrackSnapshotService(request(640, true), io, output, sizeof(output), written) == Status::Ok && written < 200 && io.closed);
    assert(readTrackSnapshotService(request(750, true), io, output, sizeof(output), written) == Status::InvalidField && written == 0 && io.closed);
    metadata.hash[0] = 3;
    assert(readTrackSnapshotService(request(320, true), io, output, sizeof(output), written) == Status::Conflict && written == 0 && io.closed);
    metadata.hash[0] = 2;
    io.state = RecordFileIo::OpenStatus::Missing;
    assert(readTrackSnapshotService(request(0, false), io, output, sizeof(output), written) == Status::Expired && written == 0);
    io.state = RecordFileIo::OpenStatus::Unavailable;
    assert(readTrackSnapshotService(request(0, false), io, output, sizeof(output), written) == Status::StorageUnavailable && written == 0);
    io.state = RecordFileIo::OpenStatus::Ready;
    io.fail_close = true;
    assert(readTrackSnapshotService(request(0, false), io, output, sizeof(output), written) == Status::StorageUnavailable && written == 0 && io.closed);
    assert(readTrackSnapshotService(request(1, false), io, output, sizeof(output), written) == Status::InvalidField && written == 0);
}
