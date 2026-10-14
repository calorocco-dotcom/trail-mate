#include "geocaching/protocol/cmp_reader.h"
#include "track_sharing/signed_manifest_store.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
struct Io
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
    std::string path;
    size_t offset = 0, writes = 0;
    bool active = false, publish_fail = false, corrupt = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        offset = 0;
        if (mode == Mode::Read && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
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
    size_t read(void*, uint8_t* out, size_t size)
    {
        assert(size <= 128);
        size = std::min(size, files[path].size() - offset);
        std::memcpy(out, files[path].data() + offset, size);
        offset += size;
        return size;
    }
    size_t write(void*, const uint8_t* in, size_t size)
    {
        assert(size <= 320);
        ++writes;
        files[path].resize(offset + size);
        std::memcpy(files[path].data() + offset, in, size);
        offset += size;
        if (corrupt) files[path][0] = 0xff;
        return size;
    }
    bool sync(void*) { return true; }
    bool publish(const char* from, const char* to)
    {
        assert(!active);
        if (publish_fail || files.count(to) || !files.count(from)) return false;
        files[to] = std::move(files[from]);
        files.erase(from);
        return true;
    }
};
int main()
{
    using namespace track_sharing;
    uint8_t scratch[128], signature[64];
    std::memset(signature, 7, 64);
    for (size_t count : {size_t(1), size_t(255), size_t(256), size_t(4096)})
    {
        Io io;
        SignedManifestStore<Io> store(io, "tmp", "final");
        static_assert(sizeof(store) <= 24, "Signed store cannot own payloads");
        std::vector<uint8_t> raw(count);
        for (size_t i = 0; i < count; ++i) raw[i] = uint8_t(i);
        assert(store.persist({raw.data(), raw.size()}, {signature, 64}, scratch, sizeof(scratch)) == SignedStoreResult::Ready && !io.active && !io.files.count("tmp"));
        geocaching::protocol::CmpReader reader({io.files["final"].data(), io.files["final"].size()});
        size_t fields = 0;
        geocaching::ByteView manifest, sig;
        assert(reader.array(fields, 2) && reader.binary(manifest, 4096) && reader.binary(sig, 64) && reader.finished() && manifest.size == count && !std::memcmp(manifest.data, raw.data(), count) && !std::memcmp(sig.data, signature, 64));
        const auto writes = io.writes;
        assert(store.persist({raw.data(), raw.size()}, {signature, 64}, scratch, sizeof(scratch)) == SignedStoreResult::Ready && io.writes == writes);
        io.files["final"][0] ^= 1;
        const auto saved = io.files;
        assert(store.persist({raw.data(), raw.size()}, {signature, 64}, scratch, sizeof(scratch)) == SignedStoreResult::Conflict && io.files == saved);
    }
    Io io;
    SignedManifestStore<Io> store(io, "tmp", "final");
    uint8_t raw[256]{};
    io.publish_fail = true;
    assert(store.persist({raw, sizeof(raw)}, {signature, 64}, scratch, sizeof(scratch)) == SignedStoreResult::StorageFailure && io.files.count("tmp") && !io.active);
    const auto writes = io.writes;
    io.publish_fail = false;
    assert(store.persist({raw, sizeof(raw)}, {signature, 64}, scratch, sizeof(scratch)) == SignedStoreResult::Ready && io.writes == writes);
    Io broken;
    broken.corrupt = true;
    SignedManifestStore<Io> failed(broken, "tmp", "final");
    assert(failed.persist({raw, sizeof(raw)}, {signature, 64}, scratch, sizeof(scratch)) == SignedStoreResult::StorageFailure && !broken.active && !broken.files.count("final"));
    broken.corrupt = false;
    const auto saved = broken.files;
    const auto failed_writes = broken.writes;
    assert(failed.persist({raw, sizeof(raw)}, {signature, 64}, scratch, sizeof(scratch)) == SignedStoreResult::Conflict && broken.files == saved && broken.writes == failed_writes);
}
