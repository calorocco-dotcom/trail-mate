#pragma once

#include "chat/domain/mesh_peer_directory.h"
#include <array>

namespace chat::contacts
{

// A navigation entry deliberately contains no display strings, position or
// device metrics. Identity bytes remain complete: abbreviations are UI only.
struct PeerBrowseEntry
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    MeshPeerIdentityKind kind = MeshPeerIdentityKind::None;
    uint32_t node_id = 0;
    uint32_t last_seen = 0;
    std::array<uint8_t, 32> identity_bytes{};
    uint8_t identity_size = 0;

    static bool fromRecord(const MeshPeerRecord& record, PeerBrowseEntry& out)
    {
        if (!meshPeerRecordIsValid(record)) return false;
        const auto& identity = record.identity;
        out = {};
        out.protocol = identity.protocol;
        out.kind = identity.kind;
        out.node_id = meshPeerProjectedNodeId(record);
        out.last_seen = record.last_seen_s;
        if (identity.kind == MeshPeerIdentityKind::PublicKey)
        {
            if (identity.public_key_len > out.identity_bytes.size()) return false;
            out.identity_size = identity.public_key_len;
            std::memcpy(out.identity_bytes.data(), identity.public_key, out.identity_size);
        }
        else if (identity.kind == MeshPeerIdentityKind::ReticulumDestination)
        {
            out.identity_size = kReticulumPeerHashSize;
            std::memcpy(out.identity_bytes.data(), identity.reticulum.destination_hash, out.identity_size);
        }
        return true;
    }

    MeshPeerIdentity identity() const
    {
        MeshPeerIdentity result{};
        result.protocol = protocol;
        result.kind = kind;
        result.node_id = node_id;
        if (kind == MeshPeerIdentityKind::PublicKey && identity_size <= identity_bytes.size())
        {
            result.public_key_len = identity_size;
            std::memcpy(result.public_key, identity_bytes.data(), identity_size);
        }
        else if (kind == MeshPeerIdentityKind::ReticulumDestination && identity_size == kReticulumPeerHashSize)
        {
            result.reticulum = makeReticulumDestinationIdentity(identity_bytes.data());
        }
        return result;
    }

    bool samePeer(const PeerBrowseEntry& other) const
    {
        if (protocol != other.protocol || kind != other.kind) return false;
        if (kind == MeshPeerIdentityKind::NodeId) return node_id == other.node_id;
        return identity_size == other.identity_size &&
               std::memcmp(identity_bytes.data(), other.identity_bytes.data(), identity_size) == 0;
    }
};

static_assert(sizeof(PeerBrowseEntry) <= 52, "Peer navigation must stay compact");

} // namespace chat::contacts
