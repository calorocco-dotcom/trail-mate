#include "hostlink/object_transfer.h"
#include <cassert>
#include <vector>

using namespace hostlink::management;
struct Storage : TransferStorage
{
    std::vector<uint8_t> file;
    bool fail_write = false, hash_ok = true;
    unsigned begins = 0, seals = 0;
    bool begin(const TransferDescriptor&) override
    {
        ++begins;
        file.clear();
        return true;
    }
    bool append(uint32_t offset, const uint8_t* data, size_t length) override
    {
        if (fail_write || offset != file.size()) return false;
        file.insert(file.end(), data, data + length);
        return true;
    }
    bool matches(uint32_t offset, const uint8_t* data, size_t length) override
    {
        return offset <= file.size() && length <= file.size() - offset && !std::memcmp(file.data() + offset, data, length);
    }
    bool verify(const uint8_t*, uint32_t length) override { return hash_ok && length == file.size(); }
    bool seal() override
    {
        ++seals;
        return true;
    }
    bool discard() override
    {
        file.clear();
        return true;
    }
};
int main()
{
    Storage storage;
    ObjectTransfer transfer(storage);
    TransferDescriptor descriptor;
    descriptor.owner[0] = 1;
    descriptor.id[0] = 2;
    descriptor.bytes = 5;
    assert(transfer.begin(descriptor, 4) == TransferResult::Invalid);
    assert(transfer.begin(descriptor, 5) == TransferResult::Ok);
    assert(transfer.begin(descriptor, 5) == TransferResult::Ok && storage.begins == 1);
    const uint8_t bytes[] = {1, 2, 3, 4, 5};
    uint8_t wrong[16]{};
    assert(transfer.append(wrong, descriptor.id, 0, bytes, 3) == TransferResult::WrongOwner);
    assert(transfer.append(descriptor.owner, descriptor.id, 1, bytes, 3) == TransferResult::Conflict);
    assert(transfer.finish(descriptor.owner, descriptor.id) == TransferResult::Conflict);
    storage.fail_write = true;
    assert(transfer.append(descriptor.owner, descriptor.id, 0, bytes, 3) == TransferResult::StorageFailure && transfer.offset() == 0);
    storage.fail_write = false;
    assert(transfer.append(descriptor.owner, descriptor.id, 0, bytes, 3) == TransferResult::Ok);
    assert(transfer.append(descriptor.owner, descriptor.id, 0, bytes, 3) == TransferResult::Ok && storage.file.size() == 3);
    assert(transfer.append(descriptor.owner, descriptor.id, 0, bytes + 1, 3) == TransferResult::Conflict);
    assert(transfer.append(descriptor.owner, descriptor.id, 3, bytes + 3, 2) == TransferResult::Ok);
    storage.hash_ok = false;
    assert(transfer.finish(descriptor.owner, descriptor.id) == TransferResult::HashMismatch && storage.seals == 0);
    storage.hash_ok = true;
    assert(transfer.finish(descriptor.owner, descriptor.id) == TransferResult::Ok);
    assert(transfer.finish(descriptor.owner, descriptor.id) == TransferResult::Ok && storage.seals == 1);
    assert(transfer.abort(descriptor.owner, descriptor.id) == TransferResult::Ok && !transfer.active());
    assert(!transfer.restore(descriptor, 6, false));
    assert(!transfer.restore(descriptor, 3, true));
    assert(transfer.restore(descriptor, 3, false) && transfer.offset() == 3);
}
