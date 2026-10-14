#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/idf_common/hostlink_route_install_identity.h"
namespace hostlink::management::route_install_runtime
{
// Slot belongs to the application scheduler. No static owner/queue is created
// here. Policy supplies storage mutual exclusion and active-route restrictions.
inline void releaseRouteInstall(RouteInstallOwner*& slot)
{
    destroyRouteInstallOwner(slot);
    slot = nullptr;
}
inline void closeRouteInstallSession(RouteInstallOwner*& slot, const uint8_t* session)
{
    if (slot && slot->matchesSession(session)) releaseRouteInstall(slot);
}
// A terminal receipt has already been copied into caller-owned response bytes.
// Recovery reads the durable target again; it does not need this PSRAM owner.
inline bool retireDeliveredRouteInstall(RouteInstallOwner*& slot)
{
    if (!slot || !slot->resultDelivered() || (slot->wireState() != 1 && slot->wireState() != 2)) return false;
    releaseRouteInstall(slot);
    return true;
}
template <class Adapter, class Policy>
Status advanceRouteInstallSlot(RouteInstallOwner*& slot, Adapter* adapter, uint8_t* scratch, size_t capacity, uint64_t now, bool connected, Policy&& policy)
{
    if (!slot) return Status::Expired;
    if (!slot->alive(now, connected))
    {
        releaseRouteInstall(slot);
        return Status::Expired;
    }
    if (!adapter) return Status::Busy;
    return advanceAuthenticatedRouteInstall(*slot, *adapter, scratch, capacity, [&]
                                            { return policy(slot->binding(), !slot->queryOnly()); });
}
template <class Adapter, class Policy>
Status processRouteInstallRequest(RouteInstallOwner*& slot, Adapter& adapter, const EnvelopeView& request, uint64_t now,
                                  uint8_t* scratch, size_t capacity, size_t& written, Policy&& policy)
{
    written = 0;
    const bool install = request.code == uint16_t(Operation::RouteInstall);
    if (!install && request.code != uint16_t(Operation::RouteInstallStatus)) return Status::Unsupported;
    if (!request.session || !scratch || capacity < 448 || capacity > 512) return Status::InvalidField;
    RouteInstallView decoded;
    const auto parsed = parseRouteInstallRequest({request.body, request.body_size}, decoded);
    if (parsed != Status::Ok) return parsed;
    auto* public_key = scratch + capacity - 64;
    const auto overlap = [&](const void* input, size_t size)
    {const auto a=reinterpret_cast<uintptr_t>(input),b=reinterpret_cast<uintptr_t>(public_key);return a<=b?b-a<size:a-b<64; };
    if (overlap(request.session, 16) || overlap(decoded.transfer.data, 16) || overlap(decoded.hash.data, 32)) return Status::InvalidField;
    uint8_t author[32];
    uint32_t generation = 0;
    const auto current = upload_runtime::readUploadAuthor(adapter, public_key, 64, author, generation);
    if (current != Status::Ok) return current;
    if (generation != decoded.generation) return Status::StaleIdentity;
    if (slot && !slot->alive(now, true)) releaseRouteInstall(slot);
    if (slot && !slot->matches(request.session, decoded)) return slot->matchesSession(request.session) ? Status::Conflict : Status::Unauthorized;
    // A finished query is always revalidated from disk on the next query.
    // Retire only transient state; journals/files remain for recovery.
    if (slot && slot->status() != Status::Busy && (slot->status() != Status::Ok || slot->resultDelivered())) releaseRouteInstall(slot);
    if (!slot)
    {
        const auto admitted = policy(decoded, install);
        if (admitted != Status::Ok) return admitted;
        if (upload_runtime::owner)
        {
            auto& upload = *upload_runtime::owner;
            const auto& journal = upload.resources.metadata();
            if (upload.kind != track_sharing::UploadKind::Kml || upload.discarding || journal.phase != track_sharing::UploadPhase::Sealed ||
                std::memcmp(journal.id, decoded.transfer.data, 16) || !upload.session.matches(request.session, sd::sd_media_session(), now)) return Status::Busy;
            const auto authorized = authorizeRouteInstallUpload(decoded, journal, upload.kind, generation, {author, 32}, true, true);
            if (authorized != Status::Ok) return authorized;
        }
        auto* created = createRouteInstallOwner(decoded, request.session, author, sd::sd_media_session(), now);
        if (!created) return Status::Busy;
        const auto started = startAuthenticatedRouteInstall(*created, adapter, install, scratch, capacity, [&]
                                                            { return policy(created->binding(), install); });
        if ((started != Status::Ok && started != Status::Busy) || created->wireState() > 2)
        {
            destroyRouteInstallOwner(created);
            return started == Status::Ok ? Status::Internal : started;
        }
        // The route owner now holds the immutable upload lease. Platform policy
        // must block all upload mutations while this slot is occupied.
        if (upload_runtime::owner)
        {
            upload_runtime::destroyOwner(upload_runtime::owner);
            upload_runtime::owner = nullptr;
        }
        slot = created;
    }
    if (!slot->identityMatches(generation, {author, 32})) return Status::StaleIdentity;
    slot->renew(now);
    const auto state = slot->wireState();
    if (state > 2 || (install && state == 2)) return Status::Internal;
    const auto& binding = slot->binding();
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    if (!writer.array(5) || !writer.binary(binding.transfer) || !writer.binary(binding.hash) || !writer.unsignedInteger(binding.bytes) ||
        !writer.text(slot->target()) || !writer.unsignedInteger(state)) return Status::Internal;
    written = writer.size();
    slot->markResultDelivered();
    return Status::Ok;
}
} // namespace hostlink::management::route_install_runtime
