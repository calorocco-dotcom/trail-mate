#include "hostlink/route_install_commit.h"
#include <cassert>
#include <string>
#include <vector>
using namespace hostlink::management;
struct Check
{
    bool ready = true;
    std::string source;
    bool matchesSource(const char* path, uint32_t bytes, const uint8_t* hash) const
    {
        return ready && source == path && bytes == 1024 && hash && hash[0] == 2;
    }
};
struct Io
{
    enum class Mode
    {
        Read,
        Update
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
    bool existing = false, io_error = false, synced = true, published = true, opened = false;
    unsigned publishes = 0;
    std::vector<std::string> calls;
    OpenResult open(const char*, Mode mode)
    {
        assert(!opened);
        calls.push_back(mode == Mode::Read ? "probe" : "update");
        if (io_error) return {nullptr, OpenStatus::IoError};
        if (mode == Mode::Read && !existing) return {nullptr, OpenStatus::Missing};
        opened = true;
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        assert(opened);
        opened = false;
        calls.push_back("close");
        return true;
    }
    bool size(void*, uint64_t& bytes)
    {
        bytes = 1024;
        return true;
    }
    bool sync(void*)
    {
        calls.push_back("sync");
        return synced;
    }
    bool publish(const char*, const char*)
    {
        assert(!opened);
        calls.push_back("publish");
        publishes++;
        existing = true;
        return published;
    }
};
int main()
{
    uint8_t id[16]{1}, hash[32]{2};
    RouteInstallView request{{id, 16}, {hash, 32}, 1024, 7};
    char source[64], target[77];
    assert(track_sharing::resolveUploadPath(id, 16, track_sharing::UploadFile::Candidate, source, sizeof(source)));
    Check check;
    check.source = source;
    const auto allow = []
    { return Status::Ok; };
    Io io;
    assert(publishValidatedRoute(io, check, request, source, target, allow) == Status::Ok);
    assert((io.calls == std::vector<std::string>{"probe", "update", "sync", "close", "publish"}));
    Io existing;
    existing.existing = true;
    assert(publishValidatedRoute(existing, check, request, source, target, allow) == Status::Conflict && !existing.publishes);
    Io denied;
    assert(publishValidatedRoute(denied, check, request, source, target, []
                                 { return Status::StaleIdentity; }) == Status::StaleIdentity &&
           denied.calls.empty());
    Io changed;
    unsigned guards = 0;
    assert(publishValidatedRoute(changed, check, request, source, target, [&]
                                 { return ++guards == 1 ? Status::Ok : Status::StaleIdentity; }) == Status::StaleIdentity &&
           !changed.publishes);
    Io failure;
    failure.published = false;
    assert(publishValidatedRoute(failure, check, request, source, target, allow) == Status::Internal && failure.existing && failure.publishes == 1);
    Io storage;
    storage.synced = false;
    assert(publishValidatedRoute(storage, check, request, source, target, allow) == Status::StorageUnavailable && !storage.publishes && !storage.opened);
    Io wrong;
    check.source = "different.kml";
    assert(publishValidatedRoute(wrong, check, request, source, target, allow) == Status::InvalidField && wrong.calls.empty());
    check.source = source;
    const std::string original_source = source;
    char alternate[64]{};
    std::strcpy(alternate, "different.kml");
    Io path_error;
    assert(publishValidatedRoute(path_error, check, request, alternate, target, allow) == Status::InvalidField && !std::strcmp(alternate, "different.kml") && path_error.calls.empty());
    uint8_t overlap[128]{};
    std::memcpy(overlap + 32, hash, 32);
    RouteInstallView overlapping{{id, 16}, {overlap + 32, 32}, 1024, 7};
    Io alias;
    assert(publishValidatedRoute(alias, check, overlapping, source, reinterpret_cast<char*>(overlap), allow) == Status::InvalidField && !std::memcmp(overlap + 32, hash, 32) && alias.calls.empty());
    assert(original_source == source);
}
