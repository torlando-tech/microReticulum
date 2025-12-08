/**
 * @file NimBLEPlatform.cpp
 * @brief NimBLE-Arduino implementation for ESP32
 */

#include "NimBLEPlatform.h"

#if defined(ESP32) && (defined(USE_NIMBLE) || defined(CONFIG_BT_NIMBLE_ENABLED))

#include "../../Log.h"

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
// Central Mode - Scanning
//=============================================================================

bool NimBLEPlatform::startScan(uint16_t duration_ms) {
    if (!_scan) {
        ERROR("NimBLEPlatform: Scan not initialized");
        return false;
    }

    if (_scanning) {
        return true;
    }

    uint32_t duration_sec = (duration_ms == 0) ? 0 : (duration_ms / 1000);

    _scan->setActiveScan(_config.scan_mode == ScanMode::ACTIVE);
    _scan->setInterval(_config.scan_interval_ms);
    _scan->setWindow(_config.scan_window_ms);

    if (_scan->start(duration_sec, false)) {
        _scanning = true;
        DEBUG("NimBLEPlatform: Scan started, duration: " +
              (duration_ms == 0 ? "continuous" : std::to_string(duration_ms) + "ms"));
        return true;
    }

    ERROR("NimBLEPlatform: Failed to start scan");
    return false;
}

void NimBLEPlatform::stopScan() {
    if (_scan && _scanning) {
        _scan->stop();
        _scanning = false;
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

    // Create or get client
    NimBLEClient* client = NimBLEDevice::createClient(nimAddr);
    if (!client) {
        ERROR("NimBLEPlatform: Failed to create client");
        return false;
    }

    client->setClientCallbacks(this, false);
    client->setConnectTimeout(timeout_ms / 1000);

    DEBUG("NimBLEPlatform: Connecting to " + address.toString());

    if (!client->connect(nimAddr, true)) {
        ERROR("NimBLEPlatform: Connection failed to " + address.toString());
        NimBLEDevice::deleteClient(client);
        return false;
    }

    // Connection successful - setup tracked in onConnect callback
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

    DEBUG("NimBLEPlatform: Write received, " + std::to_string(data.size()) + " bytes");

    if (_on_write_received) {
        ConnectionHandle conn = getConnection(conn_handle);
        _on_write_received(conn, data);
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

    DEBUG("NimBLEPlatform: Connected to peripheral: " + peer_addr.toString());

    if (_on_connected) {
        _on_connected(conn);
    }
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
    _rx_char->setCallbacks(this);

    // Create TX characteristic (notify to central)
    _tx_char = _service->createCharacteristic(
        UUID::TX_CHAR,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
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

    _advertising_obj->addServiceUUID(UUID::SERVICE);
    _advertising_obj->setMinInterval(_config.adv_interval_min_ms * 1000 / 625);  // Convert to 0.625ms units
    _advertising_obj->setMaxInterval(_config.adv_interval_max_ms * 1000 / 625);

    // NimBLE 2.x: Set scan response data with device name
    NimBLEAdvertisementData scanResponseData;
    scanResponseData.setName(_config.device_name);
    _advertising_obj->setScanResponseData(scanResponseData);

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

    return true;
}

BLEAddress NimBLEPlatform::fromNimBLE(const NimBLEAddress& addr) {
    BLEAddress result;
    const ble_addr_t* base = addr.getBase();
    if (base) {
        // NimBLE stores in reverse order
        for (int i = 0; i < 6; i++) {
            result.addr[i] = base->val[5 - i];
        }
    }
    result.type = addr.getType();
    return result;
}

NimBLEAddress NimBLEPlatform::toNimBLE(const BLEAddress& addr) {
    uint8_t reversed[6];
    for (int i = 0; i < 6; i++) {
        reversed[i] = addr.addr[5 - i];
    }
    return NimBLEAddress(reversed, addr.type);
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
