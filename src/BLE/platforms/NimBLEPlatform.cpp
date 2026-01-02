/**
 * @file NimBLEPlatform.cpp
 * @brief NimBLE-Arduino implementation for ESP32
 */

#include "NimBLEPlatform.h"

#if defined(ESP32) && (defined(USE_NIMBLE) || defined(CONFIG_BT_NIMBLE_ENABLED))

#include "../../Log.h"

// WiFi coexistence: Check if WiFi is available and connected
// This is used to add extra delays before BLE connection attempts
#if __has_include(<WiFi.h>)
    #include <WiFi.h>
    #define HAS_WIFI_COEX 1
#else
    #define HAS_WIFI_COEX 0
#endif

// NimBLE low-level GAP functions for checking stack state and native connections
extern "C" {
    #include "nimble/nimble/host/include/host/ble_gap.h"
    #include "nimble/nimble/host/include/host/ble_hs.h"

    int ble_gap_adv_active(void);
    int ble_gap_disc_active(void);
    int ble_gap_conn_active(void);
}

namespace RNS { namespace BLE {

NimBLEPlatform::NimBLEPlatform() {
}

NimBLEPlatform::~NimBLEPlatform() {
    shutdown();
}

//=============================================================================
// Lifecycle
//=============================================================================

bool NimBLEPlatform::initialize(const PlatformConfig& config) {
    if (_initialized) {
        WARNING("NimBLEPlatform: Already initialized");
        return true;
    }

    _config = config;

    // Initialize NimBLE
    NimBLEDevice::init(_config.device_name);

    // Address type for ESP32-S3:
    // - BLE_OWN_ADDR_PUBLIC fails with error 13 (ETIMEOUT) for client connections
    // - BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT also fails with error 13
    // - BLE_OWN_ADDR_RANDOM works for client connections
    // Using RANDOM address allows connections to work. Role negotiation is handled
    // by always initiating connections and using identity-based duplicate detection.
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

    // Set power level (ESP32)
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    // Set MTU
    NimBLEDevice::setMTU(_config.preferred_mtu);

    // Setup server (peripheral mode)
    if (_config.role == Role::PERIPHERAL || _config.role == Role::DUAL) {
        if (!setupServer()) {
            ERROR("NimBLEPlatform: Failed to setup server");
            return false;
        }
    }

    // Setup scan (central mode)
    if (_config.role == Role::CENTRAL || _config.role == Role::DUAL) {
        if (!setupScan()) {
            ERROR("NimBLEPlatform: Failed to setup scan");
            return false;
        }
    }

    _initialized = true;
    INFO("NimBLEPlatform: Initialized, role: " + std::string(roleToString(_config.role)));

    return true;
}

bool NimBLEPlatform::start() {
    if (!_initialized) {
        ERROR("NimBLEPlatform: Not initialized");
        return false;
    }

    if (_running) {
        return true;
    }

    // Start advertising if peripheral mode
    if (_config.role == Role::PERIPHERAL || _config.role == Role::DUAL) {
        if (!startAdvertising()) {
            WARNING("NimBLEPlatform: Failed to start advertising");
        }
    }

    _running = true;
    INFO("NimBLEPlatform: Started");

    return true;
}

void NimBLEPlatform::stop() {
    if (!_running) {
        return;
    }

    stopScan();
    stopAdvertising();
    disconnectAll();

    _running = false;
    INFO("NimBLEPlatform: Stopped");
}

void NimBLEPlatform::loop() {
    if (!_running) {
        return;
    }

    // Check if continuous scan should stop
    if (_scanning && _scan_stop_time > 0 && millis() >= _scan_stop_time) {
        DEBUG("NimBLEPlatform: Stopping scan after timeout");
        stopScan();

        // Restart advertising if in peripheral/dual mode
        if (_config.role == Role::PERIPHERAL || _config.role == Role::DUAL) {
            startAdvertising();
        }

        if (_on_scan_complete) {
            _on_scan_complete();
        }
    }

    // Process operation queue
    BLEOperationQueue::process();
}

void NimBLEPlatform::shutdown() {
    stop();

    // Cleanup clients
    for (auto& kv : _clients) {
        if (kv.second) {
            NimBLEDevice::deleteClient(kv.second);
        }
    }
    _clients.clear();
    _connections.clear();
    _discovered_devices.clear();

    // Deinit NimBLE
    if (_initialized) {
        NimBLEDevice::deinit(true);
        _initialized = false;
    }

    _server = nullptr;
    _service = nullptr;
    _rx_char = nullptr;
    _tx_char = nullptr;
    _identity_char = nullptr;
    _scan = nullptr;
    _advertising_obj = nullptr;

    INFO("NimBLEPlatform: Shutdown complete");
}

bool NimBLEPlatform::isRunning() const {
    return _running;
}

//=============================================================================
// BLE Stack Recovery
//=============================================================================

bool NimBLEPlatform::recoverBLEStack() {
    // Track consecutive recovery attempts using existing member variable
    _lightweight_reset_fails++;
    WARNING("NimBLEPlatform: Performing soft BLE reset (attempt " +
            std::to_string(_lightweight_reset_fails) + ")...");

    // If we've had too many consecutive recovery attempts without success,
    // the BLE stack is truly stuck. Reboot is the only reliable fix.
    if (_lightweight_reset_fails >= 5) {
        ERROR("NimBLEPlatform: BLE stack unrecoverable after " +
              std::to_string(_lightweight_reset_fails) + " attempts - rebooting device");
        delay(100);
        ESP.restart();
        return false;  // Won't reach here
    }

    // Stop all operations
    _scanning = false;
    _scan_stop_time = 0;
    _advertising = false;

    // Stop scan via high-level API
    if (_scan) {
        _scan->stop();
    }

    // Stop advertising
    if (_advertising_obj) {
        _advertising_obj->stop();
    }

    // Wait for all operations to settle
    delay(500);

    // Clear scan results and discovered devices
    if (_scan) {
        _scan->clearResults();
    }
    _discovered_devices.clear();

    // Reconfigure scan - this can help recover from bad state
    if (_scan) {
        _scan->setScanCallbacks(this, false);
        _scan->setActiveScan(_config.scan_mode == ScanMode::ACTIVE);
        _scan->setInterval(_config.scan_interval_ms);
        _scan->setWindow(_config.scan_window_ms);
        _scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
        _scan->setDuplicateFilter(true);
    }

    // Reset scan failure counter (but NOT lightweight_reset_fails - that tracks recovery success)
    _scan_fail_count = 0;

    // Restart advertising if in peripheral/dual mode
    if (_config.role == Role::PERIPHERAL || _config.role == Role::DUAL) {
        delay(100);
        startAdvertising();
    }

    INFO("NimBLEPlatform: Soft reset complete");
    return true;
}

//=============================================================================
// Central Mode - Scanning
//=============================================================================

bool NimBLEPlatform::startScan(uint16_t duration_ms) {
    if (!_scan) {
        ERROR("NimBLEPlatform: Scan not initialized");
        return false;
    }

    if (_scanning) {
        _scan_fail_count = 0;  // Reset on successful state
        return true;
    }

    // Check if BLE stack is busy with other operations (e.g., incoming peripheral connection)
    // This prevents conflicts between central and peripheral operations
    if (ble_gap_conn_active()) {
        DEBUG("NimBLEPlatform: Skipping scan - connection in progress");
        return false;  // Don't count as failure, just skip this cycle
    }

    // Stop advertising while scanning to avoid conflicts on ESP32
    bool was_advertising = isAdvertising();
    if (was_advertising) {
        stopAdvertising();
        // Give BLE stack time to complete advertising stop
        delay(50);
    }

    uint32_t duration_sec = (duration_ms == 0) ? 0 : (duration_ms / 1000);
    if (duration_sec < 1) duration_sec = 1;  // Minimum 1 second

    // Clear results and reconfigure scan before starting
    _scan->clearResults();
    _scan->setActiveScan(_config.scan_mode == ScanMode::ACTIVE);
    _scan->setInterval(_config.scan_interval_ms);
    _scan->setWindow(_config.scan_window_ms);

    DEBUG("NimBLEPlatform: Starting scan with duration=" + std::to_string(duration_sec) + "s" +
          " nimble_isScanning=" + std::string(_scan->isScanning() ? "true" : "false") +
          " was_advertising=" + std::string(was_advertising ? "yes" : "no"));

    // NimBLE 2.x: use 0 for continuous scanning (we'll stop it manually in loop())
    // The duration parameter in seconds doesn't work reliably on ESP32-S3
    bool started = _scan->start(0, false);
    DEBUG("NimBLEPlatform: Scan start returned " + std::string(started ? "true" : "false") +
          " nimble_isScanning=" + std::string(_scan->isScanning() ? "true" : "false"));

    if (started) {
        _scanning = true;
        _scan_fail_count = 0;  // Reset failure counter on success
        _lightweight_reset_fails = 0;  // Reset recovery counter - BLE is working again
        _scan_stop_time = millis() + duration_ms;  // Schedule when to stop
        DEBUG("NimBLEPlatform: Scan started, will stop at " + std::to_string(_scan_stop_time) +
              " (in " + std::to_string(duration_ms) + "ms)");
        return true;
    }

    // Scan failed - track failures and attempt recovery if needed
    _scan_fail_count++;
    ERROR("NimBLEPlatform: Failed to start scan (attempt " + std::to_string(_scan_fail_count) + ")");

    if (_scan_fail_count >= SCAN_FAIL_RECOVERY_THRESHOLD) {
        WARNING("NimBLEPlatform: Too many scan failures, attempting BLE stack recovery");
        if (recoverBLEStack()) {
            INFO("NimBLEPlatform: BLE stack recovered, retrying scan");
            // Try scan one more time after recovery
            _scan->clearResults();
            started = _scan->start(0, false);
            if (started) {
                _scanning = true;
                _scan_fail_count = 0;
                _scan_stop_time = millis() + duration_ms;
                INFO("NimBLEPlatform: Scan started after recovery");
                return true;
            }
        }
    }

    return false;
}

void NimBLEPlatform::stopScan() {
    if (_scan && _scanning) {
        DEBUG("NimBLEPlatform: stopScan() called - stopping scan");
        _scan->stop();
        _scanning = false;
        _scan_stop_time = 0;  // Clear the timer
        DEBUG("NimBLEPlatform: Scan stopped");
    }
}

bool NimBLEPlatform::isScanning() const {
    return _scanning;
}

//=============================================================================
// Central Mode - Connections
//=============================================================================

bool NimBLEPlatform::connect(const BLEAddress& address, uint16_t timeout_ms) {
    NimBLEAddress nimAddr = toNimBLE(address);

    // Rate limit connections to avoid overwhelming the BLE stack
    static unsigned long last_connect_time = 0;
    unsigned long now = millis();
    if (now - last_connect_time < 500) {
        DEBUG("NimBLEPlatform: Connection rate limited, waiting");
        delay(500 - (now - last_connect_time));
    }
    last_connect_time = millis();

    // Check if already connected
    if (isConnectedTo(address)) {
        WARNING("NimBLEPlatform: Already connected to " + address.toString());
        return false;
    }

    // Check connection limit
    if (getConnectionCount() >= _config.max_connections) {
        WARNING("NimBLEPlatform: Connection limit reached");
        return false;
    }

    // Stop scanning and advertising before connecting
    bool was_advertising = isAdvertising();

    // Use high-level NimBLE API to stop scan (low-level ble_gap_disc_cancel can cause timer assertions)
    if (_scan && (_scan->isScanning() || ble_gap_disc_active())) {
        DEBUG("NimBLEPlatform: Stopping scan before connect");
        _scan->stop();
        delay(50);  // Give stack time to process
        _scanning = false;
        _scan_stop_time = 0;
    }

    // Cancel advertising at low level too
    if (ble_gap_adv_active()) {
        DEBUG("NimBLEPlatform: Stopping advertising via ble_gap_adv_stop");
        ble_gap_adv_stop();
    }

    // Stop high-level advertising tracking
    if (_advertising_obj && _advertising_obj->isAdvertising()) {
        DEBUG("NimBLEPlatform: Stopping advertising before connect");
        _advertising_obj->stop();
    }

    // Wait for GAP operations to fully complete
    int wait_count = 0;
    while ((ble_gap_disc_active() || ble_gap_adv_active()) && wait_count < 50) {
        delay(10);
        wait_count++;
    }

    // Give BLE stack extra time to settle - ESP32-S3 may need more time
    delay(300);

    DEBUG("NimBLEPlatform: Post-delay GAP state: adv=" + std::to_string(ble_gap_adv_active()) +
          " disc=" + std::to_string(ble_gap_disc_active()) +
          " conn=" + std::to_string(ble_gap_conn_active()));

    // Mark scan as inactive
    _scanning = false;

    // Check if there's still a pending connection in the controller
    bool conn_pending = ble_gap_conn_active();
    if (conn_pending) {
        WARNING("NimBLEPlatform: Connection still pending in GAP, waiting...");
        int wait_count = 0;
        while (ble_gap_conn_active() && wait_count < 100) {
            delay(10);
            wait_count++;
        }
        if (ble_gap_conn_active()) {
            ERROR("NimBLEPlatform: GAP connection still active after timeout");
            if (was_advertising) startAdvertising();
            return false;
        }
    }

    // Delete any existing clients for this address to ensure clean state
    NimBLEClient* existingClient = NimBLEDevice::getClientByPeerAddress(nimAddr);
    while (existingClient) {
        DEBUG("NimBLEPlatform: Deleting existing client for " + address.toString() +
              " connected=" + std::to_string(existingClient->isConnected()));
        if (existingClient->isConnected()) {
            existingClient->disconnect();
        }
        NimBLEDevice::deleteClient(existingClient);
        existingClient = NimBLEDevice::getClientByPeerAddress(nimAddr);
    }

    // Clean up any disconnected clients to free up slots
    int clientCount = NimBLEDevice::getCreatedClientCount();
    DEBUG("NimBLEPlatform: Current client count before cleanup: " + std::to_string(clientCount));

    DEBUG("NimBLEPlatform: Connecting to " + address.toString() +
          " timeout=" + std::to_string(timeout_ms / 1000) + "s" +
          " addrType=" + std::to_string(nimAddr.getType()));

    // Use native NimBLE connection instead of NimBLE-Arduino wrapper
    // This bypasses potential issues in NimBLEClient::connect() on ESP32-S3
    DEBUG("NimBLEPlatform: Using native ble_gap_connect()");
    bool connected = connectNative(address, timeout_ms);

    if (!connected) {
        ERROR("NimBLEPlatform: Native connection failed to " + address.toString());
        if (was_advertising) startAdvertising();
        return false;
    }

    // Remove from discovered devices cache
    std::string addrKey = nimAddr.toString().c_str();
    auto cachedIt = _discovered_devices.find(addrKey);
    if (cachedIt != _discovered_devices.end()) {
        _discovered_devices.erase(cachedIt);
        DEBUG("NimBLEPlatform: Removed connected device from cache");
    }

    DEBUG("NimBLEPlatform: Connection established successfully");
    return true;
}

//=============================================================================
// Native NimBLE Connection (bypasses NimBLE-Arduino wrapper)
//=============================================================================

int NimBLEPlatform::nativeGapEventHandler(struct ble_gap_event* event, void* arg) {
    NimBLEPlatform* platform = static_cast<NimBLEPlatform*>(arg);

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            DEBUG("NimBLEPlatform::nativeGapEventHandler: BLE_GAP_EVENT_CONNECT status=" +
                  std::to_string(event->connect.status) +
                  " handle=" + std::to_string(event->connect.conn_handle));

            platform->_native_connect_result = event->connect.status;
            if (event->connect.status == 0) {
                platform->_native_connect_success = true;
                platform->_native_connect_handle = event->connect.conn_handle;
            } else {
                platform->_native_connect_success = false;
            }
            platform->_native_connect_pending = false;
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            uint16_t disc_handle = event->disconnect.conn.conn_handle;
            int disc_reason = event->disconnect.reason;

            DEBUG("NimBLEPlatform::nativeGapEventHandler: BLE_GAP_EVENT_DISCONNECT reason=" +
                  std::to_string(disc_reason) +
                  " handle=" + std::to_string(disc_handle));

            // If we were still waiting for connection, this is a failure
            if (platform->_native_connect_pending) {
                platform->_native_connect_result = disc_reason;
                platform->_native_connect_success = false;
                platform->_native_connect_pending = false;
            }

            // Clean up established connections (handles MAC rotation, out of range, etc.)
            auto conn_it = platform->_connections.find(disc_handle);
            if (conn_it != platform->_connections.end()) {
                ConnectionHandle conn = conn_it->second;
                platform->_connections.erase(conn_it);

                INFO("NimBLEPlatform: Native connection lost to " + conn.peer_address.toString() +
                     " reason=" + std::to_string(disc_reason));

                // Clean up client object
                auto client_it = platform->_clients.find(disc_handle);
                if (client_it != platform->_clients.end()) {
                    if (client_it->second) {
                        NimBLEDevice::deleteClient(client_it->second);
                    }
                    platform->_clients.erase(client_it);
                }

                // Clear operation queue for this connection
                platform->clearForConnection(disc_handle);

                // Notify higher layers
                if (platform->_on_disconnected) {
                    platform->_on_disconnected(conn, static_cast<uint8_t>(disc_reason));
                }

                // Restart advertising if in peripheral/dual mode
                if ((platform->_config.role == Role::PERIPHERAL || platform->_config.role == Role::DUAL) &&
                    !platform->_advertising) {
                    platform->startAdvertising();
                }
            }
            break;
        }

        default:
            DEBUG("NimBLEPlatform::nativeGapEventHandler: event type=" + std::to_string(event->type));
            break;
    }

    return 0;
}

bool NimBLEPlatform::connectNative(const BLEAddress& address, uint16_t timeout_ms) {
    DEBUG("NimBLEPlatform::connectNative: Starting native connection to " + address.toString());

    // Build the peer address structure
    ble_addr_t peer_addr;
    peer_addr.type = address.type;
    // NimBLE stores addresses in little-endian: val[0]=LSB, val[5]=MSB
    // Our BLEAddress stores in big-endian display order: addr[0]=MSB, addr[5]=LSB
    for (int i = 0; i < 6; i++) {
        peer_addr.val[i] = address.addr[5 - i];
    }

    DEBUG("NimBLEPlatform::connectNative: peer_addr type=" + std::to_string(peer_addr.type) +
          " val=" + std::to_string(peer_addr.val[5]) + ":" +
          std::to_string(peer_addr.val[4]) + ":" +
          std::to_string(peer_addr.val[3]) + ":" +
          std::to_string(peer_addr.val[2]) + ":" +
          std::to_string(peer_addr.val[1]) + ":" +
          std::to_string(peer_addr.val[0]));

    // Connection parameters - use reasonable defaults
    struct ble_gap_conn_params conn_params;
    memset(&conn_params, 0, sizeof(conn_params));
    conn_params.scan_itvl = 16;           // 10ms in 0.625ms units
    conn_params.scan_window = 16;         // 10ms in 0.625ms units
    conn_params.itvl_min = 24;            // 30ms in 1.25ms units
    conn_params.itvl_max = 40;            // 50ms in 1.25ms units
    conn_params.latency = 0;
    conn_params.supervision_timeout = 256; // 2560ms in 10ms units
    conn_params.min_ce_len = 0;
    conn_params.max_ce_len = 0;

    // Reset connection state
    _native_connect_pending = true;
    _native_connect_success = false;
    _native_connect_result = 0;
    _native_connect_handle = 0;
    _native_connect_address = address;

    // Use RANDOM own address type (same as what NimBLEDevice is configured with)
    uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;

    DEBUG("NimBLEPlatform::connectNative: Trying with own_addr_type=" + std::to_string(own_addr_type));

    int rc = ble_gap_connect(own_addr_type,
                              &peer_addr,
                              timeout_ms,
                              &conn_params,
                              nativeGapEventHandler,
                              this);

    DEBUG("NimBLEPlatform::connectNative: ble_gap_connect returned " + std::to_string(rc));

    if (rc != 0) {
        ERROR("NimBLEPlatform::connectNative: ble_gap_connect failed with rc=" + std::to_string(rc));
        _native_connect_pending = false;
        return false;
    }

    // Wait for connection to complete
    unsigned long start = millis();
    while (_native_connect_pending && (millis() - start) < timeout_ms) {
        delay(10);
    }

    if (_native_connect_pending) {
        // Timeout - try to cancel but only if connection is still pending at GAP level
        WARNING("NimBLEPlatform::connectNative: Connection timed out, cancelling");
        // Check if we're actually connecting before cancelling
        if (ble_gap_conn_active()) {
            int rc = ble_gap_conn_cancel();
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                DEBUG("NimBLEPlatform::connectNative: ble_gap_conn_cancel returned " + std::to_string(rc));
            }
        }
        delay(50);  // Give stack time to process
        _native_connect_pending = false;
        return false;
    }

    if (!_native_connect_success) {
        ERROR("NimBLEPlatform::connectNative: Connection failed with result=" +
              std::to_string(_native_connect_result));
        return false;
    }

    // Copy volatile to local variable
    uint16_t conn_handle = _native_connect_handle;

    INFO("NimBLEPlatform::connectNative: Connection succeeded! handle=" +
         std::to_string(conn_handle));

    // Now create an NimBLEClient for this connection to use for GATT operations
    // The connection already exists, so we just need to wrap it
    NimBLEClient* client = NimBLEDevice::getClientByHandle(conn_handle);
    if (!client) {
        // Create a new client and associate it with this connection
        NimBLEAddress nimAddr(peer_addr);
        client = NimBLEDevice::createClient(nimAddr);
        if (client) {
            client->setClientCallbacks(this, false);
        }
    }

    if (client) {
        // Track the connection
        ConnectionHandle conn;
        conn.handle = conn_handle;
        conn.peer_address = address;
        conn.local_role = Role::CENTRAL;
        conn.state = ConnectionState::CONNECTED;
        conn.mtu = client->getMTU();

        _connections[conn_handle] = conn;
        _clients[conn_handle] = client;

        if (_on_connected) {
            _on_connected(conn);
        }
    }

    return true;
}

bool NimBLEPlatform::disconnect(uint16_t conn_handle) {
    auto conn_it = _connections.find(conn_handle);
    if (conn_it == _connections.end()) {
        return false;
    }

    ConnectionHandle& conn = conn_it->second;

    if (conn.local_role == Role::CENTRAL) {
        // We are central - disconnect client
        auto client_it = _clients.find(conn_handle);
        if (client_it != _clients.end() && client_it->second) {
            client_it->second->disconnect();
            return true;
        }
    } else {
        // We are peripheral - disconnect via server
        if (_server) {
            _server->disconnect(conn_handle);
            return true;
        }
    }

    return false;
}

void NimBLEPlatform::disconnectAll() {
    // Disconnect all clients (central mode)
    for (auto& kv : _clients) {
        if (kv.second && kv.second->isConnected()) {
            kv.second->disconnect();
        }
    }

    // Disconnect all server connections (peripheral mode)
    if (_server) {
        std::vector<uint16_t> handles;
        for (const auto& kv : _connections) {
            if (kv.second.local_role == Role::PERIPHERAL) {
                handles.push_back(kv.first);
            }
        }
        for (uint16_t handle : handles) {
            _server->disconnect(handle);
        }
    }
}

bool NimBLEPlatform::requestMTU(uint16_t conn_handle, uint16_t mtu) {
    auto client_it = _clients.find(conn_handle);
    if (client_it == _clients.end() || !client_it->second) {
        return false;
    }

    // NimBLE handles MTU exchange automatically, but we can try to update
    // The MTU change callback will be invoked
    return true;
}

bool NimBLEPlatform::discoverServices(uint16_t conn_handle) {
    auto client_it = _clients.find(conn_handle);
    if (client_it == _clients.end() || !client_it->second) {
        return false;
    }

    NimBLEClient* client = client_it->second;

    // Get our service
    NimBLERemoteService* service = client->getService(UUID::SERVICE);
    if (!service) {
        ERROR("NimBLEPlatform: Service not found");
        if (_on_services_discovered) {
            ConnectionHandle conn = getConnection(conn_handle);
            _on_services_discovered(conn, false);
        }
        return false;
    }

    // Get characteristics
    NimBLERemoteCharacteristic* rxChar = service->getCharacteristic(UUID::RX_CHAR);
    NimBLERemoteCharacteristic* txChar = service->getCharacteristic(UUID::TX_CHAR);
    NimBLERemoteCharacteristic* idChar = service->getCharacteristic(UUID::IDENTITY_CHAR);

    if (!rxChar || !txChar) {
        ERROR("NimBLEPlatform: Required characteristics not found");
        if (_on_services_discovered) {
            ConnectionHandle conn = getConnection(conn_handle);
            _on_services_discovered(conn, false);
        }
        return false;
    }

    // Update connection with characteristic handles
    auto conn_it = _connections.find(conn_handle);
    if (conn_it != _connections.end()) {
        conn_it->second.rx_char_handle = rxChar->getHandle();
        conn_it->second.tx_char_handle = txChar->getHandle();
        if (idChar) {
            conn_it->second.identity_handle = idChar->getHandle();
        }
        conn_it->second.state = ConnectionState::READY;
    }

    DEBUG("NimBLEPlatform: Services discovered for " + std::to_string(conn_handle));

    if (_on_services_discovered) {
        ConnectionHandle conn = getConnection(conn_handle);
        _on_services_discovered(conn, true);
    }

    return true;
}

//=============================================================================
// Peripheral Mode
//=============================================================================

bool NimBLEPlatform::startAdvertising() {
    if (!_advertising_obj) {
        if (!setupAdvertising()) {
            return false;
        }
    }

    if (_advertising) {
        return true;
    }

    if (_advertising_obj->start()) {
        _advertising = true;
        DEBUG("NimBLEPlatform: Advertising started");
        return true;
    }

    ERROR("NimBLEPlatform: Failed to start advertising");
    return false;
}

void NimBLEPlatform::stopAdvertising() {
    if (_advertising_obj && _advertising) {
        _advertising_obj->stop();
        _advertising = false;
        DEBUG("NimBLEPlatform: Advertising stopped");
    }
}

bool NimBLEPlatform::isAdvertising() const {
    return _advertising;
}

bool NimBLEPlatform::setAdvertisingData(const Bytes& data) {
    // Custom advertising data not directly supported by high-level API
    // Use the service UUID instead
    return true;
}

void NimBLEPlatform::setIdentityData(const Bytes& identity) {
    _identity_data = identity;

    if (_identity_char && identity.size() > 0) {
        _identity_char->setValue(identity.data(), identity.size());
        DEBUG("NimBLEPlatform: Identity data set");
    }
}

//=============================================================================
// GATT Operations
//=============================================================================

bool NimBLEPlatform::write(uint16_t conn_handle, const Bytes& data, bool response) {
    auto conn_it = _connections.find(conn_handle);
    if (conn_it == _connections.end()) {
        return false;
    }

    ConnectionHandle& conn = conn_it->second;

    if (conn.local_role == Role::CENTRAL) {
        // We are central - write to peripheral's RX characteristic
        auto client_it = _clients.find(conn_handle);
        if (client_it == _clients.end() || !client_it->second) {
            return false;
        }

        NimBLEClient* client = client_it->second;
        NimBLERemoteService* service = client->getService(UUID::SERVICE);
        if (!service) return false;

        NimBLERemoteCharacteristic* rxChar = service->getCharacteristic(UUID::RX_CHAR);
        if (!rxChar) return false;

        return rxChar->writeValue(data.data(), data.size(), response);
    } else {
        // We are peripheral - this shouldn't be used, use notify instead
        WARNING("NimBLEPlatform: write() called in peripheral mode, use notify()");
        return false;
    }
}

bool NimBLEPlatform::read(uint16_t conn_handle, uint16_t char_handle,
                          std::function<void(OperationResult, const Bytes&)> callback) {
    auto client_it = _clients.find(conn_handle);
    if (client_it == _clients.end() || !client_it->second) {
        if (callback) callback(OperationResult::NOT_FOUND, Bytes());
        return false;
    }

    NimBLEClient* client = client_it->second;
    NimBLERemoteService* service = client->getService(UUID::SERVICE);
    if (!service) {
        if (callback) callback(OperationResult::NOT_FOUND, Bytes());
        return false;
    }

    // Find characteristic by handle
    NimBLERemoteCharacteristic* chr = nullptr;
    if (char_handle == _connections[conn_handle].identity_handle) {
        chr = service->getCharacteristic(UUID::IDENTITY_CHAR);
    }

    if (!chr) {
        if (callback) callback(OperationResult::NOT_FOUND, Bytes());
        return false;
    }

    NimBLEAttValue value = chr->readValue();
    if (callback) {
        Bytes result(value.data(), value.size());
        callback(OperationResult::SUCCESS, result);
    }

    return true;
}

bool NimBLEPlatform::enableNotifications(uint16_t conn_handle, bool enable) {
    auto client_it = _clients.find(conn_handle);
    if (client_it == _clients.end() || !client_it->second) {
        return false;
    }

    NimBLEClient* client = client_it->second;
    NimBLERemoteService* service = client->getService(UUID::SERVICE);
    if (!service) return false;

    NimBLERemoteCharacteristic* txChar = service->getCharacteristic(UUID::TX_CHAR);
    if (!txChar) return false;

    if (enable) {
        // Subscribe to notifications
        auto notifyCb = [this, conn_handle](NimBLERemoteCharacteristic* pChar,
                                             uint8_t* pData, size_t length, bool isNotify) {
            if (_on_data_received) {
                ConnectionHandle conn = getConnection(conn_handle);
                Bytes data(pData, length);
                _on_data_received(conn, data);
            }
        };

        return txChar->subscribe(true, notifyCb);
    } else {
        return txChar->unsubscribe();
    }
}

bool NimBLEPlatform::notify(uint16_t conn_handle, const Bytes& data) {
    if (!_tx_char) {
        return false;
    }

    _tx_char->setValue(data.data(), data.size());
    return _tx_char->notify(true);
}

bool NimBLEPlatform::notifyAll(const Bytes& data) {
    if (!_tx_char) {
        return false;
    }

    _tx_char->setValue(data.data(), data.size());
    return _tx_char->notify(true);  // Notifies all subscribed clients
}

//=============================================================================
// Connection Management
//=============================================================================

std::vector<ConnectionHandle> NimBLEPlatform::getConnections() const {
    std::vector<ConnectionHandle> result;
    for (const auto& kv : _connections) {
        result.push_back(kv.second);
    }
    return result;
}

ConnectionHandle NimBLEPlatform::getConnection(uint16_t handle) const {
    auto it = _connections.find(handle);
    if (it != _connections.end()) {
        return it->second;
    }
    return ConnectionHandle();
}

size_t NimBLEPlatform::getConnectionCount() const {
    return _connections.size();
}

bool NimBLEPlatform::isConnectedTo(const BLEAddress& address) const {
    for (const auto& kv : _connections) {
        if (kv.second.peer_address == address) {
            return true;
        }
    }
    return false;
}

//=============================================================================
// Callback Registration
//=============================================================================

void NimBLEPlatform::setOnScanResult(Callbacks::OnScanResult callback) {
    _on_scan_result = callback;
}

void NimBLEPlatform::setOnScanComplete(Callbacks::OnScanComplete callback) {
    _on_scan_complete = callback;
}

void NimBLEPlatform::setOnConnected(Callbacks::OnConnected callback) {
    _on_connected = callback;
}

void NimBLEPlatform::setOnDisconnected(Callbacks::OnDisconnected callback) {
    _on_disconnected = callback;
}

void NimBLEPlatform::setOnMTUChanged(Callbacks::OnMTUChanged callback) {
    _on_mtu_changed = callback;
}

void NimBLEPlatform::setOnServicesDiscovered(Callbacks::OnServicesDiscovered callback) {
    _on_services_discovered = callback;
}

void NimBLEPlatform::setOnDataReceived(Callbacks::OnDataReceived callback) {
    _on_data_received = callback;
}

void NimBLEPlatform::setOnNotifyEnabled(Callbacks::OnNotifyEnabled callback) {
    _on_notify_enabled = callback;
}

void NimBLEPlatform::setOnCentralConnected(Callbacks::OnCentralConnected callback) {
    _on_central_connected = callback;
}

void NimBLEPlatform::setOnCentralDisconnected(Callbacks::OnCentralDisconnected callback) {
    _on_central_disconnected = callback;
}

void NimBLEPlatform::setOnWriteReceived(Callbacks::OnWriteReceived callback) {
    _on_write_received = callback;
}

void NimBLEPlatform::setOnReadRequested(Callbacks::OnReadRequested callback) {
    _on_read_requested = callback;
}

BLEAddress NimBLEPlatform::getLocalAddress() const {
    return fromNimBLE(NimBLEDevice::getAddress());
}

//=============================================================================
// NimBLE Server Callbacks (Peripheral mode)
//=============================================================================

void NimBLEPlatform::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    uint16_t conn_handle = connInfo.getConnHandle();

    ConnectionHandle conn;
    conn.handle = conn_handle;
    conn.peer_address = fromNimBLE(connInfo.getAddress());
    conn.local_role = Role::PERIPHERAL;  // We are peripheral, they are central
    conn.state = ConnectionState::CONNECTED;
    conn.mtu = MTU::MINIMUM;

    _connections[conn_handle] = conn;

    DEBUG("NimBLEPlatform: Central connected: " + conn.peer_address.toString());

    if (_on_central_connected) {
        _on_central_connected(conn);
    }

    // Continue advertising to accept more connections
    if (_config.role == Role::DUAL && getConnectionCount() < _config.max_connections) {
        startAdvertising();
    }
}

void NimBLEPlatform::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    uint16_t conn_handle = connInfo.getConnHandle();

    auto it = _connections.find(conn_handle);
    if (it != _connections.end()) {
        ConnectionHandle conn = it->second;
        _connections.erase(it);

        DEBUG("NimBLEPlatform: Central disconnected: " + conn.peer_address.toString() +
              " reason: " + std::to_string(reason));

        if (_on_central_disconnected) {
            _on_central_disconnected(conn);
        }
    }

    // Clear operation queue for this connection
    BLEOperationQueue::clearForConnection(conn_handle);
}

void NimBLEPlatform::onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
    uint16_t conn_handle = connInfo.getConnHandle();
    updateConnectionMTU(conn_handle, MTU);

    DEBUG("NimBLEPlatform: MTU changed to " + std::to_string(MTU) +
          " for connection " + std::to_string(conn_handle));

    if (_on_mtu_changed) {
        ConnectionHandle conn = getConnection(conn_handle);
        _on_mtu_changed(conn, MTU);
    }
}

//=============================================================================
// NimBLE Characteristic Callbacks
//=============================================================================

void NimBLEPlatform::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    uint16_t conn_handle = connInfo.getConnHandle();

    NimBLEAttValue value = pCharacteristic->getValue();
    Bytes data(value.data(), value.size());

    DEBUG("NimBLEPlatform::onWrite: Received " + std::to_string(data.size()) + " bytes from conn " + std::to_string(conn_handle));

    if (_on_write_received) {
        DEBUG("NimBLEPlatform::onWrite: Getting connection handle");
        ConnectionHandle conn = getConnection(conn_handle);
        DEBUG("NimBLEPlatform::onWrite: Calling callback, peer=" + conn.peer_address.toString());
        _on_write_received(conn, data);
        DEBUG("NimBLEPlatform::onWrite: Callback returned");
    } else {
        DEBUG("NimBLEPlatform::onWrite: No callback registered");
    }
}

void NimBLEPlatform::onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    // Identity characteristic read - return stored identity
    if (pCharacteristic == _identity_char && _identity_data.size() > 0) {
        pCharacteristic->setValue(_identity_data.data(), _identity_data.size());
    }
}

void NimBLEPlatform::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo,
                                  uint16_t subValue) {
    uint16_t conn_handle = connInfo.getConnHandle();
    bool enabled = (subValue > 0);

    DEBUG("NimBLEPlatform: Notifications " + std::string(enabled ? "enabled" : "disabled") +
          " for connection " + std::to_string(conn_handle));

    if (_on_notify_enabled) {
        ConnectionHandle conn = getConnection(conn_handle);
        _on_notify_enabled(conn, enabled);
    }
}

//=============================================================================
// NimBLE Client Callbacks (Central mode)
//=============================================================================

void NimBLEPlatform::onConnect(NimBLEClient* pClient) {
    uint16_t conn_handle = pClient->getConnHandle();
    BLEAddress peer_addr = fromNimBLE(pClient->getPeerAddress());

    ConnectionHandle conn;
    conn.handle = conn_handle;
    conn.peer_address = peer_addr;
    conn.local_role = Role::CENTRAL;  // We are central
    conn.state = ConnectionState::CONNECTED;
    conn.mtu = pClient->getMTU();

    _connections[conn_handle] = conn;
    _clients[conn_handle] = pClient;

    DEBUG("NimBLEPlatform: Connected to peripheral: " + peer_addr.toString() +
          " handle=" + std::to_string(conn_handle) + " mtu=" + std::to_string(conn.mtu));

    // Signal async connect completion
    _async_connect_pending = false;
    _async_connect_failed = false;

    if (_on_connected) {
        _on_connected(conn);
    }
}

void NimBLEPlatform::onConnectFail(NimBLEClient* pClient, int reason) {
    BLEAddress peer_addr = fromNimBLE(pClient->getPeerAddress());
    ERROR("NimBLEPlatform: onConnectFail to " + peer_addr.toString() +
          " reason=" + std::to_string(reason));

    // Signal async connect failure
    _async_connect_pending = false;
    _async_connect_failed = true;
    _async_connect_error = reason;
}

void NimBLEPlatform::onDisconnect(NimBLEClient* pClient, int reason) {
    uint16_t conn_handle = pClient->getConnHandle();

    auto it = _connections.find(conn_handle);
    if (it != _connections.end()) {
        ConnectionHandle conn = it->second;
        _connections.erase(it);

        DEBUG("NimBLEPlatform: Disconnected from peripheral: " + conn.peer_address.toString() +
              " reason: " + std::to_string(reason));

        if (_on_disconnected) {
            _on_disconnected(conn, static_cast<uint8_t>(reason));
        }
    }

    // Remove client
    _clients.erase(conn_handle);
    NimBLEDevice::deleteClient(pClient);

    // Clear operation queue
    BLEOperationQueue::clearForConnection(conn_handle);
}

//=============================================================================
// NimBLE Scan Callbacks
//=============================================================================

void NimBLEPlatform::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    // Check if device has our service UUID
    bool hasService = advertisedDevice->isAdvertisingService(BLEUUID(UUID::SERVICE));

    // Debug: log RNS device scan results with address type
    if (hasService) {
        DEBUG("NimBLEPlatform: RNS device found: " + std::string(advertisedDevice->getAddress().toString().c_str()) +
              " type=" + std::to_string(advertisedDevice->getAddress().getType()) +
              " RSSI=" + std::to_string(advertisedDevice->getRSSI()) +
              " name=" + advertisedDevice->getName());

        // Cache the full device info for later connection
        // Using string key since NimBLEAdvertisedDevice stores all connection metadata
        std::string addrKey = advertisedDevice->getAddress().toString().c_str();

        // Limit discovered device cache size to prevent memory growth
        // Remove oldest entry if at limit (simple FIFO approach)
        static constexpr size_t MAX_DISCOVERED_DEVICES = 16;
        while (_discovered_devices.size() >= MAX_DISCOVERED_DEVICES) {
            auto oldest = _discovered_devices.begin();
            _discovered_devices.erase(oldest);
        }

        _discovered_devices[addrKey] = *advertisedDevice;
        TRACE("NimBLEPlatform: Cached device for connection: " + addrKey +
              " (cache size: " + std::to_string(_discovered_devices.size()) + ")");
    }

    if (hasService && _on_scan_result) {
        ScanResult result;
        result.address = fromNimBLE(advertisedDevice->getAddress());
        result.name = advertisedDevice->getName();
        result.rssi = advertisedDevice->getRSSI();
        result.connectable = advertisedDevice->isConnectable();
        result.has_reticulum_service = true;

        _on_scan_result(result);
    }
}

void NimBLEPlatform::onScanEnd(const NimBLEScanResults& results, int reason) {
    bool was_scanning = _scanning;
    _scanning = false;
    _scan_stop_time = 0;

    DEBUG("NimBLEPlatform: onScanEnd callback, reason=" + std::to_string(reason) +
          " found=" + std::to_string(results.getCount()) + " devices" +
          " was_scanning=" + std::string(was_scanning ? "yes" : "no"));

    // Only process if we were actively scanning (not a spurious callback)
    if (!was_scanning) {
        return;
    }

    // Restart advertising after scan
    if (_config.role == Role::PERIPHERAL || _config.role == Role::DUAL) {
        startAdvertising();
    }

    if (_on_scan_complete) {
        _on_scan_complete();
    }
}

//=============================================================================
// BLEOperationQueue Implementation
//=============================================================================

bool NimBLEPlatform::executeOperation(const GATTOperation& op) {
    // Most operations are executed directly in NimBLE
    // This is a placeholder for more complex queued operations
    return true;
}

//=============================================================================
// Private Methods
//=============================================================================

bool NimBLEPlatform::setupServer() {
    _server = NimBLEDevice::createServer();
    if (!_server) {
        ERROR("NimBLEPlatform: Failed to create server");
        return false;
    }

    _server->setCallbacks(this);

    // Create Reticulum service
    _service = _server->createService(UUID::SERVICE);
    if (!_service) {
        ERROR("NimBLEPlatform: Failed to create service");
        return false;
    }

    // Create RX characteristic (write from central)
    _rx_char = _service->createCharacteristic(
        UUID::RX_CHAR,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _rx_char->setValue((uint8_t*)"\x00", 1);  // Initialize to 0x00
    _rx_char->setCallbacks(this);

    // Create TX characteristic (notify/indicate to central)
    // Note: indicate property required for compatibility with ble-reticulum/Columba
    _tx_char = _service->createCharacteristic(
        UUID::TX_CHAR,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
    );
    _tx_char->setValue((uint8_t*)"\x00", 1);  // Initialize to 0x00 (matches Columba)
    _tx_char->setCallbacks(this);

    // Create Identity characteristic (read only)
    _identity_char = _service->createCharacteristic(
        UUID::IDENTITY_CHAR,
        NIMBLE_PROPERTY::READ
    );
    _identity_char->setCallbacks(this);

    // Start service
    _service->start();

    return setupAdvertising();
}

bool NimBLEPlatform::setupAdvertising() {
    _advertising_obj = NimBLEDevice::getAdvertising();
    if (!_advertising_obj) {
        ERROR("NimBLEPlatform: Failed to get advertising");
        return false;
    }

    // CRITICAL: Reset advertising state before configuring
    // Without this, the advertising data may not be properly updated on ESP32-S3
    _advertising_obj->reset();

    _advertising_obj->setMinInterval(_config.adv_interval_min_ms * 1000 / 625);  // Convert to 0.625ms units
    _advertising_obj->setMaxInterval(_config.adv_interval_max_ms * 1000 / 625);

    // NimBLE 2.x: Use addServiceUUID to include service in advertising packet
    // The name goes in scan response automatically when enableScanResponse is used
    _advertising_obj->addServiceUUID(NimBLEUUID(UUID::SERVICE));
    _advertising_obj->setName(_config.device_name);

    DEBUG("NimBLEPlatform: Advertising configured with service UUID: " + std::string(UUID::SERVICE));

    return true;
}

bool NimBLEPlatform::setupScan() {
    _scan = NimBLEDevice::getScan();
    if (!_scan) {
        ERROR("NimBLEPlatform: Failed to get scan");
        return false;
    }

    _scan->setScanCallbacks(this, false);
    _scan->setActiveScan(_config.scan_mode == ScanMode::ACTIVE);
    _scan->setInterval(_config.scan_interval_ms);
    _scan->setWindow(_config.scan_window_ms);
    _scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    _scan->setDuplicateFilter(true);  // Filter duplicates within a scan window
    // Don't call setMaxResults - let NimBLE use defaults

    DEBUG("NimBLEPlatform: Scan configured - interval=" + std::to_string(_config.scan_interval_ms) +
          " window=" + std::to_string(_config.scan_window_ms));

    return true;
}

BLEAddress NimBLEPlatform::fromNimBLE(const NimBLEAddress& addr) {
    BLEAddress result;
    const ble_addr_t* base = addr.getBase();
    if (base) {
        // NimBLE stores addresses in little-endian: val[0]=LSB, val[5]=MSB
        // Our BLEAddress stores in big-endian display order: addr[0]=MSB, addr[5]=LSB
        // Need to reverse the byte order
        for (int i = 0; i < 6; i++) {
            result.addr[i] = base->val[5 - i];
        }
    }
    result.type = addr.getType();
    return result;
}

NimBLEAddress NimBLEPlatform::toNimBLE(const BLEAddress& addr) {
    // Use NimBLEAddress string constructor - it parses "XX:XX:XX:XX:XX:XX" format
    // and handles the byte order internally
    std::string addrStr = addr.toString();
    NimBLEAddress nimAddr(addrStr.c_str(), addr.type);
    DEBUG("NimBLEPlatform::toNimBLE: input=" + addrStr +
          " type=" + std::to_string(addr.type) +
          " -> nimAddr=" + std::string(nimAddr.toString().c_str()) +
          " nimType=" + std::to_string(nimAddr.getType()));
    return nimAddr;
}

NimBLEClient* NimBLEPlatform::findClient(uint16_t conn_handle) {
    auto it = _clients.find(conn_handle);
    return (it != _clients.end()) ? it->second : nullptr;
}

NimBLEClient* NimBLEPlatform::findClient(const BLEAddress& address) {
    for (const auto& kv : _clients) {
        if (kv.second && fromNimBLE(kv.second->getPeerAddress()) == address) {
            return kv.second;
        }
    }
    return nullptr;
}

uint16_t NimBLEPlatform::allocateConnHandle() {
    return _next_conn_handle++;
}

void NimBLEPlatform::freeConnHandle(uint16_t handle) {
    // No-op for simple allocator
}

void NimBLEPlatform::updateConnectionMTU(uint16_t conn_handle, uint16_t mtu) {
    auto it = _connections.find(conn_handle);
    if (it != _connections.end()) {
        it->second.mtu = mtu;
    }
}

}} // namespace RNS::BLE

#endif // ESP32 && USE_NIMBLE
