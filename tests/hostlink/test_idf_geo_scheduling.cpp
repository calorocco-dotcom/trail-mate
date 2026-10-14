#include "platform/esp/common/storage/storage_contracts.h"
#include <cassert>
namespace geo
{
static bool pending = true, finish_next = false;
static unsigned steps = 0;
bool workPending() { return pending; }
void step()
{
    ++steps;
    if (finish_next) pending = false;
}
} // namespace geo
using namespace platform::esp::common::storage;
using Operation = StorageOperation;
using OperationGeneration = StorageOperationGeneration;
using Result = StorageOperationResult;
using ResultKind = StorageOperationResultKind;
struct Peer
{
    bool pending = true;
    bool persistencePending() const { return pending; }
};
struct Store
{
    Result stepMaintenance(Operation operation, OperationGeneration generation, const StorageOperationBudget&)
    {
        return Result::completedResult(operation, generation);
    }
};
struct Adapter final : ISemanticStorageAdapter
{
    Peer peer;
    Store store;
    Peer* peer_directory_ = &peer;
    Store* store_ = &store;
    uint8_t geocaching_steps_ = 0;
    bool geocaching_turn_ = false;
    OperationGeneration peer_payload_generation_ = 0;
    unsigned peer_begins = 0, peer_steps = 0;
    Result beginHydration(OperationGeneration generation) { return Result::completedResult(Operation::Hydrate, generation); }
    Result stepHydration(OperationGeneration generation, const StorageOperationBudget&) { return beginHydration(generation); }
    Result beginPeerPersistence(OperationGeneration generation)
    {
        ++peer_begins;
        peer_payload_generation_ = generation;
        return Result::inProgressResult(Operation::Persist, generation);
    }
    Result stepPeerPersistence(OperationGeneration generation)
    {
        if (generation != peer_payload_generation_) return Result::failure(ResultKind::StaleGeneration, Operation::Persist, generation);
        ++peer_steps;
        return Result::completedResult(Operation::Persist, generation);
    }
    Result execute(Operation operation, OperationGeneration generation) { return Result::completedResult(operation, generation); }
    void cancelAtStepBoundary(Operation, OperationGeneration) override {}
    // Production begin/step methods, with storage actions replaced by traces.
#include "idf_storage_scheduling_actual.inc"
};
int main()
{
    Adapter adapter;
    StorageOperationBudget budget{};
    assert(adapter.begin(Operation::Persist, 1).inProgress());
    assert(adapter.peer_begins == 1 && geo::steps == 0);
    assert(adapter.step(Operation::Persist, 1, budget).completed());
    assert(adapter.begin(Operation::Persist, 2).inProgress());
    assert(adapter.geocaching_steps_ == 8);
    assert(adapter.step(Operation::Persist, 2, budget).inProgress());
    assert(adapter.begin(Operation::Persist, 2).inProgress() && adapter.geocaching_steps_ == 7);
    for (unsigned index = 0; index < 6; ++index) assert(adapter.step(Operation::Persist, 2, budget).inProgress());
    assert(adapter.step(Operation::Persist, 2, budget).completed());
    assert(geo::steps == 8 && adapter.peer_steps == 1 && adapter.peer_payload_generation_ == 0);
    assert(adapter.begin(Operation::Persist, 3).inProgress() && adapter.peer_begins == 2);
    assert(adapter.step(Operation::Persist, 3, budget).completed());
    adapter.peer.pending = false;
    geo::finish_next = true;
    assert(adapter.begin(Operation::Persist, 4).inProgress());
    assert(adapter.step(Operation::Persist, 4, budget).completed());
    assert(adapter.geocaching_steps_ == 0 && geo::steps == 9);
}
