#include "hostlink/route_install_job.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
using namespace hostlink::management;
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
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<uint8_t>* opened = nullptr;
    size_t offset = 0, max_read = 0;
    unsigned publishes = 0;
    bool publish_error = false, corrupt_final = false, synced = false;
    OpenResult open(const char* path, Mode)
    {
        assert(!opened);
        auto found = files.find(path);
        if (found == files.end()) return {nullptr, OpenStatus::Missing};
        opened = &found->second;
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        assert(opened);
        opened = nullptr;
        return true;
    }
    bool size(void*, uint64_t& size)
    {
        size = opened->size();
        return true;
    }
    bool seek(void*, uint64_t value)
    {
        offset = size_t(value);
        return offset <= opened->size();
    }
    size_t read(void*, uint8_t* output, size_t count)
    {
        assert(count <= 320 && offset + count <= opened->size());
        max_read = std::max(max_read, count);
        std::memcpy(output, opened->data() + offset, count);
        return count;
    }
    bool sync(void*)
    {
        synced = true;
        return true;
    }
    bool publish(const char* source, const char* target)
    {
        assert(!opened && !files.count(target));
        publishes++;
        files[target] = std::move(files[source]);
        files.erase(source);
        if (corrupt_final) files[target][10] ^= 1;
        return !publish_error;
    }
};
static char names[2048];
static uint8_t scratch[320];
int main()
{
    const std::string xml = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><name>" + std::string(1400, 'x') + "</name><LineString><coordinates>120,30 120.01,30.01</coordinates></LineString></Placemark></kml>";
    for (unsigned mode = 0; mode < 7; ++mode)
    {
        Io io;
        uint8_t id[16]{1}, digest[32];
        char source[64], target[77];
        assert(track_sharing::resolveUploadPath(id, 16, track_sharing::UploadFile::Candidate, source, sizeof(source)));
        mbedtls_sha256_context context;
        platform::esp::storage::SnapshotStreamSha256 hash(context);
        assert(hash.start() && hash.update(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()) && hash.finish(digest));
        assert(routeInstallTarget({digest, 32}, target, sizeof(target)));
        io.files[source] = std::vector<uint8_t>(xml.begin(), xml.end());
        if (mode == 2 || mode == 3)
        {
            io.files[target] = io.files[source];
            io.files.erase(source);
            if (mode == 3) io.files[target][xml.find(std::string(1400, 'x')) + 10] ^= 1;
        }
        io.publish_error = mode == 4;
        io.corrupt_final = mode == 5;
        RouteInstallView request{{id, 16}, {digest, 32}, uint32_t(xml.size()), 7};
        RouteInstallJob<Io, decltype(hash)> job(io, hash, names, sizeof(names), request, source, target);
        const auto guard = [&]
        { return mode == 6 && io.synced ? Status::StaleIdentity : Status::Ok; };
        auto status = job.begin(mode != 1, guard);
        unsigned steps = 0;
        while (status == Status::Busy && steps++ < 100) status = job.step(scratch, sizeof(scratch), guard);
        assert(!io.opened && io.max_read <= 320 && steps < 100);
        if (mode == 1)
        {
            assert(status == Status::Ok && job.wireState() == 2 && io.publishes == 0 && io.files.count(source));
        }
        else if (mode == 3)
        {
            assert(status == Status::HashMismatch && io.publishes == 0);
        }
        else if (mode == 5)
        {
            assert(status == Status::Internal && job.phase() == RouteInstallPhase::Failed && io.publishes == 1);
        }
        else if (mode == 6)
        {
            assert(status == Status::StaleIdentity && io.publishes == 0 && io.files.count(source));
        }
        else
        {
            assert(status == Status::Ok && job.wireState() == 1 && io.files.count(target));
            assert(io.publishes == (mode == 2 ? 0U : 1U));
        }
    }
}
