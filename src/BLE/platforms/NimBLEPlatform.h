/**
 * @file NimBLEPlatform.h
 * @brief NimBLE-Arduino implementation of IBLEPlatform for ESP32
 *
 * This implementation uses the NimBLE-Arduino library to provide BLE
 * functionality on ESP32 devices. It supports both central and peripheral
 * modes simultaneously (dual-mode operation).
 */
#pragma once

#include "../BLEPlatform.h"
#include "../BLEOperationQueue.h"

// Only compile for ESP32 with NimBLE
#if defined(ESP32) && (defined(USE_NIMBLE) || defined(CONFIG_BT_NIMBLE_ENABLED))

#include <NimBLEDevice.h>

// Undefine NimBLE's backward compatibility macros to avoid conflict with our types
#undef BLEAddress

#include <map>
#include <vector>

namespace RNS { namespace BLE {

/**
 * @brief NimBLE-Arduino implementation of IBLEPlatform
 */
class NimBLEPlatform : public IBLEPlatform,
                       public BLEOperationQueue,
                       public NimBLEServerCallbacks,
                       public NimBLECharacteristicCallbacks,
                       public NimBLEClientCallbacks,
                       public NimBLEScanCallbacks {
public:
    NimBLEPlatform();
    virtual ~NimBLEPlatform();

    //=========================================================================
    // IBLEPlatform Implementation
    //=========================================================================

    // Lifecycle
    bool initialize(const PlatformConfig& config) override;
    bool start() override;
    void stop() override;
    void loop() override;
    void shutdown() override;
    bool isRunning() const override;

    // Central mode - Scanning
    bool startScan(uint16_t duration_ms = 0) override;
    void stopScan() override;
    bool isScanning() const override;

    // Central mode - Connections
    bool connect(const BLEAddress& address, uint16_t timeout_ms = 10000) override;
    bool disconnect(uint16_t conn_handle) override;
    void disconnectAll() override;
    bool requestMTU(uint16_t conn_handle, uint16_t mtu) override;
    bool discoverServices(uint16_t conn_handle) override;

    // Peripheral mode
    bool startAdvertising() override;
    void stopAdvertising() override;
    bool isAdvertising() const override;
    bool setAdvertisingData(const Bytes& data) override;
    void setIdentityData(const Bytes& identity) override;

    // GATT Operations
    bool write(uint16_t conn_handle, const Bytes& data, bool response = true) override;
    bool read(uint16_t conn_handle, uint16_t char_handle,
              std::function<void(OperationResult, const Bytes&)> callback) override;
    bool enableNotifications(uint16_t conn_handle, bool enable) override;
    bool notify(uint16_t conn_handle, const Bytes& data) override;
    bool notifyAll(const Bytes& data) override;

    // Connection management
    std::vector<ConnectionHandle> getConnections() const override;
    ConnectionHandle getConnection(uint16_t handle) const override;
    size_t getConnectionCount() const override;
    bool isConnectedTo(const BLEAddress& address) const override;

    // Callback registration
    void setOnScanResult(Callbacks::OnScanResult callback) override;
    void setOnScanComplete(Callbacks::OnScanComplete callback) override;
    void setOnConnected(Callbacks::OnConnected callback) override;
    void setOnDisconnected(Callbacks::OnDisconnected callback) override;
    void setOnMTUChanged(Callbacks::OnMTUChanged callback) override;
    void setOnServicesDiscovered(Callbacks::OnServicesDiscovered callback) override;
    void setOnDataReceived(Callbacks::OnDataReceived callback) override;
    void setOnNotifyEnabled(Callbacks::OnNotifyEnabled callback) override;
    void setOnCentralConnected(Callbacks::OnCentralConnected callback) override;
    void setOnCentralDisconnected(Callbacks::OnCentralDisconnected callback) override;
    void setOnWriteReceived(Callbacks::OnWriteReceived callback) override;
    void setOnReadRequested(Callbacks::OnReadRequested callback) override;

    // Platform info
    PlatformType getPlatformType() const override { return PlatformType::NIMBLE_ARDUINO; }
    std::string getPlatformName() const override { return "NimBLE-Arduino"; }
    BLEAddress getLocalAddress() const override;

    //=========================================================================
    // NimBLEServerCallbacks (Peripheral mode)
    //=========================================================================

    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override;

    //=========================================================================
    // NimBLECharacteristicCallbacks
    //=========================================================================

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
    void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo,
                     uint16_t subValue) override;

    //=========================================================================
    // NimBLEClientCallbacks (Central mode)
    //=========================================================================

    void onConnect(NimBLEClient* pClient) override;
    void onDisconnect(NimBLEClient* pClient, int reason) override;

    //=========================================================================
    // NimBLEScanCallbacks (Scanning)
    //=========================================================================

    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override;

protected:
    // BLEOperationQueue implementation
    bool executeOperation(const GATTOperation& op) override;

private:
    // Setup methods
    bool setupServer();
    bool setupAdvertising();
    bool setupScan();

    // Address conversion
    static BLEAddress fromNimBLE(const NimBLEAddress& addr);
    static NimBLEAddress toNimBLE(const BLEAddress& addr);

    // Find client by connection handle or address
    NimBLEClient* findClient(uint16_t conn_handle);
    NimBLEClient* findClient(const BLEAddress& address);

    // Connection handle management
    uint16_t allocateConnHandle();
    void freeConnHandle(uint16_t handle);

    // Update connection info
    void updateConnectionMTU(uint16_t conn_handle, uint16_t mtu);

    // Configuration
    PlatformConfig _config;
    bool _initialized = false;
    bool _running = false;
    bool _scanning = false;
    bool _advertising = false;
    Bytes _identity_data;

    // NimBLE objects
    NimBLEServer* _server = nullptr;
    NimBLEService* _service = nullptr;
    NimBLECharacteristic* _rx_char = nullptr;
    NimBLECharacteristic* _tx_char = nullptr;
    NimBLECharacteristic* _identity_char = nullptr;
    NimBLEScan* _scan = nullptr;
    NimBLEAdvertising* _advertising_obj = nullptr;

    // Client connections (as central)
    std::map<uint16_t, NimBLEClient*> _clients;

    // Connection tracking
    std::map<uint16_t, ConnectionHandle> _connections;

    // Connection handle allocator (NimBLE uses its own, we wrap for consistency)
    uint16_t _next_conn_handle = 1;

    // Callbacks
    Callbacks::OnScanResult _on_scan_result;
    Callbacks::OnScanComplete _on_scan_complete;
    Callbacks::OnConnected _on_connected;
    Callbacks::OnDisconnected _on_disconnected;
    Callbacks::OnMTUChanged _on_mtu_changed;
    Callbacks::OnServicesDiscovered _on_services_discovered;
    Callbacks::OnDataReceived _on_data_received;
    Callbacks::OnNotifyEnabled _on_notify_enabled;
    Callbacks::OnCentralConnected _on_central_connected;
    Callbacks::OnCentralDisconnected _on_central_disconnected;
    Callbacks::OnWriteReceived _on_write_received;
    Callbacks::OnReadRequested _on_read_requested;
};

}} // namespace RNS::BLE

#endif // ESP32 && USE_NIMBLE
