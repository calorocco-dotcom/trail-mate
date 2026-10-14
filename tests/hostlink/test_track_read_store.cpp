#include "track_sharing/read_response_store.h"
#include <array>
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct Io
{
    enum class Mode
    {
        Read,
        Update,
        CreateTemporary
    };
    enum class OpenStatus
    {
        Ready,
        Missing,
        IoError
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    std::string path;
    size_t offset = 0, writes = 0;
    bool active = false, short_write = false, short_read = false, fail_sync = false, fail_publish = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        offset = 0;
        if (mode != Mode::CreateTemporary && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        assert(active);
        active = false;
        return true;
    }
    bool size(void*, uint64_t& size)
    {
        size = files[path].size();
        return true;
    }
    bool seek(void*, uint64_t position)
    {
        offset = size_t(position);
        return offset <= files[path].size();
    }
    size_t read(void*, void* output, size_t count)
    {
        count = std::min(count, files[path].size() - offset);
        if (short_read && count) count--;
        std::memcpy(output, files[path].data() + offset, count);
        offset += count;
        return count;
    }
    size_t write(void*, const void* input, size_t count)
    {
        assert(count <= 320);
        writes++;
        if (short_write && count) count--;
        const auto* data = static_cast<const uint8_t*>(input);
        files[path].insert(files[path].end(), data, data + count);
        return count;
    }
    bool sync(void*) { return !fail_sync; }
    bool publish(const char* temporary, const char* destination)
    {
        assert(!active);
        if (fail_publish || files.count(destination) || !files.count(temporary)) return false;
        files[destination] = files[temporary];
        files.erase(temporary);
        return true;
    }
};
int main()
{
    Io io;
    track_sharing::ReadResponseStore<Io> store(io, "response.tmp", "response.ready");
    static_assert(sizeof(store) <= 32, "Store cannot retain response buffers");
    std::array<uint8_t, 128> scratch{};
    std::array<uint8_t, 320> output{};
    std::array<uint8_t, 8192> source{};
    for (size_t i = 0; i < source.size(); i++) source[i] = uint8_t(i);
    const geocaching::ByteView raw{source.data(), source.size()};
    using Result = track_sharing::ReadStoreResult;
    io.short_write = true;
    assert(store.persist(raw, scratch.data(), scratch.size()) == Result::StorageFailure && !io.active && !io.files.count("response.ready"));
    io.short_write = false;
    io.fail_sync = true;
    assert(store.persist(raw, scratch.data(), scratch.size()) == Result::StorageFailure && !io.active);
    const size_t unsynced_writes = io.writes;
    assert(store.persist(raw, scratch.data(), scratch.size()) == Result::StorageFailure && !io.active && !io.files.count("response.ready") && io.writes == unsynced_writes);
    io.fail_sync = false;
    io.fail_publish = true;
    assert(store.persist(raw, scratch.data(), scratch.size()) == Result::StorageFailure && !io.active);
    io.fail_publish = false;
    const size_t staged_writes = io.writes;
    assert(store.persist(raw, scratch.data(), scratch.size()) == Result::Ready && io.writes == staged_writes && !io.active);
    const size_t before = io.writes;
    assert(store.persist(raw, scratch.data(), scratch.size()) == Result::Ready && io.writes == before);
    source[0] ^= 1;
    assert(store.persist(raw, scratch.data(), scratch.size()) == Result::Conflict && io.writes == before);
    source[0] ^= 1;
    size_t written = 0;
    for (size_t offset = 0; offset < source.size();)
    {
        const auto count = uint16_t(std::min(size_t(320), source.size() - offset));
        assert(store.read(source.size(), offset, count, output.data(), output.size(), written) == Result::Ready && written == count && !std::memcmp(output.data(), source.data() + offset, count) && !io.active);
        offset += count;
    }
    io.short_read = true;
    assert(store.read(source.size(), 0, 320, output.data(), output.size(), written) == Result::StorageFailure && !written && !io.active);
    io.short_read = false;
    assert(store.read(source.size(), source.size(), 1, output.data(), output.size(), written) == Result::Invalid);
    io.files["response.ready"].pop_back();
    assert(store.read(source.size(), 0, 1, output.data(), output.size(), written) == Result::Conflict && !written && !io.active);
    assert(store.persist(raw, source.data(), 128) == Result::Invalid);
}
