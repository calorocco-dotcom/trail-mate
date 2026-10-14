#pragma once
#include "chat/ports/i_mesh_adapter.h"
#include "hostlink/management_envelope.h"
#include "track_sharing/directory_list.h"
#include "track_sharing/publication_directory.h"
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <new>

namespace hostlink::management::directory_runtime
{
class Owner
{
  public:
    Owner(chat::IMeshAdapter& mesh, const uint8_t* id, uint64_t now)
        : adapter(mesh), pool(entries, track_sharing::TrackDirectoryPool::kCapacity), last_used(now), binding{nullptr, this, announce, control}
    {
        std::memcpy(session, id, 16);
    }
    chat::IMeshAdapter& adapter;
    track_sharing::TrackDirectoryEntry entries[track_sharing::TrackDirectoryPool::kCapacity]{};
    track_sharing::TrackDirectoryPool pool;
    uint8_t session[16]{};
    uint64_t last_used;
    chat::lxmf::TrackDeliveryBinding binding;
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
    uint8_t local_delivery[16]{}, network_request[16]{}, target_delivery[16]{}, target_epoch[16]{};
    uint64_t retry_after[track_sharing::TrackDirectoryPool::kCapacity]{}, deadline = 0;
    size_t selected_index = 0, next_index = 0;
    bool pending = false;
    static bool announce(const chat::lxmf::TrackDirectoryAnnouncementView& view, void* context)
    {
        auto& owner = *static_cast<Owner*>(context);
        if (owner.locked.test_and_set(std::memory_order_acquire)) return false;
        const bool accepted = owner.pool.observe(view, uint64_t(esp_timer_get_time() / 1000));
        if (accepted && owner.pending && !owner.pool.find({owner.target_delivery, 16}, {owner.target_epoch, 16}))
        {
            owner.pending = false;
            owner.retry_after[owner.selected_index] = 0;
        }
        owner.locked.clear(std::memory_order_release);
        return accepted;
    }
    static bool control(const chat::lxmf::CustomDeliveryView& view, void* context)
    {
        auto& owner = *static_cast<Owner*>(context);
        if (owner.locked.test_and_set(std::memory_order_acquire)) return false;
        const bool accepted = owner.pending && uint64_t(esp_timer_get_time() / 1000) <= owner.deadline && view.source.data && view.source.size == 16 && !std::memcmp(view.source.data, owner.target_delivery, 16) &&
                              view.destination.data && view.destination.size == 16 && !std::memcmp(view.destination.data, owner.local_delivery, 16) &&
                              owner.pool.confirmCapabilities({owner.target_delivery, 16}, {owner.target_epoch, 16}, {view.data.data, view.data.size}, {owner.network_request, 16});
        if (accepted) owner.pending = false;
        owner.locked.clear(std::memory_order_release);
        return accepted;
    }
    void negotiate(uint64_t now, uint8_t* scratch, size_t capacity)
    {
        if (!scratch || capacity < 128 || locked.test_and_set(std::memory_order_acquire)) return;
        if (pending && now >= deadline)
        {
            pending = false;
            retry_after[selected_index] = now + 30000;
        }
        if (pending)
        {
            locked.clear(std::memory_order_release);
            return;
        }
        bool found = false;
        for (size_t visited = 0; visited < pool.size(); ++visited)
        {
            const auto index = next_index++ % pool.size();
            const auto* entry = pool.at(index);
            if (entry->capabilities_verified || now < retry_after[index]) continue;
            selected_index = index;
            std::memcpy(target_delivery, entry->delivery, 16);
            std::memcpy(target_epoch, entry->epoch, 16);
            found = true;
            break;
        }
        locked.clear(std::memory_order_release);
        if (!found) return;
        auto* local_author = scratch + capacity - 64;
        uint32_t generation = 0;
        if (adapter.readDeviceAuthorKey(local_author, generation) != chat::DeviceAuthorResult::Ok || !adapter.trackLocalDestination(local_delivery)) return;
        esp_fill_random(network_request, 16);
        geocaching::protocol::CmpWriter writer(scratch, capacity - 64);
        if (!track_sharing::networkRequestHeader(writer, 0, {network_request, 16}, 8192, 0)) return;
        if (locked.test_and_set(std::memory_order_acquire)) return;
        if (!pool.find({target_delivery, 16}, {target_epoch, 16}))
        {
            locked.clear(std::memory_order_release);
            return;
        }
        pending = true;
        deadline = now + 15000;
        locked.clear(std::memory_order_release);
        // No owner flag is held while entering the router; callbacks only
        // borrow their LXMF frame and synchronously finish capability validation.
        const auto result = adapter.sendTrackData(target_delivery, {scratch, writer.size()}, local_author, generation, false, nullptr);
        if (!result.ok && !locked.test_and_set(std::memory_order_acquire))
        {
            pending = false;
            retry_after[selected_index] = now + 3000;
            locked.clear(std::memory_order_release);
        }
    }
};
static_assert(sizeof(Owner) <= 1792, "Directory session is bounded transient PSRAM metadata");
inline Owner* owner = nullptr;
using PinnedDirectory = track_sharing::PublicationDirectory;
static_assert(sizeof(PinnedDirectory) <= 104, "Pinned directory contains only publication metadata");
inline Status pin(chat::IMeshAdapter& adapter, const uint8_t* session, geocaching::ByteView delivery,
                  geocaching::ByteView epoch, uint64_t now, PinnedDirectory& output)
{
    if (!session || !delivery.data || delivery.size != 16 || !epoch.data || epoch.size != 16) return Status::InvalidField;
    const auto target = reinterpret_cast<uintptr_t>(&output);
    const auto overlaps = [target](const void* pointer, size_t size)
    {
        const auto source = reinterpret_cast<uintptr_t>(pointer);
        return source <= target ? target - source < size : source - target < sizeof(PinnedDirectory);
    };
    if (overlaps(session, 16) || overlaps(delivery.data, 16) || overlaps(epoch.data, 16) ||
        (owner && overlaps(owner, sizeof(Owner)))) return Status::InvalidField;
    if (!owner || now < owner->last_used || now - owner->last_used >= 30000) return Status::Expired;
    if (&owner->adapter != &adapter || std::memcmp(owner->session, session, 16)) return Status::Unauthorized;
    if (owner->locked.test_and_set(std::memory_order_acquire)) return Status::Busy;
    const auto* entry = owner->pool.find(delivery, epoch);
    if (!entry || !entry->capabilities_verified)
    {
        owner->locked.clear(std::memory_order_release);
        return entry ? Status::Unauthorized : Status::Expired;
    }
    std::memcpy(output.delivery, entry->delivery, 16);
    std::memcpy(output.public_key, entry->public_key, 64);
    std::memcpy(output.epoch, entry->epoch, 16);
    output.file_limit = entry->file_limit;
    output.chunk_limit = entry->chunk_limit;
    output.query_limit = entry->query_limit;
    owner->last_used = now;
    owner->locked.clear(std::memory_order_release);
    return Status::Ok;
}
inline bool release()
{
    if (!owner) return true;
    // Do not hold the pool flag while taking the router lock. Successful unbind
    // waits for the current callback before its PSRAM context can be destroyed.
    const auto removed = owner->adapter.replaceTrackDelivery(&owner->binding,nullptr);
    if (removed != chat::TrackBindingChange::Applied && removed != chat::TrackBindingChange::Conflict) return false;
    owner->~Owner();
    heap_caps_free(owner);
    owner = nullptr;
    return true;
}
// Job context is fully constructed and its identity guard fixed before this
// scheduler-only call. Atomic replacement retires directory callbacks before
// releasing PSRAM; later directory cleanup cannot clear the job's binding.
template <class ReadWork>
chat::TrackBindingChange handoverRead(chat::IMeshAdapter& adapter, ReadWork& work, uint64_t now)
{
    if (owner && &owner->adapter != &adapter) return chat::TrackBindingChange::Conflict;
    const auto changed = work.attach(now, owner ? &owner->binding : nullptr);
    if (changed == chat::TrackBindingChange::Applied && owner)
    {
        owner->~Owner();
        heap_caps_free(owner);
        owner = nullptr;
    }
    return changed;
}
inline chat::TrackBindingChange handover(chat::IMeshAdapter& adapter, const chat::lxmf::TrackDeliveryBinding* replacement)
{
    if (!replacement || !replacement->handler || !replacement->expected_local_author || !replacement->expected_author_generation ||
        (owner && &owner->adapter != &adapter)) return chat::TrackBindingChange::Conflict;
    const auto changed = adapter.replaceTrackDelivery(owner ? &owner->binding : nullptr,replacement);
    if (changed != chat::TrackBindingChange::Applied) return changed;
    if (owner)
    {
        owner->~Owner();heap_caps_free(owner);owner=nullptr;
    }
    return changed;
}
inline void advance(uint64_t now, bool connected, uint8_t* scratch = nullptr, size_t capacity = 0)
{
    if (owner && (!connected || now < owner->last_used || now - owner->last_used >= 30000))
    {
        release();
        return;
    }
    if (owner && connected) owner->negotiate(now, scratch, capacity);
}
inline void closeSession(const uint8_t* id)
{
    if (owner && id && !std::memcmp(owner->session, id, 16)) release();
}
inline Status list(chat::IMeshAdapter& adapter, const EnvelopeView& request, uint64_t now, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t cursor = 0;
    if (!request.session || !reader.array(fields, 1) || fields != 1 || !reader.unsignedInteger(cursor) || cursor > 8 || !reader.finished()) return Status::InvalidField;
    advance(now, true);
    if (owner && (&owner->adapter != &adapter || std::memcmp(owner->session, request.session, 16))) return Status::Conflict;
    if (!owner)
    {
        if (cursor) return Status::Expired;
        constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        if (heap_caps_get_free_size(caps) < sizeof(Owner) + 8192 || heap_caps_get_largest_free_block(caps) < sizeof(Owner)) return Status::Busy;
        void* memory = heap_caps_malloc(sizeof(Owner), caps);
        if (!memory) return Status::Busy;
        auto* created = new (memory) Owner(adapter, request.session, now);
        const auto bound = adapter.replaceTrackDelivery(nullptr,&created->binding);
        if (bound != chat::TrackBindingChange::Applied)
        {
            created->~Owner();
            heap_caps_free(created);
            return bound == chat::TrackBindingChange::Unsupported ? Status::Unsupported : Status::Busy;
        }
        owner = created;
    }
    if (owner->locked.test_and_set(std::memory_order_acquire)) return Status::Busy;
    const bool encoded = track_sharing::encodeTrackDirectoryPage(owner->pool, size_t(cursor), output, capacity, written);
    owner->locked.clear(std::memory_order_release);
    if (!encoded) return Status::InvalidField;
    owner->last_used = now;
    return Status::Ok;
}
inline Status discover(chat::IMeshAdapter& adapter, const EnvelopeView& request, uint64_t now, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    geocaching::ByteView destination;
    if (!output || !capacity || !reader.array(fields, 1) || fields != 1 || !reader.binary(destination, 16) || !track_sharing::networkRequestId(destination) || !reader.finished()) return Status::InvalidField;
    uint8_t target[16];
    std::memcpy(target, destination.data, 16);
    const uint8_t empty_page[] = {0x91, 0};
    auto opening = request;
    opening.body = empty_page;
    opening.body_size = sizeof(empty_page);
    const auto opened = list(adapter, opening, now, output, capacity, written);
    if (opened != Status::Ok)
    {
        written = 0;
        return opened;
    }
    written = 0;
    if (!adapter.requestTrackDirectoryPath(target)) return Status::Busy;
    output[0] = 0x90;
    written = 1;
    return Status::Ok;
}
} // namespace hostlink::management::directory_runtime
