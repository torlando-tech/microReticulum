/**
 * @file BLEIdentityManager.cpp
 * @brief BLE-Reticulum Protocol v2.2 identity handshake manager implementation
 */

#include "BLEIdentityManager.h"
#include "../Log.h"

namespace RNS { namespace BLE {

BLEIdentityManager::BLEIdentityManager() {
}

void BLEIdentityManager::setLocalIdentity(const Bytes& identity_hash) {
    if (identity_hash.size() >= Limits::IDENTITY_SIZE) {
        _local_identity = Bytes(identity_hash.data(), Limits::IDENTITY_SIZE);
        DEBUG("BLEIdentityManager: Local identity set: " + _local_identity.toHex().substr(0, 8) + "...");
    } else {
        ERROR("BLEIdentityManager: Invalid identity size: " + std::to_string(identity_hash.size()));
    }
}

void BLEIdentityManager::setHandshakeCompleteCallback(HandshakeCompleteCallback callback) {
    _handshake_complete_callback = callback;
}

void BLEIdentityManager::setHandshakeFailedCallback(HandshakeFailedCallback callback) {
    _handshake_failed_callback = callback;
}

//=============================================================================
// Handshake Operations
//=============================================================================

Bytes BLEIdentityManager::initiateHandshake(const Bytes& mac_address) {
    if (!hasLocalIdentity()) {
        ERROR("BLEIdentityManager: Cannot initiate handshake without local identity");
        return Bytes();
    }

    if (mac_address.size() < Limits::MAC_SIZE) {
        ERROR("BLEIdentityManager: Invalid MAC address size");
        return Bytes();
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    // Create or update handshake session
    HandshakeSession& session = getOrCreateSession(mac);
    session.is_central = true;
    session.state = HandshakeState::INITIATED;
    session.started_at = Utilities::OS::time();

    DEBUG("BLEIdentityManager: Initiating handshake as central with " +
          BLEAddress(mac.data()).toString());

    // Return our identity to be written to peer
    return _local_identity;
}

bool BLEIdentityManager::processReceivedData(const Bytes& mac_address, const Bytes& data, bool is_central) {
    if (mac_address.size() < Limits::MAC_SIZE) {
        return false;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    // Check if this looks like a handshake
    if (!isHandshakeData(data, mac)) {
        return false;  // Regular data, not consumed
    }

    // This is a handshake - extract peer's identity
    if (data.size() != Limits::IDENTITY_SIZE) {
        // Should not happen given isHandshakeData check, but be safe
        return false;
    }

    Bytes peer_identity(data.data(), Limits::IDENTITY_SIZE);

    DEBUG("BLEIdentityManager: Received identity handshake from " +
          BLEAddress(mac.data()).toString() + ": " +
          peer_identity.toHex().substr(0, 8) + "...");

    // Complete the handshake
    completeHandshake(mac, peer_identity, is_central);

    return true;  // Handshake data consumed
}

bool BLEIdentityManager::isHandshakeData(const Bytes& data, const Bytes& mac_address) const {
    // Handshake is detected if:
    // 1. Data is exactly 16 bytes (identity size)
    // 2. No existing identity mapping for this MAC

    if (data.size() != Limits::IDENTITY_SIZE) {
        return false;
    }

    if (mac_address.size() < Limits::MAC_SIZE) {
        return false;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    // Check if we already have identity for this MAC
    auto it = _address_to_identity.find(mac);
    if (it != _address_to_identity.end()) {
        // Already have identity - this is regular data, not handshake
        return false;
    }

    // No existing identity + 16 bytes = handshake
    return true;
}

void BLEIdentityManager::completeHandshake(const Bytes& mac_address, const Bytes& peer_identity,
                                            bool is_central) {
    if (mac_address.size() < Limits::MAC_SIZE || peer_identity.size() != Limits::IDENTITY_SIZE) {
        return;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);
    Bytes identity(peer_identity.data(), Limits::IDENTITY_SIZE);

    // Store bidirectional mapping
    _address_to_identity[mac] = identity;
    _identity_to_address[identity] = mac;

    // Remove handshake session
    _handshakes.erase(mac);

    DEBUG("BLEIdentityManager: Handshake complete with " +
          BLEAddress(mac.data()).toString() +
          " identity: " + identity.toHex().substr(0, 8) + "..." +
          (is_central ? " (we are central)" : " (we are peripheral)"));

    // Invoke callback
    if (_handshake_complete_callback) {
        _handshake_complete_callback(mac, identity, is_central);
    }
}

void BLEIdentityManager::checkTimeouts() {
    double now = Utilities::OS::time();
    std::vector<Bytes> timed_out;

    for (const auto& kv : _handshakes) {
        const HandshakeSession& session = kv.second;

        if (session.state != HandshakeState::COMPLETE) {
            double age = now - session.started_at;
            if (age > Timing::HANDSHAKE_TIMEOUT) {
                timed_out.push_back(kv.first);
            }
        }
    }

    for (const Bytes& mac : timed_out) {
        WARNING("BLEIdentityManager: Handshake timeout for " +
                BLEAddress(mac.data()).toString());

        if (_handshake_failed_callback) {
            _handshake_failed_callback(mac, "Handshake timeout");
        }

        _handshakes.erase(mac);
    }
}

//=============================================================================
// Identity Mapping
//=============================================================================

Bytes BLEIdentityManager::getIdentityForMac(const Bytes& mac_address) const {
    if (mac_address.size() < Limits::MAC_SIZE) {
        return Bytes();
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    auto it = _address_to_identity.find(mac);
    if (it != _address_to_identity.end()) {
        return it->second;
    }

    return Bytes();
}

Bytes BLEIdentityManager::getMacForIdentity(const Bytes& identity) const {
    if (identity.size() != Limits::IDENTITY_SIZE) {
        return Bytes();
    }

    auto it = _identity_to_address.find(identity);
    if (it != _identity_to_address.end()) {
        return it->second;
    }

    return Bytes();
}

bool BLEIdentityManager::hasIdentity(const Bytes& mac_address) const {
    if (mac_address.size() < Limits::MAC_SIZE) {
        return false;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);
    return _address_to_identity.find(mac) != _address_to_identity.end();
}

void BLEIdentityManager::updateMacForIdentity(const Bytes& identity, const Bytes& new_mac) {
    if (identity.size() != Limits::IDENTITY_SIZE || new_mac.size() < Limits::MAC_SIZE) {
        return;
    }

    Bytes mac(new_mac.data(), Limits::MAC_SIZE);

    auto identity_it = _identity_to_address.find(identity);
    if (identity_it == _identity_to_address.end()) {
        return;  // Unknown identity
    }

    // Remove old MAC mapping
    Bytes old_mac = identity_it->second;
    _address_to_identity.erase(old_mac);

    // Add new mappings
    _address_to_identity[mac] = identity;
    _identity_to_address[identity] = mac;

    DEBUG("BLEIdentityManager: Updated MAC for identity " +
          identity.toHex().substr(0, 8) + "... to " +
          BLEAddress(mac.data()).toString());
}

void BLEIdentityManager::removeMapping(const Bytes& mac_address) {
    if (mac_address.size() < Limits::MAC_SIZE) {
        return;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    auto it = _address_to_identity.find(mac);
    if (it != _address_to_identity.end()) {
        Bytes identity = it->second;
        _identity_to_address.erase(identity);
        _address_to_identity.erase(it);

        DEBUG("BLEIdentityManager: Removed mapping for " +
              BLEAddress(mac.data()).toString());
    }

    // Also clean up any pending handshake
    _handshakes.erase(mac);
}

void BLEIdentityManager::clearAllMappings() {
    _address_to_identity.clear();
    _identity_to_address.clear();
    _handshakes.clear();

    DEBUG("BLEIdentityManager: Cleared all identity mappings");
}

bool BLEIdentityManager::isHandshakeInProgress(const Bytes& mac_address) const {
    if (mac_address.size() < Limits::MAC_SIZE) {
        return false;
    }

    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    auto it = _handshakes.find(mac);
    if (it != _handshakes.end()) {
        return it->second.state != HandshakeState::NONE &&
               it->second.state != HandshakeState::COMPLETE;
    }

    return false;
}

//=============================================================================
// Private Methods
//=============================================================================

BLEIdentityManager::HandshakeSession& BLEIdentityManager::getOrCreateSession(const Bytes& mac_address) {
    Bytes mac(mac_address.data(), Limits::MAC_SIZE);

    auto it = _handshakes.find(mac);
    if (it != _handshakes.end()) {
        return it->second;
    }

    // Create new session
    HandshakeSession session;
    session.mac_address = mac;
    session.state = HandshakeState::NONE;
    session.started_at = Utilities::OS::time();

    _handshakes[mac] = session;
    return _handshakes[mac];
}

}} // namespace RNS::BLE
