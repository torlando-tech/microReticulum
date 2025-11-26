# microReticulum Completion Project

## Project Goal
Complete the C++ implementation of the Reticulum Network Stack (microReticulum) to achieve full interoperability with the Python reference implementation. Target platforms are ESP32-S3 (LilyGo T-Deck Plus, Heltec V3/V4) and Nordic nRF52 (RAK4631), but all development and testing starts on native Linux.

## Repository Setup

### Fork and Clone
```bash
# Fork https://github.com/attermann/microReticulum to your GitHub account first
git clone https://github.com/YOUR_USERNAME/microReticulum.git
cd microReticulum

# Add upstream remote
git remote add upstream https://github.com/attermann/microReticulum.git

# Create feature branch
git checkout -b feature/complete-implementation
```

### Install Dependencies
```bash
# PlatformIO (build system)
pip install platformio

# Clone Python RNS for testing
cd ..
git clone https://github.com/markqvist/Reticulum.git
cd Reticulum
pip install -e .

# Verify Python RNS works
rnsd --help
```

### Verify Native Build Works
```bash
cd microReticulum/examples/udp_announce
pio run -e native
# Should compile without errors

# Run it (will fail to connect, that's fine for now)
.pio/build/native/program
```

---

## Current Implementation Status

### ✅ Complete and Working
- Framework with C++ API mimicking Python reference
- `Bytes` utility class for buffer handling
- `Identity` - key generation, loading, signing, verification
- `Destination` - creation, addressing, proof strategies  
- `Packet` - creation, encryption, decryption, signing, proofs
- `Transport` - path finding, packet routing, interface management
- `Link` - establishment, teardown, basic packet exchange
- `Interfaces` - UDP interface (for testing), LoRa interface (for hardware)
- `Persistence` - config, identities, routing tables via msgpack
- Cryptography - Ed25519, X25519, AES-256-CBC, HKDF, HMAC, Fernet, PKCS7
- Memory management - TLSF allocator for MCU targets

### ❌ Not Implemented (YOUR WORK)
1. **Ratchets** - Forward secrecy mechanism for links
2. **Resource** - Large data transfer with fragmentation/reassembly
3. **Channel** - Multiplexed message streams over links
4. **Buffer** - Stream-oriented wrapper around Channel

### ⚠️ Partially Implemented / Needs Verification
- Link callbacks and lifecycle management
- Request/Response mechanism (may be incomplete)

---

## Feature Implementation Order

Implement in this order due to dependencies:

```
1. Ratchets (Link depends on this for forward secrecy)
      ↓
2. Resource (standalone large transfer mechanism)
      ↓
3. Channel (uses Link, provides message multiplexing)
      ↓
4. Buffer (wraps Channel with stream interface)
```

---

## Feature 1: Link Key Derivation (Previously "Ratchets")

### Clarification: Python RNS Link Encryption

**IMPORTANT**: The original document incorrectly described per-message key rotation ("ratchets") for Links. After reviewing Python RNS source code (`RNS/Link.py`), here's what Python actually implements:

**Python RNS Link Encryption (Actual Behavior):**
1. **ECDH Key Exchange**: During link establishment, peers exchange X25519 public keys
2. **Single Derived Key**: HKDF derives a single key from the ECDH shared secret
3. **Token-based Encryption**: The derived key creates a Token used for ALL packets on the link
4. **No Per-Message Ratcheting**: Python RNS does NOT rotate keys after each message

The `handshake()` method in `Link.py` shows:
```python
self.shared_key = self.prv.exchange(self.peer_pub)
self.derived_key = RNS.Cryptography.hkdf(
    length=derived_key_length,
    derive_from=self.shared_key,
    salt=self.get_salt(),
    context=self.get_context())
# ...
self.token = Token(self.derived_key)  # Used for ALL packets
```

### What C++ Must Implement
The C++ implementation already correctly matches Python's behavior:
- `Link::handshake()` performs ECDH exchange and HKDF derivation
- `_derived_key` is used to create a Token for all link packets
- No key rotation between messages

### Python Reference Files
- `RNS/Link.py` - See `handshake()` method (lines 353-366), Token creation (lines 1193, 1207)
- `RNS/Cryptography/Token.py` - Token encryption using derived key

### Success Criteria
- [x] Link can initialize derived key from ECDH shared secret via HKDF
- [x] Derived key creates Token for link encryption
- [x] Keys match Python RNS output for same inputs (Link establishment works)
- [x] Test: Establish link between C++ and Python nodes, exchange 100+ messages (PASSED 2025-11-26)

### Test Procedure
```bash
# Terminal 1: Python target
cd Reticulum
python -c "
import RNS
import time

rns = RNS.Reticulum('./tests/rnsconfig')
identity = RNS.Identity()
dest = RNS.Destination(identity, RNS.Destination.IN, RNS.Destination.SINGLE, 'test', 'ratchet')
dest.set_proof_strategy(RNS.Destination.PROVE_ALL)

def link_established(link):
    print(f'Link established: {link}')
    link.set_link_closed_callback(lambda l: print('Link closed'))

dest.set_link_established_callback(link_established)
print(f'Destination hash: {dest.hexhash}')

while True:
    time.sleep(1)
"

# Terminal 2: C++ client (you'll modify examples/link or create new test)
cd microReticulum/examples/link
pio run -e native
.pio/build/native/program
```

---

## Feature 2: Resource

### What It Does
Resource enables transfer of arbitrary-sized data over a Link. It handles:
- Fragmentation into MTU-sized chunks
- Reassembly on receive
- Progress callbacks
- Compression (optional)
- Checksums and verification
- Timeout handling
- Metadata attachment

### Python Reference Files
- `RNS/Resource.py` (~800 lines) - Main implementation
- `RNS/Link.py` - `request_resource`, `handle_resource_*` methods
- `tests/link.py` - `test_09_large_resource` for test cases

### Key Classes to Implement
```cpp
// New file: src/Resource.h, src/Resource.cpp

namespace RNS {

class Resource {
public:
    enum Status {
        NONE           = 0x00,
        QUEUED         = 0x01,
        ADVERTISED     = 0x02,
        TRANSFERRING   = 0x03,
        COMPLETE       = 0x04,
        FAILED         = 0x05,
        CORRUPT        = 0x06,
    };

    // Constructors
    Resource(const Bytes& data, Link& link, 
             Callbacks::resource_callback callback = nullptr,
             Callbacks::progress_callback progress = nullptr,
             double timeout = 0.0,
             bool auto_compress = true);
    
    // For receiving
    static Resource accept(const Bytes& advertisement, Link& link,
                          Callbacks::resource_callback callback = nullptr,
                          Callbacks::progress_callback progress = nullptr);

    // Accessors
    Status status() const;
    double progress() const;
    size_t total_size() const;
    Bytes data() const;
    Bytes request_id() const;
    
    // Control
    void cancel();

private:
    // Fragmentation
    void _segment_data();
    void _send_next_segment();
    void _receive_segment(const Bytes& segment);
    
    // State
    Status _status;
    Bytes _data;
    Bytes _hash;
    Bytes _request_id;
    std::vector<Bytes> _segments;
    size_t _segment_index;
    size_t _total_segments;
    Link* _link;
    double _timeout;
    double _started_at;
    bool _auto_compress;
    
    // Callbacks
    Callbacks::resource_callback _callback;
    Callbacks::progress_callback _progress_callback;
};

} // namespace RNS
```

### Success Criteria
- [ ] Can send 1KB resource from C++ to Python and receive confirmation
- [ ] Can receive 1KB resource from Python in C++
- [ ] Can send/receive 1MB resource with correct data integrity
- [ ] Can send/receive 50MB resource (matches `test_09_large_resource`)
- [ ] Progress callbacks fire with accurate percentages
- [ ] Timeout handling works (cancel after deadline)
- [ ] Metadata dict can be attached and retrieved
- [ ] Compression toggle works (auto_compress=true/false)

### Test Procedure
```bash
# Use the existing Python test target
cd Reticulum
python -c "from tests.link import targets; targets()"

# In another terminal, run C++ Resource test
# (You'll create this test case)
```

### Segment Size Calculation
From Python `Resource.py`:
```python
SEGMENT_SIZE = LINK_MTU - RESOURCE_OVERHEAD
# LINK_MTU is typically 500 bytes
# RESOURCE_OVERHEAD is header size for segment metadata
```

---

## Feature 3: Channel

### What It Does
Channel provides a multiplexed message-passing interface over a Link. Multiple logical "streams" can share one Link, each with its own message types and handlers.

### Python Reference Files
- `RNS/Channel.py` (~400 lines)
- `RNS/Link.py` - `get_channel()` method
- `tests/channel.py` - Unit tests for Channel
- `tests/link.py` - `test_10_channel_round_trip`

### Key Classes to Implement
```cpp
// New file: src/Channel.h, src/Channel.cpp

namespace RNS {

class MessageBase {
public:
    virtual ~MessageBase() = default;
    virtual uint16_t message_type() const = 0;
    virtual Bytes pack() const = 0;
    virtual void unpack(const Bytes& data) = 0;
    
    Bytes id() const { return _id; }
protected:
    Bytes _id;
};

class Channel {
public:
    Channel(Link& link);
    
    // Message type registration
    template<typename T>
    void register_message_type();
    
    // Send/receive
    void send(const MessageBase& message);
    void add_message_handler(std::function<void(MessageBase&)> handler);
    
    // State
    bool is_ready_to_send() const;
    
private:
    Link* _link;
    std::map<uint16_t, std::function<MessageBase*()>> _message_factories;
    std::vector<std::function<void(MessageBase&)>> _handlers;
    
    // Sequencing
    uint16_t _next_sequence;
    std::deque<std::pair<uint16_t, Bytes>> _rx_ring;
    
    void _receive(const Bytes& raw);
    void _process_rx_ring();
};

} // namespace RNS
```

### Success Criteria
- [ ] Can register custom message types
- [ ] Can send message from C++ Channel, receive in Python Channel
- [ ] Can receive message from Python, dispatch to correct handler in C++
- [ ] Message IDs preserved across round-trip
- [ ] Multiple message types can coexist on same Channel
- [ ] `test_10_channel_round_trip` pattern works C++ ↔ Python

### Test Message Type (for compatibility testing)
```cpp
// Match the Python tests/channel.py MessageTest class
class MessageTest : public MessageBase {
public:
    static constexpr uint16_t TYPE = 0xABCD; // Check actual value in Python
    
    std::string data;
    std::string not_serialized = "default"; // Not sent over wire
    
    uint16_t message_type() const override { return TYPE; }
    
    Bytes pack() const override {
        // msgpack: {"data": self.data}
    }
    
    void unpack(const Bytes& raw) override {
        // Deserialize msgpack
    }
};
```

---

## Feature 4: Buffer

### What It Does
Buffer provides a stream-oriented interface (like a file or socket) on top of Channel. Supports `read()`, `write()`, `readline()`, `flush()`, and ready callbacks.

### Python Reference Files
- `RNS/Buffer.py` (~300 lines)
- `tests/channel.py` - `test_buffer_*` tests
- `tests/link.py` - `test_11_buffer_round_trip`, `test_12_buffer_round_trip_big`

### Key Classes to Implement
```cpp
// New file: src/Buffer.h, src/Buffer.cpp

namespace RNS {

// Internal message type for Buffer data
class StreamDataMessage : public MessageBase {
public:
    static constexpr uint16_t TYPE = 0x0001; // Check Python value
    Bytes stream_data;
    bool eof = false;
    // ... pack/unpack
};

class RawChannelReader {
public:
    RawChannelReader(uint8_t stream_id, Channel& channel);
    
    Bytes read(size_t max_bytes = 0); // 0 = read all available
    Bytes readline();
    void add_ready_callback(std::function<void(size_t)> callback);
    void close();
    
private:
    Channel* _channel;
    uint8_t _stream_id;
    Bytes _buffer;
    bool _eof;
    std::function<void(size_t)> _ready_callback;
};

class RawChannelWriter {
public:
    RawChannelWriter(uint8_t stream_id, Channel& channel);
    
    size_t write(const Bytes& data);
    void flush();
    void close();
    
private:
    Channel* _channel;
    uint8_t _stream_id;
    Bytes _buffer;
};

// Convenience factory functions (match Python API)
namespace Buffer {
    std::pair<RawChannelReader, RawChannelWriter> 
    create_bidirectional_buffer(uint8_t rx_stream_id, 
                                 uint8_t tx_stream_id,
                                 Channel& channel,
                                 std::function<void(size_t)> ready_callback = nullptr);
    
    RawChannelReader create_reader(uint8_t stream_id, Channel& channel);
    RawChannelWriter create_writer(uint8_t stream_id, Channel& channel);
}

} // namespace RNS
```

### Success Criteria
- [ ] Can write bytes to Buffer, flush, read on other end
- [ ] `readline()` correctly handles newline delimiters
- [ ] Ready callback fires when data available
- [ ] EOF signaling works (writer.close() → reader sees EOF)
- [ ] 32KB round-trip works (matches BUFFER_TEST_TARGET in Python tests)
- [ ] Bidirectional buffer works (both sides read and write)
- [ ] `test_11_buffer_round_trip` passes C++ ↔ Python
- [ ] `test_12_buffer_round_trip_big` passes C++ ↔ Python

---

## Testing Infrastructure

### Directory Structure
Create a new test directory in microReticulum:
```
microReticulum/
├── test/
│   ├── test_rns_loopback/      # Existing
│   ├── test_persistence/        # Existing
│   ├── test_interop/            # NEW: Python interop tests
│   │   ├── platformio.ini
│   │   ├── test_main.cpp
│   │   ├── test_resource.cpp
│   │   ├── test_channel.cpp
│   │   ├── test_buffer.cpp
│   │   └── python_targets/
│   │       ├── resource_target.py
│   │       ├── channel_target.py
│   │       └── buffer_target.py
│   └── scripts/
│       └── run_interop_tests.sh
```

### Interop Test Runner Script
```bash
#!/bin/bash
# test/scripts/run_interop_tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MICRO_RET_DIR="$SCRIPT_DIR/../.."
PYTHON_RNS_DIR="$MICRO_RET_DIR/../Reticulum"

echo "Building native test binary..."
cd "$MICRO_RET_DIR/test/test_interop"
pio run -e native

echo "Starting Python target..."
cd "$PYTHON_RNS_DIR"
python "$MICRO_RET_DIR/test/test_interop/python_targets/resource_target.py" &
PYTHON_PID=$!
sleep 2

echo "Running C++ tests..."
cd "$MICRO_RET_DIR/test/test_interop"
.pio/build/native/program

echo "Cleaning up..."
kill $PYTHON_PID 2>/dev/null || true

echo "All tests passed!"
```

### Fixed Test Keys (Use These for Reproducibility)
From Python `tests/link.py`:
```cpp
// Include in your test files
const char* TEST_KEYS[][2] = {
    {"f8953ffaf607627e615603ff1530c82c434cf87c07179dd7689ea776f30b964cfb7ba6164af00c5111a45e69e57d885e1285f8dbfe3a21e95ae17cf676b0f8b7", 
     "650b5d76b6bec0390d1f8cfca5bd33f9"},
    {"d85d036245436a3c33d3228affae06721f8203bc364ee0ee7556368ac62add650ebf8f926abf628da9d92baaa12db89bd6516ee92ec29765f3afafcb8622d697", 
     "1469e89450c361b253aefb0c606b6111"},
    // ... add more from Python tests
};
```

---

## Build Configuration

### Native Development (Primary)
```ini
# platformio.ini section for native testing
[env:native]
platform = native
build_flags =
    ${env.build_flags}
    -std=c++11
    -g3                          # Debug symbols
    -ggdb                        # GDB-friendly debug info
    -Wall -Wextra                # Warnings
    -fsanitize=address           # Memory error detection (optional)
    -fsanitize=undefined         # UB detection (optional)
    -DNATIVE
    -DRNS_USE_ALLOCATOR=0        # Don't use TLSF on native
lib_deps =
    ${env.lib_deps}
```

### ESP32-S3 Target (Later)
```ini
[env:lilygo_tdeck]
platform = espressif32
board = esp32-s3-devkitc-1       # Generic S3, customize as needed
framework = arduino
board_build.partitions = no_ota.csv
build_flags =
    ${env.build_flags}
    -DBOARD_TDECK
    -DRNS_USE_ALLOCATOR=1
    -DRNS_USE_TLSF=1
lib_deps =
    ${env.lib_deps}
```

---

## Development Workflow

### Daily Workflow
```bash
# 1. Pull latest upstream changes (if any)
git fetch upstream
git rebase upstream/main

# 2. Work on feature branch
git checkout feature/resource  # or whatever feature

# 3. Build and test
cd examples/udp_announce  # or test/test_interop
pio run -e native
.pio/build/native/program

# 4. If testing against Python RNS, in separate terminal:
cd ../Reticulum
python -c "from tests.link import targets; targets()"

# 5. Commit with clear messages
git commit -m "Resource: implement segment transmission"
```

### Code Style
Follow existing microReticulum patterns:
- Classes in `RNS` namespace
- Header files in `src/`, implementation in `src/`
- Use `Bytes` class for all byte buffers
- Use `TRACE()`, `DEBUG()`, `INFO()`, `ERROR()` macros for logging
- Follow existing callback patterns (see `Link.h` for examples)

### Debugging Tips
```bash
# Run with GDB
cd test/test_interop
pio debug -e native

# Run with Valgrind (memory check)
valgrind --leak-check=full .pio/build/native/program

# Increase log verbosity
# In code: RNS::Reticulum::loglevel(RNS::LOG_TRACE);
```

---

## Success Milestones

### Milestone 1: Link Key Derivation Working (Corrected) - COMPLETE
- [x] Link establishment with HKDF key derivation (matches Python)
- [x] 100+ messages exchanged successfully (validates Token encryption)
- [x] No crashes, no memory leaks

### Milestone 2: Resource Transfer
- [x] 1KB resource C++ → Python (complete - 2025-11-26)
- [x] 1KB resource Python → C++ (receiving complete - 2025-11-26)
- [ ] 1MB resource both directions
- [ ] 50MB resource (matches Python test)
- [ ] Progress callbacks working

#### Implementation Notes (2025-11-26):
Resource **receiving** from Python to C++ is fully functional:
- ResourceAdvertisement parsing (msgpack) ✓
- Resource::accept() for incoming resources ✓
- Hashmap management and part requests ✓
- Token decryption of resource data ✓
- BZ2 decompression ✓
- Proof computation and validation ✓
- Key fix: Proof packet must use `Type::Packet::PROOF` (0x03) not DATA

Resource **sending** from C++ to Python is fully functional (2025-11-26):
- Resource constructor for outgoing data ✓
- Data compression (bz2) ✓
- Token encryption of resource data ✓
- Hashmap generation for part tracking ✓
- ResourceAdvertisement::pack() with all 11 fields ✓
- advertise() method sends advertisement packet ✓
- request() method responds to RESOURCE_REQ ✓
- Key fixes applied:
  - ResourceAdvertisement must include ALL 11 fields (t,d,n,h,r,o,i,l,q,f,m)
  - Don't double-encrypt: Packet class handles link encryption automatically
- Verified: 1KB resource with compression (933 bytes saved), data integrity confirmed

### Milestone 3: Channel Messaging
- [ ] Custom message types ✓
- [ ] Round-trip messaging ✓
- [ ] Multiple handlers ✓

### Milestone 4: Buffer Streaming
- [ ] Basic read/write ✓
- [ ] Ready callbacks ✓
- [ ] 32KB round-trip ✓
- [ ] EOF handling ✓

### Milestone 5: Full Integration
- [ ] All Python `tests/link.py` tests pass against C++ implementation
- [ ] ESP32-S3 build succeeds
- [ ] LoRa interface works on hardware
- [ ] Memory usage acceptable on MCU target

---

## Reference Documentation

### Key Python Files to Study
```
Reticulum/
├── RNS/
│   ├── Identity.py      # Already implemented, good reference
│   ├── Destination.py   # Already implemented
│   ├── Link.py          # Ratchet code is here
│   ├── Packet.py        # Already implemented
│   ├── Transport.py     # Already implemented
│   ├── Resource.py      # ← IMPLEMENT THIS
│   ├── Channel.py       # ← IMPLEMENT THIS
│   └── Buffer.py        # ← IMPLEMENT THIS
└── tests/
    ├── link.py          # Integration tests
    ├── channel.py       # Unit tests for Channel/Buffer
    └── identity.py      # Reference for testing patterns
```

### Key microReticulum Files
```
microReticulum/
├── src/
│   ├── Identity.h/.cpp      # Good example of implementation pattern
│   ├── Link.h/.cpp          # Add ratchet support here
│   ├── Packet.h/.cpp        # Reference for Bytes usage
│   ├── Transport.h/.cpp     # May need updates for Resource/Channel
│   ├── Bytes.h              # THE key utility class, study this
│   ├── Callbacks.h          # Callback type definitions
│   └── Type.h               # Enums and constants
├── examples/
│   ├── udp_announce/        # Good starting point
│   └── link/                # Link testing
└── test/
    ├── test_rns_loopback/   # Existing test pattern
    └── test_persistence/    # Another test pattern
```

---

## Getting Help

### If Stuck on Protocol Details
1. Read the Python implementation - it's the authoritative source
2. Add debug logging to Python RNS to see exact bytes/sequence
3. Check the Reticulum manual: https://markqvist.github.io/Reticulum/manual/

### If Stuck on C++ Issues
1. Check existing microReticulum code for patterns
2. The `Bytes` class handles most buffer complexity
3. Memory issues? Check TLSF allocator isn't enabled for native builds

### If Tests Fail
1. Compare byte-by-byte output between C++ and Python
2. Check endianness (network byte order)
3. Verify msgpack encoding matches exactly
4. Check timing - some operations have timeouts

---

## Notes for the Implementing Agent

1. **Start small**: Get the simplest test case working first, then expand
2. **Log everything**: Use TRACE/DEBUG liberally when implementing
3. **Match Python exactly**: When in doubt, do exactly what Python does
4. **Test incrementally**: Don't implement everything then test - test each piece
5. **Commit often**: Small, working commits are better than large broken ones
6. **Ask for clarification**: If the spec is unclear, check the Python source

The goal is a C++ implementation that is **byte-compatible** with Python RNS. If a C++ node and Python node can't interoperate, something is wrong.
