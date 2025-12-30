/**
 * @file BLEPeerManager.cpp
 * @brief BLE-Reticulum Protocol v2.2 peer management implementation
 */

#include "BLEPeerManager.h"
#include "../Log.h"

#include <algorithm>
#include <cmath>

namespace RNS { namespace BLE {

BLEPeerManager::BLEPeerManager() {
    _local_mac = Bytes(6);  // Initialize to zeros
}

void BLEPeerManager::setLocalMac(const Bytes& mac) {
    if (mac.size() >= Limits::MAC_SIZE) {
        _local_mac = Bytes(mac.data(), Limits::MAC_SIZE);
    }
}

//=============================================================================
// Peer Discovery
//=============================================================================

bool BLEPeerManager::addDiscoveredPeer(const Bytes& mac_address, int8_t rssi, uint8_t address_type) {
    if (mac_address.size() < Limits::MAC_SIZE) {
        return false;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);
    double now = Utilities::OS::time();

    // Check if this MAC maps to a known identity
    auto mac_it = _mac_to_identity.find(mac);
    if (mac_it != _mac_to_identity.end()) {
        // Update existing peer with identity
        auto peer_it = _peers_by_identity.find(mac_it->second);
        if (peer_it != _peers_by_identity.end()) {
            PeerInfo& peer = peer_it->second;

            // Check if blacklisted
            if (peer.state == PeerState::BLACKLISTED && now < peer.blacklisted_until) {
                return false;
            }

            peer.last_seen = now;
            peer.rssi = rssi;
            peer.address_type = address_type;  // Update address type
            // Exponential moving average for RSSI
            peer.rssi_avg = static_cast<int8_t>(0.7f * peer.rssi_avg + 0.3f * rssi);

            return true;
        }
    }

    // Check if peer exists in MAC-only storage
    auto mac_only_it = _peers_by_mac_only.find(mac);
    if (mac_only_it != _peers_by_mac_only.end()) {
        PeerInfo& peer = mac_only_it->second;

        // Check if blacklisted
        if (peer.state == PeerState::BLACKLISTED && now < peer.blacklisted_until) {
            return false;
        }

        peer.last_seen = now;
        peer.rssi = rssi;
        peer.address_type = address_type;  // Update address type
        peer.rssi_avg = static_cast<int8_t>(0.7f * peer.rssi_avg + 0.3f * rssi);

        return true;
    }

    // New peer - add to MAC-only storage
    PeerInfo peer;
    peer.mac_address = mac;
    peer.address_type = address_type;  // Store address type
    peer.state = PeerState::DISCOVERED;
    peer.discovered_at = now;
    peer.last_seen = now;
    peer.rssi = rssi;
    peer.rssi_avg = rssi;

    _peers_by_mac_only[mac] = peer;

    DEBUG("BLEPeerManager: Discovered new peer " + BLEAddress(mac.data()).toString() +
          " RSSI " + std::to_string(rssi));

    return true;
}

bool BLEPeerManager::setPeerIdentity(const Bytes& mac_address, const Bytes& identity) {
    if (mac_address.size() < Limits::MAC_SIZE || identity.size() != Limits::IDENTITY_SIZE) {
        return false;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    // Check if peer exists in MAC-only storage
    auto mac_only_it = _peers_by_mac_only.find(mac);
    if (mac_only_it != _peers_by_mac_only.end()) {
        promoteToIdentityKeyed(mac, identity);
        return true;
    }

    // Check if peer already has identity (MAC might have changed)
    auto identity_it = _peers_by_identity.find(identity);
    if (identity_it != _peers_by_identity.end()) {
        // Update MAC address mapping
        PeerInfo& peer = identity_it->second;

        // Remove old MAC mapping if different
        if (peer.mac_address != mac) {
            _mac_to_identity.erase(peer.mac_address);
            peer.mac_address = mac;
            _mac_to_identity[mac] = identity;
        }

        return true;
    }

    // Peer not found
    WARNING("BLEPeerManager: Cannot set identity for unknown peer");
    return false;
}

bool BLEPeerManager::updatePeerMac(const Bytes& identity, const Bytes& new_mac) {
    if (identity.size() != Limits::IDENTITY_SIZE || new_mac.size() < Limits::MAC_SIZE) {
        return false;
    }

    Bytes mac(new_mac.data(), Limits::MAC_SIZE);

    auto identity_it = _peers_by_identity.find(identity);
    if (identity_it == _peers_by_identity.end()) {
        return false;
    }

    PeerInfo& peer = identity_it->second;

    // Remove old MAC mapping
    _mac_to_identity.erase(peer.mac_address);

    // Update to new MAC
    peer.mac_address = mac;
    _mac_to_identity[mac] = identity;

    DEBUG("BLEPeerManager: Updated MAC for peer to " + BLEAddress(mac.data()).toString());

    return true;
}

//=============================================================================
// Peer Lookup
//=============================================================================

PeerInfo* BLEPeerManager::getPeerByMac(const Bytes& mac_address) {
    if (mac_address.size() < Limits::MAC_SIZE) return nullptr;

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    // Check MAC-to-identity mapping first
    auto mac_it = _mac_to_identity.find(mac);
    if (mac_it != _mac_to_identity.end()) {
        auto identity_it = _peers_by_identity.find(mac_it->second);
        if (identity_it != _peers_by_identity.end()) {
            return &identity_it->second;
        }
    }

    // Check MAC-only storage
    auto mac_only_it = _peers_by_mac_only.find(mac);
    if (mac_only_it != _peers_by_mac_only.end()) {
        return &mac_only_it->second;
    }

    return nullptr;
}

const PeerInfo* BLEPeerManager::getPeerByMac(const Bytes& mac_address) const {
    return const_cast<BLEPeerManager*>(this)->getPeerByMac(mac_address);
}

PeerInfo* BLEPeerManager::getPeerByIdentity(const Bytes& identity) {
    if (identity.size() != Limits::IDENTITY_SIZE) return nullptr;

    auto it = _peers_by_identity.find(identity);
    if (it != _peers_by_identity.end()) {
        return &it->second;
    }

    return nullptr;
}

const PeerInfo* BLEPeerManager::getPeerByIdentity(const Bytes& identity) const {
    return const_cast<BLEPeerManager*>(this)->getPeerByIdentity(identity);
}

PeerInfo* BLEPeerManager::getPeerByHandle(uint16_t conn_handle) {
    // Search identity-keyed peers
    for (auto& kv : _peers_by_identity) {
        if (kv.second.conn_handle == conn_handle) {
            return &kv.second;
        }
    }

    // Search MAC-only peers
    for (auto& kv : _peers_by_mac_only) {
        if (kv.second.conn_handle == conn_handle) {
            return &kv.second;
        }
    }

    return nullptr;
}

const PeerInfo* BLEPeerManager::getPeerByHandle(uint16_t conn_handle) const {
    return const_cast<BLEPeerManager*>(this)->getPeerByHandle(conn_handle);
}

std::vector<PeerInfo*> BLEPeerManager::getConnectedPeers() {
    std::vector<PeerInfo*> result;

    for (auto& kv : _peers_by_identity) {
        if (kv.second.isConnected()) {
            result.push_back(&kv.second);
        }
    }

    for (auto& kv : _peers_by_mac_only) {
        if (kv.second.isConnected()) {
            result.push_back(&kv.second);
        }
    }

    return result;
}

std::vector<PeerInfo*> BLEPeerManager::getAllPeers() {
    std::vector<PeerInfo*> result;

    for (auto& kv : _peers_by_identity) {
        result.push_back(&kv.second);
    }

    for (auto& kv : _peers_by_mac_only) {
        result.push_back(&kv.second);
    }

    return result;
}

//=============================================================================
// Connection Management
//=============================================================================

PeerInfo* BLEPeerManager::getBestConnectionCandidate() {
    double now = Utilities::OS::time();
    PeerInfo* best = nullptr;
    float best_score = -1.0f;

    auto checkPeer = [&](PeerInfo& peer) {
        // Skip if already connected or connecting
        if (peer.state != PeerState::DISCOVERED) {
            return;
        }

        // Skip if blacklisted
        if (peer.state == PeerState::BLACKLISTED && now < peer.blacklisted_until) {
            return;
        }

        // Skip if we shouldn't initiate (MAC sorting)
        if (!shouldInitiateConnection(peer.mac_address)) {
            return;
        }

        if (peer.score > best_score) {
            best_score = peer.score;
            best = &peer;
        }
    };

    // Check identity-keyed peers (unlikely to be DISCOVERED state, but possible after disconnect)
    for (auto& kv : _peers_by_identity) {
        checkPeer(kv.second);
    }

    // Check MAC-only peers (more common for connection candidates)
    for (auto& kv : _peers_by_mac_only) {
        checkPeer(kv.second);
    }

    return best;
}

bool BLEPeerManager::shouldInitiateConnection(const Bytes& peer_mac) const {
    return shouldInitiateConnection(_local_mac, peer_mac);
}

bool BLEPeerManager::shouldInitiateConnection(const Bytes& our_mac, const Bytes& peer_mac) {
    if (our_mac.size() < Limits::MAC_SIZE || peer_mac.size() < Limits::MAC_SIZE) {
        return false;
    }

    // Lower MAC initiates connection
    BLEAddress our_addr(our_mac.data());
    BLEAddress peer_addr(peer_mac.data());

    return our_addr.isLowerThan(peer_addr);
}

void BLEPeerManager::connectionSucceeded(const Bytes& identifier) {
    PeerInfo* peer = findPeer(identifier);
    if (!peer) return;

    peer->connection_successes++;
    peer->consecutive_failures = 0;
    peer->connected_at = Utilities::OS::time();
    peer->state = PeerState::CONNECTED;

    DEBUG("BLEPeerManager: Connection succeeded for peer");
}

void BLEPeerManager::connectionFailed(const Bytes& identifier) {
    PeerInfo* peer = findPeer(identifier);
    if (!peer) return;

    peer->connection_failures++;
    peer->consecutive_failures++;
    peer->state = PeerState::DISCOVERED;

    // Check if should blacklist
    if (peer->consecutive_failures >= Limits::BLACKLIST_THRESHOLD) {
        double duration = calculateBlacklistDuration(peer->consecutive_failures);
        peer->blacklisted_until = Utilities::OS::time() + duration;
        peer->state = PeerState::BLACKLISTED;

        WARNING("BLEPeerManager: Blacklisted peer for " + std::to_string(duration) +
                "s after " + std::to_string(peer->consecutive_failures) + " failures");
    }
}

void BLEPeerManager::setPeerState(const Bytes& identifier, PeerState state) {
    PeerInfo* peer = findPeer(identifier);
    if (peer) {
        peer->state = state;
    }
}

void BLEPeerManager::setPeerHandle(const Bytes& identifier, uint16_t conn_handle) {
    PeerInfo* peer = findPeer(identifier);
    if (peer) {
        peer->conn_handle = conn_handle;
    }
}

void BLEPeerManager::setPeerMTU(const Bytes& identifier, uint16_t mtu) {
    PeerInfo* peer = findPeer(identifier);
    if (peer) {
        peer->mtu = mtu;
    }
}

void BLEPeerManager::removePeer(const Bytes& identifier) {
    // Try identity first
    if (identifier.size() == Limits::IDENTITY_SIZE) {
        auto identity_it = _peers_by_identity.find(identifier);
        if (identity_it != _peers_by_identity.end()) {
            // Remove MAC mapping
            _mac_to_identity.erase(identity_it->second.mac_address);
            _peers_by_identity.erase(identity_it);
            return;
        }
    }

    // Try MAC
    if (identifier.size() >= Limits::MAC_SIZE) {
        Bytes mac(identifier.data(), Limits::MAC_SIZE);

        // Check if maps to identity
        auto mac_it = _mac_to_identity.find(mac);
        if (mac_it != _mac_to_identity.end()) {
            _peers_by_identity.erase(mac_it->second);
            _mac_to_identity.erase(mac_it);
            return;
        }

        // Check MAC-only
        _peers_by_mac_only.erase(mac);
    }
}

void BLEPeerManager::updateRssi(const Bytes& identifier, int8_t rssi) {
    PeerInfo* peer = findPeer(identifier);
    if (peer) {
        peer->rssi = rssi;
        peer->rssi_avg = static_cast<int8_t>(0.7f * peer->rssi_avg + 0.3f * rssi);
    }
}

//=============================================================================
// Statistics
//=============================================================================

void BLEPeerManager::recordPacketSent(const Bytes& identifier) {
    PeerInfo* peer = findPeer(identifier);
    if (peer) {
        peer->packets_sent++;
        peer->last_activity = Utilities::OS::time();
    }
}

void BLEPeerManager::recordPacketReceived(const Bytes& identifier) {
    PeerInfo* peer = findPeer(identifier);
    if (peer) {
        peer->packets_received++;
        peer->last_activity = Utilities::OS::time();
    }
}

void BLEPeerManager::updateLastActivity(const Bytes& identifier) {
    PeerInfo* peer = findPeer(identifier);
    if (peer) {
        peer->last_activity = Utilities::OS::time();
    }
}

//=============================================================================
// Scoring & Blacklist
//=============================================================================

void BLEPeerManager::recalculateScores() {
    for (auto& kv : _peers_by_identity) {
        kv.second.score = calculateScore(kv.second);
    }

    for (auto& kv : _peers_by_mac_only) {
        kv.second.score = calculateScore(kv.second);
    }
}

void BLEPeerManager::checkBlacklistExpirations() {
    double now = Utilities::OS::time();

    auto checkAndClear = [now](PeerInfo& peer) {
        if (peer.state == PeerState::BLACKLISTED && now >= peer.blacklisted_until) {
            peer.state = PeerState::DISCOVERED;
            peer.blacklisted_until = 0;
            DEBUG("BLEPeerManager: Peer blacklist expired, restored to DISCOVERED");
        }
    };

    for (auto& kv : _peers_by_identity) {
        checkAndClear(kv.second);
    }

    for (auto& kv : _peers_by_mac_only) {
        checkAndClear(kv.second);
    }
}

//=============================================================================
// Counts & Limits
//=============================================================================

size_t BLEPeerManager::connectedCount() const {
    size_t count = 0;

    for (const auto& kv : _peers_by_identity) {
        if (kv.second.isConnected()) count++;
    }

    for (const auto& kv : _peers_by_mac_only) {
        if (kv.second.isConnected()) count++;
    }

    return count;
}

void BLEPeerManager::cleanupStalePeers(double max_age) {
    double now = Utilities::OS::time();
    std::vector<Bytes> to_remove;

    // Check MAC-only peers (identity-keyed peers are more persistent)
    for (const auto& kv : _peers_by_mac_only) {
        const PeerInfo& peer = kv.second;

        // Only clean up DISCOVERED peers (not connected or connecting)
        if (peer.state == PeerState::DISCOVERED) {
            double age = now - peer.last_seen;
            if (age > max_age) {
                to_remove.push_back(kv.first);
            }
        }
    }

    for (const Bytes& mac : to_remove) {
        _peers_by_mac_only.erase(mac);
        TRACE("BLEPeerManager: Removed stale peer " + BLEAddress(mac.data()).toString());
    }
}

//=============================================================================
// Private Methods
//=============================================================================

float BLEPeerManager::calculateScore(const PeerInfo& peer) const {
    double now = Utilities::OS::time();

    // RSSI component (60% weight)
    float rssi_norm = normalizeRssi(peer.rssi_avg);
    float rssi_score = Scoring::RSSI_WEIGHT * rssi_norm;

    // History component (30% weight)
    float history_score = 0.0f;
    if (peer.connection_attempts > 0) {
        float success_rate = static_cast<float>(peer.connection_successes) /
                             static_cast<float>(peer.connection_attempts);
        history_score = Scoring::HISTORY_WEIGHT * success_rate;
    } else {
        // New peer: benefit of the doubt (50%)
        history_score = Scoring::HISTORY_WEIGHT * 0.5f;
    }

    // Recency component (10% weight)
    float recency_score = 0.0f;
    double age = now - peer.last_seen;
    if (age < 5.0) {
        recency_score = Scoring::RECENCY_WEIGHT * 1.0f;
    } else if (age < 30.0) {
        // Linear decay from 1.0 to 0.0 over 25 seconds
        recency_score = Scoring::RECENCY_WEIGHT * (1.0f - static_cast<float>((age - 5.0) / 25.0));
    }

    return rssi_score + history_score + recency_score;
}

float BLEPeerManager::normalizeRssi(int8_t rssi) const {
    // Clamp to expected range
    if (rssi < Scoring::RSSI_MIN) rssi = Scoring::RSSI_MIN;
    if (rssi > Scoring::RSSI_MAX) rssi = Scoring::RSSI_MAX;

    // Map to 0.0-1.0
    return static_cast<float>(rssi - Scoring::RSSI_MIN) /
           static_cast<float>(Scoring::RSSI_MAX - Scoring::RSSI_MIN);
}

double BLEPeerManager::calculateBlacklistDuration(uint8_t failures) const {
    // Exponential backoff: 60s × min(2^(failures-3), 8)
    if (failures < Limits::BLACKLIST_THRESHOLD) {
        return 0;
    }

    uint8_t exponent = failures - Limits::BLACKLIST_THRESHOLD;
    uint8_t multiplier = 1 << exponent;  // 2^exponent
    if (multiplier > Limits::BLACKLIST_MAX_MULTIPLIER) {
        multiplier = Limits::BLACKLIST_MAX_MULTIPLIER;
    }

    return Timing::BLACKLIST_BASE_BACKOFF * multiplier;
}

PeerInfo* BLEPeerManager::findPeer(const Bytes& identifier) {
    // Try as identity
    if (identifier.size() == Limits::IDENTITY_SIZE) {
        auto it = _peers_by_identity.find(identifier);
        if (it != _peers_by_identity.end()) {
            return &it->second;
        }
    }

    // Try as MAC
    if (identifier.size() >= Limits::MAC_SIZE) {
        return getPeerByMac(identifier);
    }

    return nullptr;
}

void BLEPeerManager::promoteToIdentityKeyed(const Bytes& mac_address, const Bytes& identity) {
    auto mac_only_it = _peers_by_mac_only.find(mac_address);
    if (mac_only_it == _peers_by_mac_only.end()) {
        return;
    }

    // Copy peer info
    PeerInfo peer = mac_only_it->second;
    peer.identity = identity;

    // Remove from MAC-only storage
    _peers_by_mac_only.erase(mac_only_it);

    // Add to identity-keyed storage
    _peers_by_identity[identity] = peer;

    // Add MAC-to-identity mapping
    _mac_to_identity[mac_address] = identity;

    DEBUG("BLEPeerManager: Promoted peer to identity-keyed storage");
}

}} // namespace RNS::BLE
