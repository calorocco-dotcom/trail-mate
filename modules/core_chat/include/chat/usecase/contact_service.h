/**
 * @file contact_service.h
 * @brief Contact service (use case layer)
 *
 * Responsibilities:
 * - Coordinate node info and contact nickname management
 * - Provide unified interface to get display names (nickname or short_name)
 */

#pragma once

#include "../domain/contact_types.h"
#include "../ports/i_mesh_peer_directory.h"
#include "chat/domain/peer_browse_entry.h"
#include <string>
#include <vector>

namespace chat
{
namespace contacts
{

/**
 * @brief Contact service
 * Use case layer: coordinates domain model and storage adapters
 */
class ContactService
{
  public:
    /**
     * @brief Constructor with dependency injection
     * @param directory Unified durable peer directory
     */
    explicit ContactService(IMeshPeerDirectory& directory);
    ~ContactService() = default;

    /**
     * @brief Initialize service (load stores)
     */
    void begin();

    /** Selects the only protocol partition visible to contact queries. */
    void setActiveProtocol(MeshProtocol protocol);

    void applyNodeUpdate(uint32_t node_id, const NodeUpdate& update);
    bool recordPeer(const MeshPeerRecord& record);

    /**
     * @brief Update node facts decoded from a protocol metadata packet
     * @param node_id Node ID
     * @param short_name Short name
     * @param long_name Long name
     * @param snr Signal-to-Noise Ratio
     * @param now_secs Current timestamp (seconds)
     */
    void updateNodeInfo(uint32_t node_id, const char* short_name, const char* long_name,
                        float snr, float rssi, uint32_t now_secs, uint8_t protocol = 0,
                        uint8_t role = kNodeRoleUnknown, uint8_t hops_away = 0xFF,
                        uint8_t hw_model = 0, uint8_t channel = 0xFF);

    /**
     * @brief Update node protocol type (without changing names)
     */
    void updateNodeProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs);

    /**
     * @brief Update node position info
     */
    void updateNodePosition(uint32_t node_id, const NodePosition& pos);
    bool setNextHop(uint32_t node_id, uint8_t next_hop);
    uint8_t getNextHop(uint32_t node_id) const;

    /**
     * @brief Get display name for a node (nickname if contact, short_name otherwise)
     * @param node_id Node ID
     * @return Display name
     */
    std::string getContactName(uint32_t node_id) const;

    /**
     * @brief Get display name for a Reticulum destination identity
     * @param identity Reticulum peer destination identity
     * @return Saved nickname, announce/long name, or empty when unknown
     */
    std::string getReticulumContactName(const ReticulumPeerIdentity& identity) const;

    /**
     * @brief Get all contacts (nodes with nicknames)
     */
    std::vector<PeerDirectoryItem> getContacts() const;

    /**
     * @brief Get all nearby nodes (nodes without nicknames, visible within 6 days)
     */
    std::vector<PeerDirectoryItem> getNearby() const;

    /**
     * @brief Get ignored non-contact nodes so local admin UIs can still manage them
     */
    std::vector<PeerDirectoryItem> getIgnoredNodes() const;

    /** Get the active protocol's complete peer-directory projection. */
    std::vector<PeerDirectoryItem> getAllPeers() const;

    // Card navigation and single-peer hydration do not build cached_nodes_.
    MeshPeerDirectoryStatus browsePeers(MeshPeerDirectoryView view,
                                        const char* query,
                                        std::vector<PeerBrowseEntry>& output) const;
    MeshPeerDirectoryStatus readPeer(const PeerBrowseEntry& entry,
                                     PeerDirectoryItem& output) const;

    /**
     * @brief Add contact (set nickname)
     * @param node_id Node ID
     * @param nickname Nickname (max 12 bytes)
     * @return true if successful
     */
    bool addContact(uint32_t node_id, const char* nickname);

    /**
     * @brief Edit contact nickname
     * @param node_id Node ID
     * @param nickname New nickname (max 12 bytes)
     * @return true if successful
     */
    bool editContact(uint32_t node_id, const char* nickname);

    /**
     * @brief Remove contact (delete nickname)
     * @param node_id Node ID
     * @return true if removed
     */
    bool removeContact(uint32_t node_id);

    /**
     * @brief Remove a node entirely from local contact/node caches
     * @param node_id Node ID
     * @return true if any local record was removed
     */
    bool removeNode(uint32_t node_id);

    /**
     * @brief Set whether a node should be ignored by nearby/discovery style UX
     * @param node_id Node ID
     * @param ignored True to ignore, false to unignore
     * @return true if the node exists and was updated
     */
    bool setNodeIgnored(uint32_t node_id, bool ignored);

    /**
     * @brief Set whether a node's public key has been manually trusted/verified
     * @param node_id Node ID
     * @param verified True to trust, false to clear trust
     * @return true if the node exists and was updated
     */
    bool setNodeKeyManuallyVerified(uint32_t node_id, bool verified);

    /**
     * @brief Get node info by node_id
     * @param node_id Node ID
     * @return Peer projection if found, nullptr otherwise
     */
    const PeerDirectoryItem* getPeerByNodeId(uint32_t node_id) const;

    bool findNodeIdByReticulumDestinationHash(
        const uint8_t destination_hash[kReticulumPeerHashSize],
        uint32_t* out_node_id) const;

    /**
     * @brief Clear cached node info
     */
    void clearCache();

  private:
    IMeshPeerDirectory& directory_;
    MeshProtocol active_protocol_ = MeshProtocol::Meshtastic;
    mutable std::vector<PeerDirectoryItem> cached_nodes_;
    mutable uint32_t cache_timestamp_;
    mutable MeshPeerRecord update_scratch_{};
    mutable MeshPeerRecord lookup_scratch_{};
    static constexpr uint32_t kCacheTimeoutMs = 1000; // 1 second cache

    void invalidateCache() const;
    void buildCache() const;
    bool ensureNodeExistsForContact(uint32_t node_id);
    bool hasNodeEntry(uint32_t node_id) const;
    bool isNodeVisible(uint32_t last_seen) const;
    std::string formatTimeStatus(uint32_t last_seen) const;
};

} // namespace contacts
} // namespace chat
