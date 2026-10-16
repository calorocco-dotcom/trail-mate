#pragma once
#include "music/storage.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
struct MemoryStorage : music::Storage
{
    using Bytes = std::vector<uint8_t>;
    std::map<std::string, Bytes> files;
    std::set<std::string> directories{"/", "/Music"};
    unsigned reads = 0, read_bytes = 0, opens = 0;
    uint32_t media_session = 1;
    bool online = true, fail_write = false;
    music::StorageResult result = music::StorageResult::Ok;
    struct Handle : music::File
    {
        MemoryStorage* store;
        Bytes* bytes;
        std::size_t at = 0;
        bool writeable = false;
        uint32_t size() const override { return bytes->size(); }
        std::size_t read_at(uint32_t offset, void* output, std::size_t size) override
        {
            ++store->reads;
            if (offset > bytes->size()) return 0;
            size = std::min(size, bytes->size() - offset);
            store->read_bytes += size;
            std::memcpy(output, bytes->data() + offset, size);
            return size;
        }
        std::size_t write(const void* input, std::size_t size) override
        {
            if (!writeable || store->fail_write) return 0;
            if (at + size > bytes->size()) bytes->resize(at + size);
            std::memcpy(bytes->data() + at, input, size);
            at += size;
            return size;
        }
        bool flush() override { return !store->fail_write; }
    };
    bool ready() const override { return online; }
    uint32_t session() const override { return media_session; }
    music::File* open(const char* path, music::FileMode mode) override
    {
        ++opens;
        if (!online)
        {
            result = music::StorageResult::Unavailable;
            return nullptr;
        }
        if (mode == music::FileMode::Read && !files.count(path))
        {
            result = music::StorageResult::Missing;
            return nullptr;
        }
        auto& data = files[path];
        if (mode == music::FileMode::Write) data.clear();
        auto* file = new Handle;
        file->store = this;
        file->bytes = &data;
        file->at = mode == music::FileMode::Append ? data.size() : 0;
        file->writeable = mode != music::FileMode::Read;
        result = music::StorageResult::Ok;
        return file;
    }
    void close(music::File*& file) override
    {
        delete file;
        file = nullptr;
    }
    music::StorageResult last_result() const override { return result; }
    music::StorageResult next(const char* dir, uint64_t& cursor, music::Entry& out) override
    {
        if (!online) return music::StorageResult::Unavailable;
        if (!directories.count(dir)) return music::StorageResult::Missing;
        std::map<std::string, bool> entries;
        const std::string prefix = std::string(dir) + "/";
        for (const auto& file : files)
            if (file.first.rfind(prefix, 0) == 0)
            {
                const auto name = file.first.substr(prefix.size());
                const auto slash = name.find('/');
                entries[name.substr(0, slash)] = slash != std::string::npos;
            }
        auto index = cursor;
        for (const auto& item : entries)
            if (index-- == 0)
            {
                out = {};
                music::copy_text(out.name, sizeof(out.name), item.first.c_str());
                out.directory = item.second;
                if (!item.second) out.size = files[prefix + item.first].size();
                ++cursor;
                return music::StorageResult::Ok;
            }
        return music::StorageResult::End;
    }
    bool exists(const char* path) override { return files.count(path) || directories.count(path); }
    bool mkdir(const char* path) override
    {
        directories.insert(path);
        return true;
    }
    bool remove(const char* path) override { return files.erase(path) != 0; }
    bool rename(const char* from, const char* to) override
    {
        auto found = files.find(from);
        if (found == files.end() || files.count(to)) return false;
        files[to] = std::move(found->second);
        files.erase(found);
        return true;
    }
};
