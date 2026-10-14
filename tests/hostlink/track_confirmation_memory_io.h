#pragma once
#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct TrackConfirmationMemoryIo
{
    enum class Mode
    {
        Read,
        CreateTemporary
    };
    enum class OpenStatus
    {
        Ready,
        Missing
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    std::string path, fail_create;
    size_t offset = 0;
    bool active = false;
    unsigned writes = 0;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        offset = 0;
        if (mode == Mode::CreateTemporary && path == fail_create) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::Read && !files.count(path)) return {nullptr, OpenStatus::Missing};
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
    bool seek(void*, uint64_t value)
    {
        offset = size_t(value);
        return offset <= files[path].size();
    }
    size_t read(void*, uint8_t* output, size_t count)
    {
        assert(active && count <= 320);
        count = std::min(count, files[path].size() - offset);
        std::memcpy(output, files[path].data() + offset, count);
        offset += count;
        return count;
    }
    size_t write(void*, const uint8_t* input, size_t count)
    {
        assert(active && count <= 320);
        files[path].resize(std::max(files[path].size(), offset + count));
        std::memcpy(files[path].data() + offset, input, count);
        offset += count;
        ++writes;
        return count;
    }
    bool sync(void*) { return true; }
    bool publish(const char* temporary, const char* destination)
    {
        assert(!active);
        if (!files.count(temporary) || files.count(destination)) return false;
        files[destination] = files[temporary];
        files.erase(temporary);
        return true;
    }
};
