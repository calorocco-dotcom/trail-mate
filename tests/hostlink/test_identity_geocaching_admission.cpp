#include <atomic>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <memory>
namespace ui::geocaching
{
enum class DraftReadStatus
{
    Pending,
    Ready
};
}
namespace gc
{
enum class QueryClientPhase
{
    Idle,
    PageReady,
    Failed,
    Querying
};
}
namespace
{
enum class Phase
{
    Ready,
    Failed,
    Recover
};
bool can_lock = true, locked = false, download_busy = false, publication_busy = false, save_busy = false;
struct Guard
{
    bool locked = can_lock;
    Guard() { ::locked = locked; }
    ~Guard() { ::locked = false; }
};
bool ensureMutex() { return true; }
bool downloadActive() { return download_busy; }
bool publicationActive() { return publication_busy; }
bool draftSaveActive() { return save_busy; }
struct Store
{
    bool work = false;
    bool commitPending() const { return work; }
    bool busy() const { return work; }
    bool pending() const { return work; }
};
struct Workspace
{
    bool work = false;
    const void* holder() const { return work ? this : nullptr; }
};
struct Catalog
{
    bool reading = false;
};
struct Read
{
    ui::geocaching::DraftReadStatus status = ui::geocaching::DraftReadStatus::Ready;
};
struct Client
{
    gc::QueryClientPhase value = gc::QueryClientPhase::Idle;
    gc::QueryClientPhase phase() const { return value; }
};
struct Session
{
    Phase phase = Phase::Ready;
    bool recovery = false, checkpoint = false, response = false, publication_restore_pending = false, download_restore_pending = false, recover = false;
    Workspace workspace_owner;
    Store *store = nullptr, *dispatch_store = nullptr, *receipts = nullptr, *saved = nullptr;
    Catalog* draft_catalog = nullptr;
    Read* draft_read = nullptr;
    Client* client = nullptr;
    bool needsRecovery() const { return recover; }
};
std::unique_ptr<Session> session;
std::atomic<void*> pending_reply{nullptr}, pending_announcement{nullptr};
std::atomic<uint8_t> wanted{3};
std::atomic<bool> network_requested{true}, local_storage_requested{true}, active{true};
uint64_t epoch = 0;
// Production lock/check/callback/release policy with controlled owner states.
#include "identity_geocaching_admission_actual.inc"
unsigned calls = 0;
bool action(void* context)
{
    assert(locked);
    ++calls;
    return *static_cast<bool*>(context);
}
} // namespace
int main()
{
    bool committed = false;
    const auto blocked = [&]()
    {const auto before=calls;assert(!withIdentityMaintenance(action,&committed)&&calls==before&&!locked); };
    can_lock = false;
    blocked();
    can_lock = true;
    pending_reply.store(&committed);
    blocked();
    pending_reply.store(nullptr);
    pending_announcement.store(&committed);
    blocked();
    pending_announcement.store(nullptr);
    session = std::make_unique<Session>();
    for (bool Session::*field : {&Session::recovery, &Session::checkpoint, &Session::response, &Session::publication_restore_pending, &Session::download_restore_pending, &Session::recover})
    {
        session.get()->*field = true;
        blocked();
        session.get()->*field = false;
    }
    session->phase = Phase::Recover;
    blocked();
    session->phase = Phase::Ready;
    session->workspace_owner.work = true;
    blocked();
    session->workspace_owner.work = false;
    for (bool* flag : {&download_busy, &publication_busy, &save_busy})
    {
        *flag = true;
        blocked();
        *flag = false;
    }
    Store store;
    store.work = true;
    for (Store* Session::*field : {&Session::store, &Session::dispatch_store, &Session::receipts, &Session::saved})
    {
        session.get()->*field = &store;
        blocked();
        session.get()->*field = nullptr;
    }
    Catalog catalog;
    catalog.reading = true;
    session->draft_catalog = &catalog;
    blocked();
    session->draft_catalog = nullptr;
    Read read;
    read.status = ui::geocaching::DraftReadStatus::Pending;
    session->draft_read = &read;
    blocked();
    session->draft_read = nullptr;
    Client client;
    client.value = gc::QueryClientPhase::Querying;
    session->client = &client;
    blocked();
    client.value = gc::QueryClientPhase::PageReady;
    auto* original = session.get();
    assert(withIdentityMaintenance(action, &committed) && session.get() == original && !locked && wanted == 3);
    committed = true;
    assert(withIdentityMaintenance(action, &committed) && !session && wanted == 0 && !active && !network_requested && !local_storage_requested && epoch == 1 && !locked);
    assert(withIdentityMaintenance(action, &committed) && epoch == 2);
}
