# BLE-Reticulum Protocol v2.2 Implementation Plan

## Overview

Implement the BLE-Reticulum protocol v2.2 as a C++ interface for microReticulum, targeting ESP32-S3 and nRF52840 platforms with a HAL abstraction layer.

**Requirements:**
- [x] HAL abstraction supporting NimBLE-Arduino and ESP-IDF native BLE
- [x] Dual-mode operation (central + peripheral simultaneously)
- [x] Separate fragmentation layer (reusable classes)
- [x] Both platforms in parallel (ESP32-S3 and nRF52840)

---

## Protocol Summary (v2.2)

### GATT Service Structure

| Component | UUID |
|-----------|------|
| Service | `37145b00-442d-4a94-917f-8f42c5da28e3` |
| RX Characteristic (write) | `37145b00-442d-4a94-917f-8f42c5da28e5` |
| TX Characteristic (notify) | `37145b00-442d-4a94-917f-8f42c5da28e4` |
| Identity Characteristic (read) | `37145b00-442d-4a94-917f-8f42c5da28e6` |

### Key Protocol Features

- **MAC sorting:** Lower MAC initiates connection (becomes central)
- **Identity handshake:** 16-byte identity exchange after connection
- **Fragmentation:** 5-byte header (type, sequence, total) + payload
- **Keepalive:** 15-second interval to prevent BLE timeout
- **Peer scoring:** RSSI (60%) + history (30%) + recency (10%)
- **Blacklist backoff:** 60s × min(2^(failures-3), 8)

### Fragment Header Format (5 bytes)

```
Byte 0:     Type (0x01=START, 0x02=CONTINUE, 0x03=END)
Bytes 1-2:  Sequence number (big-endian)
Bytes 3-4:  Total fragments (big-endian)
Bytes 5+:   Payload (MTU - 5 bytes)
```

---

## File Structure

```
src/BLE/
    BLETypes.h              # Common types, UUIDs, constants
    BLEFragmenter.h/.cpp    # Fragment outgoing packets
    BLEReassembler.h/.cpp   # Reassemble incoming fragments
    BLEPeerManager.h/.cpp   # Peer tracking, scoring, blacklist
    BLEIdentityManager.h/.cpp # Identity handshake, MAC↔identity mapping
    BLEPlatform.h           # HAL abstract interface
    BLEOperationQueue.h/.cpp # GATT operation serialization
    platforms/
        NimBLEPlatform.h/.cpp    # ESP32 NimBLE-Arduino
        ESPIDFPlatform.h/.cpp    # ESP32 ESP-IDF native (optional)
        ZephyrPlatform.h/.cpp    # nRF52840 Zephyr

examples/common/ble_interface/
    BLEInterface.h/.cpp     # Main InterfaceImpl subclass
    BLEPeerInterface.h/.cpp # Per-peer sub-interface
    library.properties
```

---

## Implementation Progress

### Phase 1: Core Protocol Layer (No BLE dependency)
- [x] `src/BLE/BLETypes.h` - Constants, UUIDs, enums, common structures
- [x] `src/BLE/BLEFragmenter.h/.cpp` - Fragment outgoing packets
- [x] `src/BLE/BLEReassembler.h/.cpp` - Reassemble incoming fragments with timeout
- [ ] Unit tests for fragmentation/reassembly

### Phase 2: Peer & Identity Management
- [x] `src/BLE/BLEPeerManager.h/.cpp` - Peer tracking, scoring, blacklist
- [x] `src/BLE/BLEIdentityManager.h/.cpp` - Identity handshake, MAC↔identity mapping
- [ ] Unit tests for peer management

### Phase 3: HAL Abstract Interface
- [x] `src/BLE/BLEPlatform.h` - IBLEPlatform abstract interface
- [x] `src/BLE/BLEOperationQueue.h/.cpp` - GATT operation serialization
- [x] `src/BLE/BLEPlatform.cpp` - Platform factory

### Phase 4: NimBLE Platform Implementation (ESP32-S3)
- [x] `src/BLE/platforms/NimBLEPlatform.h/.cpp` - NimBLE-Arduino implementation
- [x] Peripheral mode (GATT server, advertising)
- [x] Central mode (scanning, GATT client)
- [x] MTU negotiation
- [ ] Dual-mode operation testing

### Phase 5: BLEInterface (InterfaceImpl subclass)
- [x] `examples/common/ble_interface/BLEInterface.h/.cpp` - Main interface
- [ ] `examples/common/ble_interface/BLEPeerInterface.h/.cpp` - Per-peer interface (optional)
- [x] `examples/common/ble_interface/library.properties` - PlatformIO metadata
- [ ] Integration with Transport layer

### Phase 6: ESP-IDF Platform (Optional)
- [ ] `src/BLE/platforms/ESPIDFPlatform.h/.cpp` - ESP-IDF native implementation

### Phase 7: Zephyr Platform (nRF52840)
- [ ] `src/BLE/platforms/ZephyrPlatform.h/.cpp` - Zephyr BLE implementation
- [ ] Static GATT service definition
- [ ] nRF52840 testing

### Phase 8: Testing & Validation
- [ ] ESP32-to-ESP32 mesh connectivity
- [ ] Interop testing with Python RNS BLEInterface
- [ ] Multi-peer stress testing
- [ ] Documentation

---

## Class Definitions

### BLEFragmenter

```cpp
namespace RNS::BLE {

class BLEFragmenter {
public:
    static const size_t HEADER_SIZE = 5;
    enum Type : uint8_t { START = 0x01, CONTINUE = 0x02, END = 0x03 };

    explicit BLEFragmenter(size_t mtu = 23);
    void set_mtu(size_t mtu);
    size_t get_mtu() const;
    size_t get_payload_size() const;  // mtu - HEADER_SIZE

    bool needs_fragmentation(const Bytes& data) const;
    uint16_t calculate_fragment_count(size_t data_size) const;
    std::vector<Bytes> fragment(const Bytes& data, uint16_t sequence_base = 0);

    static Bytes create_fragment(Type type, uint16_t seq, uint16_t total, const Bytes& payload);
    static bool parse_header(const Bytes& frag, Type& type, uint16_t& seq, uint16_t& total);
};

}
```

### BLEReassembler

```cpp
namespace RNS::BLE {

class BLEReassembler {
public:
    static constexpr double TIMEOUT = 30.0;  // v2.2 spec
    using ReassemblyCallback = std::function<void(const Bytes& peer_identity, const Bytes& packet)>;
    using TimeoutCallback = std::function<void(const Bytes& peer_identity, const std::string& reason)>;

    void set_reassembly_callback(ReassemblyCallback callback);
    void set_timeout_callback(TimeoutCallback callback);

    bool process_fragment(const Bytes& peer_identity, const Bytes& fragment);
    void check_timeouts();
    void clear_for_peer(const Bytes& identity);
    size_t pending_count() const;
};

}
```

### BLEPeerManager

```cpp
namespace RNS::BLE {

class BLEPeerManager {
public:
    static const size_t MAX_PEERS = 7;
    static constexpr double KEEPALIVE_INTERVAL = 15.0;

    enum class PeerState { DISCOVERED, CONNECTING, HANDSHAKING, CONNECTED, DISCONNECTING, BLACKLISTED };

    struct PeerInfo {
        Bytes mac_address;
        Bytes identity;
        PeerState state;
        bool is_central;
        double last_seen;
        int8_t rssi;
        uint32_t connection_attempts;
        uint32_t connection_successes;
        uint16_t conn_handle;
        size_t mtu;
        float score;
    };

    bool add_discovered_peer(const Bytes& mac, int8_t rssi);
    bool set_peer_identity(const Bytes& mac, const Bytes& identity);
    PeerInfo* get_peer_by_mac(const Bytes& mac);
    PeerInfo* get_peer_by_identity(const Bytes& identity);
    PeerInfo* get_best_connection_candidate();

    static bool should_initiate_connection(const Bytes& peer_mac, const Bytes& our_mac);

    void connection_succeeded(const Bytes& identity);
    void connection_failed(const Bytes& mac);
    void recalculate_scores();
    void check_blacklist_expirations();
};

}
```

### BLEIdentityManager

```cpp
namespace RNS::BLE {

class BLEIdentityManager {
public:
    static const size_t IDENTITY_SIZE = 16;
    using HandshakeCompleteCallback = std::function<void(const Bytes& mac, const Bytes& identity, bool is_central)>;

    void set_local_identity(const Bytes& identity_hash);
    const Bytes& get_local_identity() const;

    void set_handshake_complete_callback(HandshakeCompleteCallback callback);

    Bytes initiate_handshake(const Bytes& mac_address);
    bool process_received_data(const Bytes& mac, const Bytes& data, bool is_central);
    bool is_handshake_data(const Bytes& data, const Bytes& mac) const;

    Bytes get_identity_for_mac(const Bytes& mac) const;
    Bytes get_mac_for_identity(const Bytes& identity) const;
    bool has_identity(const Bytes& mac) const;
    void update_mac_for_identity(const Bytes& identity, const Bytes& new_mac);
    void remove_mapping(const Bytes& mac);
};

}
```

### IBLEPlatform (HAL Interface)

```cpp
namespace RNS::BLE {

struct BLEAddress { uint8_t addr[6]; uint8_t type; };
struct ScanResult { BLEAddress address; std::string name; int8_t rssi; bool has_reticulum_service; };
struct ConnectionHandle { uint16_t handle; BLEAddress peer; Role role; ConnectionState state; uint16_t mtu; };
struct PlatformConfig { Role role; std::string device_name; uint16_t preferred_mtu; uint8_t max_connections; };

class IBLEPlatform {
public:
    using Ptr = std::shared_ptr<IBLEPlatform>;
    virtual ~IBLEPlatform() = default;

    // Lifecycle
    virtual bool initialize(const PlatformConfig& config) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isRunning() const = 0;

    // Central mode
    virtual bool startScan(uint16_t duration_ms = 0) = 0;
    virtual void stopScan() = 0;
    virtual bool connect(const BLEAddress& address, uint16_t timeout_ms = 10000) = 0;
    virtual bool disconnect(uint16_t conn_handle) = 0;
    virtual bool requestMTU(uint16_t conn_handle, uint16_t mtu) = 0;
    virtual bool discoverServices(uint16_t conn_handle) = 0;

    // Peripheral mode
    virtual bool startAdvertising() = 0;
    virtual void stopAdvertising() = 0;
    virtual void setIdentityData(const Bytes& identity) = 0;

    // GATT operations
    virtual bool write(uint16_t conn_handle, const Bytes& data, bool response = true) = 0;
    virtual bool notify(uint16_t conn_handle, const Bytes& data) = 0;
    virtual bool enableNotifications(uint16_t conn_handle, bool enable) = 0;

    // Connection info
    virtual std::vector<ConnectionHandle> getConnections() const = 0;
    virtual size_t getConnectionCount() const = 0;
    virtual BLEAddress getLocalAddress() const = 0;

    // Callbacks
    virtual void setOnScanResult(std::function<void(const ScanResult&)> cb) = 0;
    virtual void setOnConnected(std::function<void(const ConnectionHandle&)> cb) = 0;
    virtual void setOnDisconnected(std::function<void(const ConnectionHandle&, uint8_t)> cb) = 0;
    virtual void setOnMTUChanged(std::function<void(const ConnectionHandle&, uint16_t)> cb) = 0;
    virtual void setOnDataReceived(std::function<void(const ConnectionHandle&, const Bytes&)> cb) = 0;
    virtual void setOnWriteReceived(std::function<void(const ConnectionHandle&, const Bytes&)> cb) = 0;
};

class BLEPlatformFactory {
public:
    static IBLEPlatform::Ptr create();  // Auto-detect platform
};

}
```

### BLEInterface (Main Interface)

```cpp
class BLEInterface : public RNS::InterfaceImpl {
public:
    static const uint32_t BITRATE_GUESS = 100000;
    static const uint16_t HW_MTU = 512;

    BLEInterface(const char* name = "BLEInterface");
    virtual ~BLEInterface();

    // Configuration (call before start)
    void setRole(RNS::BLE::Role role);
    void setDeviceName(const std::string& name);
    void setMaxConnections(uint8_t max);

    // InterfaceImpl overrides
    virtual bool start() override;
    virtual void stop() override;
    virtual void loop() override;
    virtual std::string toString() const override;

    size_t peerCount() const;

protected:
    virtual void send_outgoing(const RNS::Bytes& data) override;

private:
    RNS::BLE::IBLEPlatform::Ptr _platform;
    RNS::BLE::BLEFragmenter _fragmenter;
    RNS::BLE::BLEReassembler _reassembler;
    RNS::BLE::BLEPeerManager _peer_manager;
    RNS::BLE::BLEIdentityManager _identity_manager;

    // Callback handlers
    void onScanResult(const RNS::BLE::ScanResult& result);
    void onConnected(const RNS::BLE::ConnectionHandle& conn);
    void onDisconnected(const RNS::BLE::ConnectionHandle& conn, uint8_t reason);
    void onMTUChanged(const RNS::BLE::ConnectionHandle& conn, uint16_t mtu);
    void onDataReceived(const RNS::BLE::ConnectionHandle& conn, const RNS::Bytes& data);
    void onWriteReceived(const RNS::BLE::ConnectionHandle& conn, const RNS::Bytes& data);
};
```

---

## Data Flow

### Outgoing Packet Flow

```
1. Transport::transmit(packet)
2. BLEInterface::send_outgoing(data)
3. For each connected peer:
   a. BLEFragmenter::fragment(data, mtu)
   b. For each fragment: _platform->write(conn_handle, fragment)
4. InterfaceImpl::handle_outgoing(data)  // stats
```

### Incoming Packet Flow

```
1. Platform callback: onDataReceived(conn, fragment)
2. BLEIdentityManager::process_received_data(mac, data)
   - If handshake (16 bytes, no existing identity): complete handshake, return true
   - Otherwise: return false
3. BLEReassembler::process_fragment(identity, fragment)
   - If complete packet: _reassembly_callback(identity, packet)
4. BLEInterface handles callback:
   - InterfaceImpl::handle_incoming(packet) → Transport::inbound()
```

---

## Connection Lifecycle

```
1. Discovery      Scan finds peer with Reticulum service UUID
2. MAC Sorting    Lower MAC initiates connection (becomes central)
3. Connect        Central connects to peripheral
4. Services       Discover RX/TX/Identity characteristics
5. Read Identity  Central reads peripheral's 16-byte identity
6. Subscribe      Central enables TX notifications
7. Handshake      Central writes its 16-byte identity to RX
8. MTU            Request 517 bytes, use negotiated value
9. Ready          Data exchange via fragments
10. Keepalive     15-second pings prevent timeout
```

---

## Build Configuration

### platformio.ini (ESP32-S3 with NimBLE)

```ini
[env:esp32s3_ble]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
lib_deps =
    h2zero/NimBLE-Arduino@^1.4.0
build_flags =
    -DUSE_NIMBLE
    -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=1
    -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=1
    -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=7
```

### platformio.ini (nRF52840 with Zephyr)

```ini
[env:nrf52840_ble]
platform = nordicnrf52
board = nrf52840_dk
framework = zephyr
build_flags =
    -DCONFIG_BT=y
    -DCONFIG_BT_CENTRAL=y
    -DCONFIG_BT_PERIPHERAL=y
    -DCONFIG_BT_MAX_CONN=7
```

---

## Reference Files

| File | Purpose |
|------|---------|
| `src/Interface.h:37-90` | InterfaceImpl base class pattern |
| `src/Bytes.h` | Core data container (COW semantics) |
| `examples/common/auto_interface/AutoInterface.h` | Multi-peer interface pattern |
| `src/Utilities/OS.h` | Timing (OS::time()), endianness helpers |
| `src/Identity.h` | Identity hash generation |
| `../public/ble-reticulum/` | Python reference implementation |
| `../public/columba/` | Kotlin/Android reference implementation |

---

## Notes

- **Thread Safety:** BLE callbacks may execute in ISR or BLE task context. Use producer-consumer queues to defer processing to main loop.
- **Operation Queue:** BLE stacks don't queue GATT operations internally. Always serialize with BLEOperationQueue.
- **MTU Delay:** Add 150ms delay after MTU negotiation before enabling notifications (prevents GATT_WRITE_REQUEST_BUSY errors).
- **Keepalive:** Send first keepalive immediately after connection (don't wait for interval).
