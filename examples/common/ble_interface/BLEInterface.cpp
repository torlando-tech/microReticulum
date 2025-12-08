/**
 * @file BLEInterface.cpp
 * @brief BLE-Reticulum Protocol v2.2 interface implementation
 */

#include "BLEInterface.h"
#include "../../../src/Log.h"
#include "../../../src/Utilities/OS.h"

using namespace RNS;
using namespace RNS::BLE;

BLEInterface::BLEInterface(const char* name) : InterfaceImpl(name) {
    _IN = true;
    _OUT = true;
    _bitrate = BITRATE_GUESS;
    _HW_MTU = HW_MTU_DEFAULT;
}

BLEInterface::~BLEInterface() {
    stop();
}

//=============================================================================
// Configuration
//=============================================================================

void BLEInterface::setRole(Role role) {
    _role = role;
}

void BLEInterface::setDeviceName(const std::string& name) {
    _device_name = name;
}

void BLEInterface::setLocalIdentity(const Bytes& identity) {
    if (identity.size() >= Limits::IDENTITY_SIZE) {
        _local_identity = Bytes(identity.data(), Limits::IDENTITY_SIZE);
        _identity_manager.setLocalIdentity(_local_identity);
    }
}

void BLEInterface::setMaxConnections(uint8_t max) {
    _max_connections = (max <= Limits::MAX_PEERS) ? max : Limits::MAX_PEERS;
}

//=============================================================================
// InterfaceImpl Overrides
//=============================================================================

bool BLEInterface::start() {
    if (_platform && _platform->isRunning()) {
        return true;
    }

    // Validate identity
    if (!_identity_manager.hasLocalIdentity()) {
        ERROR("BLEInterface: Local identity not set");
        return false;
    }

    // Create platform
    _platform = BLEPlatformFactory::create();
    if (!_platform) {
        ERROR("BLEInterface: Failed to create BLE platform");
        return false;
    }

    // Configure platform
    PlatformConfig config;
    config.role = _role;
    config.device_name = _device_name;
    config.preferred_mtu = MTU::REQUESTED;
    config.max_connections = _max_connections;

    if (!_platform->initialize(config)) {
        ERROR("BLEInterface: Failed to initialize BLE platform");
        _platform.reset();
        return false;
    }

    // Setup callbacks
    setupCallbacks();

    // Set identity data for peripheral mode
    _platform->setIdentityData(_local_identity);

    // Set local MAC in peer manager
    _peer_manager.setLocalMac(_platform->getLocalAddress().toBytes());

    // Start platform
    if (!_platform->start()) {
        ERROR("BLEInterface: Failed to start BLE platform");
        _platform.reset();
        return false;
    }

    _online = true;
    _last_scan = 0;  // Trigger immediate scan
    _last_keepalive = Utilities::OS::time();
    _last_maintenance = Utilities::OS::time();

    INFO("BLEInterface: Started, role: " + std::string(roleToString(_role)) +
         ", identity: " + _local_identity.toHex().substr(0, 8) + "...");

    return true;
}

void BLEInterface::stop() {
    if (_platform) {
        _platform->stop();
        _platform->shutdown();
        _platform.reset();
    }

    _fragmenters.clear();
    _online = false;

    INFO("BLEInterface: Stopped");
}

void BLEInterface::loop() {
    if (!_platform || !_platform->isRunning()) {
        return;
    }

    double now = Utilities::OS::time();

    // Platform loop
    _platform->loop();

    // Periodic scanning (central mode)
    if (_role == Role::CENTRAL || _role == Role::DUAL) {
        if (now - _last_scan >= SCAN_INTERVAL) {
            performScan();
            _last_scan = now;
        }
    }

    // Keepalive processing
    if (now - _last_keepalive >= KEEPALIVE_INTERVAL) {
        sendKeepalives();
        _last_keepalive = now;
    }

    // Maintenance (cleanup, scores, timeouts)
    if (now - _last_maintenance >= MAINTENANCE_INTERVAL) {
        performMaintenance();
        _last_maintenance = now;
    }
}

//=============================================================================
// Data Transfer
//=============================================================================

void BLEInterface::send_outgoing(const Bytes& data) {
    if (!_platform || !_platform->isRunning()) {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    // Get all connected peers
    auto connected_peers = _peer_manager.getConnectedPeers();

    if (connected_peers.empty()) {
        TRACE("BLEInterface: No connected peers, dropping packet");
        return;
    }

    // Send to all connected peers
    for (PeerInfo* peer : connected_peers) {
        if (peer->hasIdentity()) {
            sendToPeer(peer->identity, data);
        }
    }

    // Track outgoing stats
    handle_outgoing(data);
}

bool BLEInterface::sendToPeer(const Bytes& peer_identity, const Bytes& data) {
    PeerInfo* peer = _peer_manager.getPeerByIdentity(peer_identity);
    if (!peer || !peer->isConnected()) {
        return false;
    }

    // Get or create fragmenter for this peer
    auto frag_it = _fragmenters.find(peer_identity);
    if (frag_it == _fragmenters.end()) {
        _fragmenters[peer_identity] = BLEFragmenter(peer->mtu);
        frag_it = _fragmenters.find(peer_identity);
    }

    // Update MTU if changed
    frag_it->second.setMTU(peer->mtu);

    // Fragment the data
    std::vector<Bytes> fragments = frag_it->second.fragment(data);

    // Send each fragment
    for (const Bytes& fragment : fragments) {
        bool sent = false;

        if (peer->is_central) {
            // We are central - write to peripheral
            sent = _platform->write(peer->conn_handle, fragment, false);
        } else {
            // We are peripheral - notify central
            sent = _platform->notify(peer->conn_handle, fragment);
        }

        if (!sent) {
            WARNING("BLEInterface: Failed to send fragment to " +
                    peer_identity.toHex().substr(0, 8));
            return false;
        }
    }

    _peer_manager.recordPacketSent(peer_identity);
    return true;
}

//=============================================================================
// Status
//=============================================================================

size_t BLEInterface::peerCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _peer_manager.connectedCount();
}

//=============================================================================
// Platform Callbacks
//=============================================================================

void BLEInterface::setupCallbacks() {
    _platform->setOnScanResult([this](const ScanResult& result) {
        onScanResult(result);
    });

    _platform->setOnConnected([this](const ConnectionHandle& conn) {
        onConnected(conn);
    });

    _platform->setOnDisconnected([this](const ConnectionHandle& conn, uint8_t reason) {
        onDisconnected(conn, reason);
    });

    _platform->setOnMTUChanged([this](const ConnectionHandle& conn, uint16_t mtu) {
        onMTUChanged(conn, mtu);
    });

    _platform->setOnServicesDiscovered([this](const ConnectionHandle& conn, bool success) {
        onServicesDiscovered(conn, success);
    });

    _platform->setOnDataReceived([this](const ConnectionHandle& conn, const Bytes& data) {
        onDataReceived(conn, data);
    });

    _platform->setOnCentralConnected([this](const ConnectionHandle& conn) {
        onCentralConnected(conn);
    });

    _platform->setOnCentralDisconnected([this](const ConnectionHandle& conn) {
        onCentralDisconnected(conn);
    });

    _platform->setOnWriteReceived([this](const ConnectionHandle& conn, const Bytes& data) {
        onWriteReceived(conn, data);
    });

    // Identity manager callbacks
    _identity_manager.setHandshakeCompleteCallback(
        [this](const Bytes& mac, const Bytes& identity, bool is_central) {
            onHandshakeComplete(mac, identity, is_central);
        });

    _identity_manager.setHandshakeFailedCallback(
        [this](const Bytes& mac, const std::string& reason) {
            onHandshakeFailed(mac, reason);
        });

    // Reassembler callbacks
    _reassembler.setReassemblyCallback(
        [this](const Bytes& peer_identity, const Bytes& packet) {
            onPacketReassembled(peer_identity, packet);
        });

    _reassembler.setTimeoutCallback(
        [this](const Bytes& peer_identity, const std::string& reason) {
            onReassemblyTimeout(peer_identity, reason);
        });
}

void BLEInterface::onScanResult(const ScanResult& result) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!result.has_reticulum_service) {
        return;
    }

    // Add to peer manager
    _peer_manager.addDiscoveredPeer(result.address.toBytes(), result.rssi);

    DEBUG("BLEInterface: Discovered " + result.address.toString() +
          " RSSI: " + std::to_string(result.rssi));
}

void BLEInterface::onConnected(const ConnectionHandle& conn) {
    std::lock_guard<std::mutex> lock(_mutex);

    Bytes mac = conn.peer_address.toBytes();

    // Update peer state
    _peer_manager.setPeerState(mac, PeerState::HANDSHAKING);
    _peer_manager.setPeerHandle(mac, conn.handle);

    DEBUG("BLEInterface: Connected to " + conn.peer_address.toString() +
          " (we are central)");

    // Discover services
    _platform->discoverServices(conn.handle);
}

void BLEInterface::onDisconnected(const ConnectionHandle& conn, uint8_t reason) {
    std::lock_guard<std::mutex> lock(_mutex);

    Bytes identity = _identity_manager.getIdentityForMac(conn.peer_address.toBytes());

    if (identity.size() > 0) {
        // Clean up
        _fragmenters.erase(identity);
        _reassembler.clearForPeer(identity);
        _peer_manager.setPeerState(identity, PeerState::DISCOVERED);
    }

    _identity_manager.removeMapping(conn.peer_address.toBytes());

    DEBUG("BLEInterface: Disconnected from " + conn.peer_address.toString() +
          " reason: " + std::to_string(reason));
}

void BLEInterface::onMTUChanged(const ConnectionHandle& conn, uint16_t mtu) {
    std::lock_guard<std::mutex> lock(_mutex);

    Bytes mac = conn.peer_address.toBytes();
    _peer_manager.setPeerMTU(mac, mtu);

    // Update fragmenter if exists
    Bytes identity = _identity_manager.getIdentityForMac(mac);
    if (identity.size() > 0) {
        auto it = _fragmenters.find(identity);
        if (it != _fragmenters.end()) {
            it->second.setMTU(mtu);
        }
    }

    DEBUG("BLEInterface: MTU changed to " + std::to_string(mtu) +
          " for " + conn.peer_address.toString());
}

void BLEInterface::onServicesDiscovered(const ConnectionHandle& conn, bool success) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!success) {
        WARNING("BLEInterface: Service discovery failed for " + conn.peer_address.toString());
        _platform->disconnect(conn.handle);
        return;
    }

    DEBUG("BLEInterface: Services discovered for " + conn.peer_address.toString());

    // Enable notifications on TX characteristic
    _platform->enableNotifications(conn.handle, true);

    // Initiate handshake (as central)
    initiateHandshake(conn);
}

void BLEInterface::onDataReceived(const ConnectionHandle& conn, const Bytes& data) {
    // Called when we receive notification from peripheral (we are central)
    handleIncomingData(conn, data);
}

void BLEInterface::onCentralConnected(const ConnectionHandle& conn) {
    std::lock_guard<std::mutex> lock(_mutex);

    Bytes mac = conn.peer_address.toBytes();

    // Update peer manager
    _peer_manager.addDiscoveredPeer(mac, 0);
    _peer_manager.setPeerState(mac, PeerState::HANDSHAKING);
    _peer_manager.setPeerHandle(mac, conn.handle);

    // Mark as peripheral connection (they are central, we are peripheral)
    PeerInfo* peer = _peer_manager.getPeerByMac(mac);
    if (peer) {
        peer->is_central = false;  // We are NOT central in this connection
    }

    DEBUG("BLEInterface: Central connected: " + conn.peer_address.toString() +
          " (we are peripheral)");
}

void BLEInterface::onCentralDisconnected(const ConnectionHandle& conn) {
    onDisconnected(conn, 0);
}

void BLEInterface::onWriteReceived(const ConnectionHandle& conn, const Bytes& data) {
    // Called when central writes to our RX characteristic (we are peripheral)
    handleIncomingData(conn, data);
}

//=============================================================================
// Handshake Callbacks
//=============================================================================

void BLEInterface::onHandshakeComplete(const Bytes& mac, const Bytes& identity, bool is_central) {
    std::lock_guard<std::mutex> lock(_mutex);

    // Update peer manager with identity
    _peer_manager.setPeerIdentity(mac, identity);
    _peer_manager.connectionSucceeded(identity);

    // Create fragmenter for this peer
    PeerInfo* peer = _peer_manager.getPeerByIdentity(identity);
    uint16_t mtu = peer ? peer->mtu : MTU::MINIMUM;
    _fragmenters[identity] = BLEFragmenter(mtu);

    INFO("BLEInterface: Handshake complete with " + identity.toHex().substr(0, 8) +
         "... (we are " + (is_central ? "central" : "peripheral") + ")");
}

void BLEInterface::onHandshakeFailed(const Bytes& mac, const std::string& reason) {
    std::lock_guard<std::mutex> lock(_mutex);

    WARNING("BLEInterface: Handshake failed with " +
            BLEAddress(mac.data()).toString() + ": " + reason);

    _peer_manager.connectionFailed(mac);
}

//=============================================================================
// Reassembly Callbacks
//=============================================================================

void BLEInterface::onPacketReassembled(const Bytes& peer_identity, const Bytes& packet) {
    // Packet reassembly complete - pass to transport
    _peer_manager.recordPacketReceived(peer_identity);
    handle_incoming(packet);
}

void BLEInterface::onReassemblyTimeout(const Bytes& peer_identity, const std::string& reason) {
    WARNING("BLEInterface: Reassembly timeout for " +
            peer_identity.toHex().substr(0, 8) + ": " + reason);
}

//=============================================================================
// Internal Operations
//=============================================================================

void BLEInterface::performScan() {
    if (!_platform || _platform->isScanning()) {
        return;
    }

    // Only scan if we have room for more connections
    if (_peer_manager.connectedCount() >= _max_connections) {
        return;
    }

    _platform->startScan(5000);  // 5 second scan
}

void BLEInterface::processDiscoveredPeers() {
    // Find best connection candidate
    PeerInfo* candidate = _peer_manager.getBestConnectionCandidate();

    if (candidate && _peer_manager.canAcceptConnection()) {
        _peer_manager.setPeerState(candidate->mac_address, PeerState::CONNECTING);
        candidate->connection_attempts++;

        BLEAddress addr(candidate->mac_address.data());
        _platform->connect(addr, 10000);
    }
}

void BLEInterface::sendKeepalives() {
    // Send empty keepalive to maintain connections
    Bytes keepalive(1);
    keepalive.writable(1)[0] = 0x00;

    auto connected = _peer_manager.getConnectedPeers();
    for (PeerInfo* peer : connected) {
        if (peer->hasIdentity()) {
            // Don't use sendToPeer for keepalives (no fragmentation needed)
            if (peer->is_central) {
                _platform->write(peer->conn_handle, keepalive, false);
            } else {
                _platform->notify(peer->conn_handle, keepalive);
            }
        }
    }
}

void BLEInterface::performMaintenance() {
    std::lock_guard<std::mutex> lock(_mutex);

    // Check reassembly timeouts
    _reassembler.checkTimeouts();

    // Check handshake timeouts
    _identity_manager.checkTimeouts();

    // Check blacklist expirations
    _peer_manager.checkBlacklistExpirations();

    // Recalculate peer scores
    _peer_manager.recalculateScores();

    // Clean up stale peers
    _peer_manager.cleanupStalePeers();

    // Process discovered peers (try to connect)
    processDiscoveredPeers();
}

void BLEInterface::handleIncomingData(const ConnectionHandle& conn, const Bytes& data) {
    std::lock_guard<std::mutex> lock(_mutex);

    Bytes mac = conn.peer_address.toBytes();

    // Determine our role
    bool is_central = (conn.local_role == Role::CENTRAL);

    // First check if this is an identity handshake
    if (_identity_manager.processReceivedData(mac, data, is_central)) {
        // Was a handshake - consumed
        return;
    }

    // Check for keepalive (1 byte, value 0x00)
    if (data.size() == 1 && data.data()[0] == 0x00) {
        _peer_manager.updateLastActivity(_identity_manager.getIdentityForMac(mac));
        return;
    }

    // Regular data - pass to reassembler
    Bytes identity = _identity_manager.getIdentityForMac(mac);
    if (identity.size() == 0) {
        WARNING("BLEInterface: Received data from peer without identity");
        return;
    }

    _reassembler.processFragment(identity, data);
}

void BLEInterface::initiateHandshake(const ConnectionHandle& conn) {
    Bytes mac = conn.peer_address.toBytes();

    // Get handshake data (our identity)
    Bytes handshake = _identity_manager.initiateHandshake(mac);

    if (handshake.size() > 0) {
        // Write our identity to peer's RX characteristic
        _platform->write(conn.handle, handshake, true);

        DEBUG("BLEInterface: Sent identity handshake to " + conn.peer_address.toString());
    }
}
