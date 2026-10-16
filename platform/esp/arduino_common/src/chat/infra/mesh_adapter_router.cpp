/**
 * @file mesh_adapter_router.cpp
 * @brief Thread-safe mesh adapter router for runtime protocol switching
 */

#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"

#include "geocaching/protocol/sign_record.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/local_identity_access.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/ui/reticulum_call_runtime.h"
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
#include "hostlink/identity_backup_export.h"
#include "platform/esp/common/radio_identity_management.h"
#include "track_sharing/device_signer.h"
#endif

namespace chat
{

MeshAdapterRouter::LockGuard::LockGuard(SemaphoreHandle_t mutex,
                                        TickType_t wait_ticks)
    : mutex_(mutex)
{
    if (mutex_ != nullptr)
    {
        locked_ = (xSemaphoreTake(mutex_, wait_ticks) == pdTRUE);
    }
}

MeshAdapterRouter::LockGuard::~LockGuard()
{
    if (locked_ && mutex_ != nullptr)
    {
        xSemaphoreGive(mutex_);
    }
}

MeshAdapterRouter::MeshAdapterRouter()
{
    mutex_ = xSemaphoreCreateMutex();
}

MeshAdapterRouter::~MeshAdapterRouter()
{
    if (mutex_ != nullptr)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool MeshAdapterRouter::installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend)
{
    LockGuard lock(mutex_);
    if (!lock.locked()) return false;
    applyGeocachingHandlers(protocol, backend.get());
    return core_.installBackend(protocol, std::move(backend));
}

bool MeshAdapterRouter::hasBackend() const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.hasBackend();
}

bool MeshAdapterRouter::installServiceBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend)
{
    LockGuard lock(mutex_);
    if (!lock.locked()) return false;
    applyGeocachingHandlers(protocol, backend.get());
    return core_.installServiceBackend(protocol, std::move(backend));
}

void MeshAdapterRouter::applyGeocachingHandlers(MeshProtocol protocol, IMeshAdapter* backend)
{
    if (!backend || (protocol != MeshProtocol::Reticulum && protocol != MeshProtocol::RNode)) return;
    if (auto* tracks = backend->trackTransport()) tracks->setTrackDeliveryBinding(track_delivery_binding_);
    auto* service = backend->geocachingTransport();
    if (!service) return;
    service->setGeocachingAnnouncementHandler(geocaching_announcement_, geocaching_context_);
    service->setGeocachingDeliveryHandler(geocaching_delivery_, geocaching_context_);
}

bool MeshAdapterRouter::bindGeocachingHandlers(
    void (*announcement)(const lxmf::GeocachingAnnouncementView&, void*),
    bool (*delivery)(const lxmf::CustomDeliveryView&, void*), void* context)
{
    LockGuard lock(mutex_);
    if (!lock.locked()) return false;
    geocaching_announcement_ = announcement;
    geocaching_delivery_ = delivery;
    geocaching_context_ = context;
    applyGeocachingHandlers(MeshProtocol::Reticulum, core_.backendForProtocol(MeshProtocol::Reticulum));
    return true;
}

bool MeshAdapterRouter::processServiceQueue(MeshProtocol protocol)
{
    LockGuard lock(mutex_, 0);
    return lock.locked() && core_.processServiceQueue(protocol);
}

bool MeshAdapterRouter::bindTrackDelivery(const lxmf::TrackDeliveryBinding* binding)
{
    // Legacy entry is acquire-only: removal/replacement must name its owner.
    return replaceTrackDelivery(nullptr, binding) == TrackBindingChange::Applied;
}

TrackBindingChange MeshAdapterRouter::replaceTrackDelivery(const lxmf::TrackDeliveryBinding* expected, const lxmf::TrackDeliveryBinding* replacement)
{
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return TrackBindingChange::Busy;
    if (track_delivery_binding_ != expected) return TrackBindingChange::Conflict;
    if (expected == replacement) return TrackBindingChange::Applied;
    track_delivery_binding_ = replacement;
    auto* backend = core_.backendForProtocol(MeshProtocol::Reticulum);
    if (backend && backend->trackTransport()) backend->trackTransport()->setTrackDeliveryBinding(replacement);
    return TrackBindingChange::Applied;
}

TrackBindingChange MeshAdapterRouter::visitTrackDelivery(const lxmf::TrackDeliveryBinding* expected, uint8_t* author, uint32_t& generation,
                                                         TrackBindingVisit visit, void* context)
{
    if (!expected || !author || !visit || !expected->expected_local_author || !expected->expected_author_generation) return TrackBindingChange::Conflict;
    const auto a = reinterpret_cast<uintptr_t>(author), b = reinterpret_cast<uintptr_t>(expected->expected_local_author);
    if (a <= b ? b - a < 64 : a - b < 64) return TrackBindingChange::Conflict;
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return TrackBindingChange::Busy;
    if (track_delivery_binding_ != expected) return TrackBindingChange::Conflict;
    const auto* backend = deviceAuthorBackendLocked();
    if (!backend) return TrackBindingChange::Busy;
    const auto read = backend->readDeviceAuthorKey(author, generation);
    if (read != DeviceAuthorResult::Ok) return read == DeviceAuthorResult::Unavailable ? TrackBindingChange::Busy : TrackBindingChange::Conflict;
    if (generation != expected->expected_author_generation || std::memcmp(author, expected->expected_local_author, 64)) return TrackBindingChange::Conflict;
    visit(context);
    return TrackBindingChange::Applied;
}

bool MeshAdapterRouter::trackLocalDestination(uint8_t* output)
{
    if (!output) return false;
    std::memset(output, 0, 16);
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return false;
    const auto* backend = deviceAuthorBackendLocked();
    return backend && backend->isReady() && geocachingDestinationLocked(output);
}

bool MeshAdapterRouter::requestTrackDirectoryPath(const uint8_t* destination)
{
    if (!destination) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < 16; ++i) bits |= destination[i];
    if (!bits) return false;
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || !track_delivery_binding_ || !deviceAuthorBackendLocked()) return false;
    auto* backend = core_.backendForProtocol(MeshProtocol::Reticulum);
    auto* transport = backend && backend->isReady() ? backend->trackTransport() : nullptr;
    return transport && transport->requestDirectoryPath(destination);
}

MeshSendResult MeshAdapterRouter::sendTrackData(const uint8_t destination[16], lxmf::ByteSpan data,
                                                const uint8_t expected_author[64], uint32_t expected_generation,
                                                bool response, std::array<uint8_t, 32>* accepted_hash)
{
    if (accepted_hash) accepted_hash->fill(0);
    if (!destination || !data.data || !data.size || data.size > 8192 || !expected_author || !expected_generation)
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return MeshSendResult::fail(MeshOperationFailure::Busy);
    if (!deviceAuthorBackendLocked()) return MeshSendResult::fail(MeshOperationFailure::NotReady);
    auto* backend = core_.backendForProtocol(MeshProtocol::Reticulum);
    auto* transport = backend ? backend->trackTransport() : nullptr;
    if (!backend || !backend->isReady() || !transport) return MeshSendResult::fail(MeshOperationFailure::NotReady);
    uint8_t current[64]{};
    uint32_t generation = 0;
    if (backend->readDeviceAuthorKey(current, generation) != DeviceAuthorResult::Ok ||
        generation != expected_generation || std::memcmp(current, expected_author, sizeof(current)))
        return MeshSendResult::fail(MeshOperationFailure::LocalIdentityMissing);
    return transport->sendTrackData(destination, data, response, accepted_hash);
}

std::unique_ptr<IMeshAdapter> MeshAdapterRouter::takeServiceBackend(MeshProtocol protocol, const IMeshAdapter* expected)
{
    LockGuard lock(mutex_, 0);
    return lock.locked() ? core_.takeServiceBackend(protocol, expected) : nullptr;
}

std::unique_ptr<IMeshAdapter> MeshAdapterRouter::takeInactiveReticulumCache()
{
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || core_.isServiceBackend(MeshProtocol::Reticulum)) return {};
    return core_.takeInactiveBackend(MeshProtocol::Reticulum);
}

MeshSendResult MeshAdapterRouter::sendGeocachingData(const uint8_t destination_hash[16],
                                                     lxmf::ByteSpan data, bool response, std::array<uint8_t, 32>* accepted_lxmf_hash,
                                                     const uint8_t expected_source[16])
{
    if (accepted_lxmf_hash) accepted_lxmf_hash->fill(0);
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return MeshSendResult::fail(MeshOperationFailure::Busy);
    auto* service = geocachingTransportLocked();
    if (!service) return MeshSendResult::fail(MeshOperationFailure::NotReady);
    if (expected_source)
    {
        uint8_t current[16]{};
        if (!geocachingDestinationLocked(current) || std::memcmp(current, expected_source, 16))
            return MeshSendResult::fail(MeshOperationFailure::LocalIdentityMissing);
    }
    return service->sendGeocachingData(destination_hash, data, response, accepted_lxmf_hash);
}

bool MeshAdapterRouter::geocachingDestinationLocked(uint8_t out[16])
{
    auto* service = geocachingTransportLocked();
    uint8_t public_key[64]{}, identity[16]{}, name[reticulum::kNameHashSize]{};
    if (!service || !service->getGeocachingAuthorKey(public_key)) return false;
    reticulum::computeIdentityHash(public_key, identity);
    reticulum::computeNameHash("lxmf", "delivery", name);
    reticulum::computeDestinationHash(name, identity, out);
    return true;
}

bool MeshAdapterRouter::getGeocachingDispatchDestination(uint8_t out[16])
{
    if (!out) return false;
    std::memset(out, 0, 16);
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || !geocachingTransportLocked()) return false;
    auto* backend = core_.backendForProtocol(MeshProtocol::Reticulum);
    return backend && backend->isReady() && geocachingDestinationLocked(out);
}

IGeocachingTransport* MeshAdapterRouter::geocachingTransportLocked()
{
    const auto active = core_.backendProtocol();
    if (active != MeshProtocol::Reticulum && active != MeshProtocol::RNode &&
        !core_.isServiceBackend(MeshProtocol::Reticulum))
    {
        return nullptr;
    }
    auto* backend = core_.backendForProtocol(MeshProtocol::Reticulum);
    return backend ? backend->geocachingTransport() : nullptr;
}

const IMeshAdapter* MeshAdapterRouter::deviceAuthorBackendLocked() const
{
    if (lxmf::LocalIdentityLease::rebootRequired()) return nullptr;
    const auto active = core_.backendProtocol();
    if (active != MeshProtocol::Reticulum && active != MeshProtocol::RNode &&
        !core_.isServiceBackend(MeshProtocol::Reticulum)) return nullptr;
    return core_.backendForProtocol(MeshProtocol::Reticulum);
}

DeviceAuthorResult MeshAdapterRouter::readDeviceAuthorKey(uint8_t* public_key, uint32_t& generation) const
{
    generation = 0;
    if (lxmf::LocalIdentityLease::rebootRequired()) return DeviceAuthorResult::Unavailable;
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return DeviceAuthorResult::Unavailable;
    const auto* backend = deviceAuthorBackendLocked();
    if (backend) return backend->readDeviceAuthorKey(public_key, generation);
    // Establish the device account on first USB use without starting RF.
    // init() only generates when all identity key storage is absent; partial
    // or unreadable material is refused by the existing persistence policy.
    lxmf::LocalIdentityLease identity([](lxmf::LxmfIdentity& value)
                                      { return value.init(); });
    return identity.read(public_key, generation);
}
DeviceAuthorResult MeshAdapterRouter::exportDeviceIdentityBackup(uint32_t generation, uint8_t* output, size_t capacity, size_t& written) const
{
    written = 0;
    if (lxmf::LocalIdentityLease::rebootRequired()) return DeviceAuthorResult::Unavailable;
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return DeviceAuthorResult::Unavailable;
    const auto* backend = deviceAuthorBackendLocked();
    if (backend) return backend->exportDeviceIdentityBackup(generation, output, capacity, written);
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    lxmf::LocalIdentityLease identity;
    return identity.get() ? hostlink::management::exportNativeIdentityBackup(*identity.get(), generation, output, capacity, written) : identity.result();
#else
    return DeviceAuthorResult::Unsupported;
#endif
}
DeviceIdentityRestoreResult MeshAdapterRouter::restoreDeviceIdentity(const hostlink::management::IdentityRestoreView& request, uint8_t* work, size_t capacity)
{
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || lxmf::LocalIdentityLease::rebootRequired()) return DeviceIdentityRestoreResult::Unavailable;
    auto* backend = deviceAuthorBackendLocked() ? core_.backendForProtocol(MeshProtocol::Reticulum) : nullptr;
    DeviceIdentityRestoreResult result;
    if (backend) result = backend->restoreDeviceIdentity(request, work, capacity);
    else
    {
        lxmf::LocalIdentityLease identity;
        result = identity.get() ? identity.get()->restoreNativeIdentity(request, work, capacity) : DeviceIdentityRestoreResult::Unavailable;
    }
    lxmf::LocalIdentityLease::noteRestore(result);
    return result;
}
DeviceAuthorResult MeshAdapterRouter::readRadioIdentityInfo(uint8_t kind, uint32_t generation, uint8_t* output, size_t capacity, size_t& written) const
{
    written = 0;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || lxmf::LocalIdentityLease::rebootRequired()) return DeviceAuthorResult::Unavailable;
    lxmf::LocalIdentityLease identity;
    return identity.get() ? platform::esp::common::readRadioIdentityInfo(*identity.get(), kind, generation, output, capacity, written) : identity.result();
#else
    return DeviceAuthorResult::Unsupported;
#endif
}
DeviceAuthorResult MeshAdapterRouter::exportRadioIdentityBackup(uint8_t kind, uint32_t generation, uint8_t* output, size_t capacity, size_t& written, const uint8_t* session, const uint8_t* request) const
{
    written = 0;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || lxmf::LocalIdentityLease::rebootRequired()) return DeviceAuthorResult::Unavailable;
    lxmf::LocalIdentityLease identity;
    return identity.get() ? platform::esp::common::exportRadioIdentityBackup(*identity.get(), kind, generation, output, capacity, written, session, request) : identity.result();
#else
    return DeviceAuthorResult::Unsupported;
#endif
}
DeviceIdentityRestoreResult MeshAdapterRouter::restoreRadioIdentity(const hostlink::management::RadioIdentityRestoreView& request, uint8_t* work, size_t capacity)
{
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || lxmf::LocalIdentityLease::rebootRequired()) return DeviceIdentityRestoreResult::Unavailable;
    lxmf::LocalIdentityLease identity;
    const auto result = identity.get() ? platform::esp::common::restoreRadioIdentity(*identity.get(), request, work, capacity) : DeviceIdentityRestoreResult::Unavailable;
    lxmf::LocalIdentityLease::noteRestore(result);
    return result;
#else
    return DeviceIdentityRestoreResult::Unsupported;
#endif
}
DeviceAuthorResult MeshAdapterRouter::signTrackManifest(const uint8_t* raw, size_t size, uint32_t generation, track_sharing::ManifestView& view,
                                                        uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity) const
{
    if (lxmf::LocalIdentityLease::rebootRequired()) return DeviceAuthorResult::Unavailable;
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return DeviceAuthorResult::Unavailable;
    const auto* backend = deviceAuthorBackendLocked();
    if (backend) return backend->signTrackManifest(raw, size, generation, view, workspace, capacity, signature, signature_capacity);
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    lxmf::LocalIdentityLease identity;
    return identity.get() ? track_sharing::signDeviceTrackManifest(*identity.get(), raw, size, generation, view, workspace, capacity, signature, signature_capacity) : identity.result();
#else
    return DeviceAuthorResult::Unsupported;
#endif
}

DeviceAuthorResult MeshAdapterRouter::proveDeviceAuthor(const uint8_t* session, const uint8_t* challenge,
                                                        uint32_t expected_generation, uint8_t* workspace, size_t capacity,
                                                        uint8_t* public_key, uint32_t& generation, uint8_t* signature) const
{
    generation = 0;
    if (lxmf::LocalIdentityLease::rebootRequired()) return DeviceAuthorResult::Unavailable;
    LockGuard lock(mutex_, 0);
    if (!lock.locked()) return DeviceAuthorResult::Unavailable;
    const auto* backend = deviceAuthorBackendLocked();
    // Resolve and sign under one lock; no backend pointer survives a switch.
    if (backend) return backend->proveDeviceAuthor(session, challenge, expected_generation, workspace, capacity,
                                                   public_key, generation, signature);
    lxmf::LocalIdentityLease identity;
    return identity.prove(session, challenge, expected_generation, workspace, capacity, public_key, generation, signature);
}

bool MeshAdapterRouter::getGeocachingAuthorKey(uint8_t out[64])
{
    if (!out) return false;
    std::memset(out, 0, 64);
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || lxmf::LocalIdentityLease::rebootRequired()) return false;
    auto* service = geocachingTransportLocked();
    if (service) return service->getGeocachingAuthorKey(out);
    lxmf::LocalIdentityLease identity;
    uint32_t generation = 0;
    return identity.read(out, generation) == DeviceAuthorResult::Ok;
}

bool MeshAdapterRouter::signGeocachingRecord(lxmf::ByteSpan record, uint8_t* workspace, size_t workspace_capacity,
                                             uint8_t* output, size_t output_capacity, size_t& written)
{
    written = 0;
    LockGuard lock(mutex_, 0);
    if (!lock.locked() || lxmf::LocalIdentityLease::rebootRequired()) return false;
    auto* service = geocachingTransportLocked();
    if (service) return service->signGeocachingRecord(record, workspace, workspace_capacity, output, output_capacity, written);
    lxmf::LocalIdentityLease identity;
    return identity.get() && ::geocaching::protocol::signGeocacheRecord({record.data, record.size}, *identity.get(), workspace, workspace_capacity, output, output_capacity, written);
}

MeshProtocol MeshAdapterRouter::backendProtocol() const
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.backendProtocol() : MeshProtocol::Meshtastic;
}

IMeshAdapter* MeshAdapterRouter::backendForProtocol(MeshProtocol protocol)
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.backendForProtocol(protocol) : nullptr;
}

const IMeshAdapter* MeshAdapterRouter::backendForProtocol(MeshProtocol protocol) const
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.backendForProtocol(protocol) : nullptr;
}

bool MeshAdapterRouter::deriveVmpContactSecret(NodeId peer_id,
                                               uint8_t out_secret[32])
{
    if (!out_secret)
    {
        return false;
    }
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return false;
    }

    const MeshProtocol protocol = core_.backendProtocol();
    IMeshAdapter* const backend = core_.backendForProtocol(protocol);
    if (!backend)
    {
        return false;
    }

    // AppContext installs these concrete production backends. Keeping this
    // Pager-only bridge here avoids adding a VMP key API to IMeshAdapter.
    if (protocol == MeshProtocol::Meshtastic)
    {
        return static_cast<meshtastic::MtAdapter*>(backend)->deriveVmpContactSecret(
            peer_id, out_secret);
    }
    if (protocol == MeshProtocol::MeshCore)
    {
        return static_cast<meshcore::MeshCoreAdapter*>(backend)->deriveVmpContactSecret(
            peer_id, out_secret);
    }
    if (protocol == MeshProtocol::Reticulum)
    {
        return static_cast<reticulum::ReticulumAdapter*>(backend)
            ->deriveVmpContactSecret(peer_id, out_secret);
    }
    return false;
}

MeshCapabilities MeshAdapterRouter::getCapabilities() const
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.getCapabilities() : MeshCapabilities{};
}

bool MeshAdapterRouter::sendText(ChannelId channel, const std::string& text,
                                 MessageId* out_msg_id, NodeId peer)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.sendText(channel, text, out_msg_id, peer);
}

bool MeshAdapterRouter::sendTextWithId(ChannelId channel, const std::string& text,
                                       MessageId forced_msg_id,
                                       MessageId* out_msg_id, NodeId peer)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.sendTextWithId(channel, text, forced_msg_id, out_msg_id, peer);
}

MeshSendResult MeshAdapterRouter::sendTextDetailed(ChannelId channel, const std::string& text,
                                                   MessageId forced_msg_id, NodeId peer)
{
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return MeshSendResult::fail(MeshOperationFailure::Busy);
    }
    return core_.sendTextDetailed(channel, text, forced_msg_id, peer);
}

MeshSendResult MeshAdapterRouter::sendTextToReticulumDestination(
    ChannelId channel,
    const std::string& text,
    MessageId forced_msg_id,
    const ReticulumPeerIdentity& destination)
{
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return MeshSendResult::fail(MeshOperationFailure::Busy);
    }
    return core_.sendTextToReticulumDestination(channel, text, forced_msg_id, destination);
}

bool MeshAdapterRouter::pollIncomingText(MeshIncomingText* out)
{
    // UI/runtime polls must not wait behind synchronous radio work that can
    // retain the router lock for the full LoRa airtime.
    LockGuard lock(mutex_, 0);
    return lock.locked() && core_.pollIncomingText(out);
}

IIncomingDeliveryCommitPort* MeshAdapterRouter::incomingDeliveryCommitPort()
{
    return this;
}

void MeshAdapterRouter::commitIncomingText(const MeshIncomingText& message,
                                           bool durably_accepted)
{
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return;
    }

    IMeshAdapter* backend = core_.backendForProtocol(core_.backendProtocol());
    IIncomingDeliveryCommitPort* commit_port =
        backend ? backend->incomingDeliveryCommitPort() : nullptr;
    if (commit_port)
    {
        commit_port->commitIncomingText(message, durably_accepted);
    }
}

bool MeshAdapterRouter::sendAppData(ChannelId channel, uint32_t portnum,
                                    const uint8_t* payload, size_t len,
                                    NodeId dest, bool want_ack,
                                    MessageId packet_id,
                                    bool want_response)
{
    LockGuard lock(mutex_);
    return lock.locked() &&
           core_.sendAppData(channel, portnum, payload, len, dest, want_ack, packet_id, want_response);
}

bool MeshAdapterRouter::pollIncomingData(MeshIncomingData* out)
{
    LockGuard lock(mutex_, 0);
    return lock.locked() && core_.pollIncomingData(out);
}

bool MeshAdapterRouter::requestNodeInfo(NodeId dest, bool want_response)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.requestNodeInfo(dest, want_response);
}

bool MeshAdapterRouter::broadcastSelfIdentity()
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.broadcastSelfIdentity();
}

bool MeshAdapterRouter::startKeyVerification(NodeId dest)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.startKeyVerification(dest);
}

bool MeshAdapterRouter::submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.submitKeyVerificationNumber(dest, nonce, number);
}

NodeId MeshAdapterRouter::getNodeId() const
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // The P4 IDF radio runtime can spend a full LoRa airtime inside
    // processSendQueue() while holding the router lock.  Node identity is a
    // presentation read, so do not stall the LVGL task behind that work.
    LockGuard lock(mutex_, 0);
#else
    LockGuard lock(mutex_);
#endif
    return lock.locked() ? core_.getNodeId() : 0;
}

bool MeshAdapterRouter::isPkiReady() const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.isPkiReady();
}

bool MeshAdapterRouter::getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // Settings may be opened while an announce is on air.  A temporarily
    // unavailable identity snapshot is preferable to blocking every LVGL
    // input event until the synchronous transmit finishes.
    LockGuard lock(mutex_, 0);
#else
    LockGuard lock(mutex_);
#endif
    if (!lock.locked())
    {
        if (out)
        {
            *out = ReticulumLocalIdentityInfo{};
        }
        return false;
    }
    return core_.getReticulumLocalIdentityInfo(out);
}

bool MeshAdapterRouter::hasPkiKey(NodeId dest) const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.hasPkiKey(dest);
}

bool MeshAdapterRouter::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.triggerDiscoveryAction(action);
}

MeshActionResult MeshAdapterRouter::triggerDiscoveryActionDetailed(MeshDiscoveryAction action)
{
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }
    return core_.triggerDiscoveryActionDetailed(action);
}

MeshActionResult MeshAdapterRouter::startReticulumAudioCall(
    const ReticulumPeerIdentity& destination)
{
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }
    return core_.startReticulumAudioCall(destination);
}

MeshActionResult MeshAdapterRouter::pingReticulumDestination(
    const ReticulumPeerIdentity& destination)
{
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }
    return core_.pingReticulumDestination(destination);
}

void MeshAdapterRouter::applyConfig(const MeshConfig& config)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.applyConfig(config);
    }
}

void MeshAdapterRouter::setUserInfo(const char* long_name, const char* short_name)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setUserInfo(long_name, short_name);
    }
}

void MeshAdapterRouter::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setNetworkLimits(duty_cycle_enabled, util_percent);
    }
}

void MeshAdapterRouter::setPrivacyConfig(uint8_t encrypt_mode)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setPrivacyConfig(encrypt_mode);
    }
}

bool MeshAdapterRouter::setWifiTransportEnabled(bool enabled)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.setWifiTransportEnabled(enabled);
}

bool MeshAdapterRouter::isReady() const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.isReady();
}

bool MeshAdapterRouter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    LockGuard lock(mutex_, 0);
    return lock.locked() && core_.pollIncomingRawPacket(out_data, out_len, max_len);
}

void MeshAdapterRouter::handleRawPacket(const uint8_t* data, size_t size)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.handleRawPacket(data, size);
    }
}

void MeshAdapterRouter::setLastRxStats(float rssi, float snr)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setLastRxStats(rssi, snr);
    }
}

void MeshAdapterRouter::processSendQueue()
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.processSendQueue();
        if (!::platform::ui::reticulum_call::realtime_mode_active())
        {
            // Only an explicitly installed service is eligible. The core also
            // skips the active Reticulum/RNode slot to prevent double polling.
            core_.processServiceQueue(MeshProtocol::Reticulum);
        }
    }
}

} // namespace chat
