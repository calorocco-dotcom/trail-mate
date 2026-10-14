#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace track_sharing
{
enum class FileIntegrityResult : uint8_t
{
    Idle,
    Pending,
    Ready,
    Busy,
    Invalid,
    HashMismatch,
    StorageFailure,
    Cancelled
};
// Io lends operation-scoped file handles; Hash lends an exclusive stream.
// Caller retains an immutable source lease and stable device-resolved path.
// This verifies exact bytes only, not XML geometry or signature authenticity.
template <class Io, class Hash>
class FileIntegrityCheck
{
  public:
    FileIntegrityCheck(Io& io, Hash& hash) : io_(io), hash_(hash) {}
    ~FileIntegrityCheck()
    {
        if (active()) hash_.reset();
    }
    FileIntegrityCheck(const FileIntegrityCheck&) = delete;
    FileIntegrityCheck& operator=(const FileIntegrityCheck&) = delete;
    FileIntegrityResult begin(const char* path, uint32_t bytes, const uint8_t expected[32])
    {
        if (active()) return FileIntegrityResult::Busy;
        if (!path || !*path || !expected || !bytes || bytes > 8388608) return FileIntegrityResult::Invalid;
        path_ = path;
        bytes_ = bytes;
        offset_ = 0;
        std::memcpy(expected_, expected, 32);
        if (!hash_.start()) return fail(FileIntegrityResult::StorageFailure);
        result_ = FileIntegrityResult::Pending;
        return result_;
    }
    FileIntegrityResult step(uint8_t* scratch, size_t capacity)
    {
        if (!active()) return result_;
        if (!scratch || capacity < 32) return fail(FileIntegrityResult::Invalid);
        if (offset_ == bytes_)
        {
            if (!hash_.finish(scratch)) return fail(FileIntegrityResult::StorageFailure);
            const bool matches = !std::memcmp(scratch, expected_, 32);
            return fail(matches ? FileIntegrityResult::Ready : FileIntegrityResult::HashMismatch);
        }
        const auto length = std::min<size_t>(std::min<size_t>(capacity, 320), bytes_ - offset_);
        const auto opened = io_.open(path_, Io::Mode::Read);
        if (!opened.handle) return fail(FileIntegrityResult::StorageFailure);
        uint64_t actual = 0;
        const bool read = io_.size(opened.handle, actual) && actual == bytes_ && io_.seek(opened.handle, offset_) && io_.read(opened.handle, scratch, length) == length;
        const bool closed = io_.close(opened.handle);
        if (!read || !closed || !hash_.update(scratch, length)) return fail(FileIntegrityResult::StorageFailure);
        offset_ += static_cast<uint32_t>(length);
        return result_;
    }
    FileIntegrityResult cancel() { return active() ? fail(FileIntegrityResult::Cancelled) : result_; }
    bool active() const { return result_ == FileIntegrityResult::Pending; }
    uint32_t checkedBytes() const { return offset_; }
    bool matchesExpected(uint32_t bytes, const uint8_t* hash) const
    {
        return result_ == FileIntegrityResult::Ready && hash && bytes == bytes_ && !std::memcmp(hash, expected_, 32);
    }
    bool matchesSource(const char* path, uint32_t bytes, const uint8_t* hash) const
    {
        return path && path_ && !std::strcmp(path, path_) && matchesExpected(bytes, hash);
    }

  private:
    FileIntegrityResult fail(FileIntegrityResult result)
    {
        hash_.reset();
        result_ = result;
        return result_;
    }
    Io& io_;
    Hash& hash_;
    const char* path_ = nullptr;
    uint8_t expected_[32]{};
    uint32_t bytes_ = 0, offset_ = 0;
    FileIntegrityResult result_ = FileIntegrityResult::Idle;
};
} // namespace track_sharing
