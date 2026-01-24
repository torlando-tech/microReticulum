# Phase 3 Plan 02: shared_ptr and Session Object Audit Findings

## Executive Summary

Audited 7 core classes (Identity, Destination, Link, Channel, Resource, Buffer) using the pimpl idiom with `shared_ptr`. Found **predominantly `new T() + shared_ptr` pattern** (14 sites) with only **2 `make_shared` usage sites** (Buffer.cpp and BLEPlatform.cpp). The codebase uses fixed-size pools extensively to avoid heap fragmentation.

**Key Findings:**
- Identity pools (destinations, ratchets) properly use PSRAM via `heap_caps_aligned_alloc`
- Link pools (resources, requests) use fixed-size arrays - zero heap fragmentation
- Channel uses fixed-size ring buffers (16 entries) - zero heap fragmentation
- Resource uses `std::vector` for parts/hashmap - potential fragmentation during transfers
- Most `new T()` allocations occur at startup or per-link-establishment (acceptable)
- Per-packet allocations minimal - `Bytes` is the main dynamic allocation

## MEM-03: shared_ptr Allocation Patterns

### Pattern Distribution

| Pattern | Count | Impact | Notes |
|---------|-------|--------|-------|
| `new T()` then `shared_ptr<T>(p)` | 14 | 2 allocations per object | Control block separate from object |
| `make_shared<T>()` | 2 | 1 allocation per object | Control block embedded with object |

### Overall Assessment
The 2-allocation pattern is functionally correct but:
- **Memory overhead:** 24-40 bytes extra per object (control block)
- **Fragmentation:** Two allocations vs one increases fragmentation potential
- **Performance:** Minor - cache locality slightly worse

For embedded systems, `make_shared` is preferred where object lifetime is well-defined.

---

## By Class

### Identity

| Site | File:Line | Pattern | Frequency | Notes |
|------|-----------|---------|-----------|-------|
| `Identity(create_keys)` | Identity.cpp:133 | `new Object()` | Per identity recall/create | ~64 bytes Object + crypto keys |
| `createKeys()` | Identity.cpp:144-160 | `Ptr::generate()` | Per identity create | Generates 4 crypto key objects |
| `load_private_key()` | Identity.cpp:181-186 | `from_private_bytes()` | Per identity load | Loads 4 crypto key objects |
| `load_public_key()` | Identity.cpp:228-229 | `from_public_bytes()` | Per identity load | Loads 2 public key objects |
| `encrypt()` | Identity.cpp:779 | `X25519PrivateKey::generate()` | Per encryption | Ephemeral key for encryption |

**Pool Allocations:**
- `_known_destinations_pool`: 192 slots x ~121 bytes = ~23KB (PSRAM verified via `heap_caps_aligned_alloc`)
- `_known_ratchets_pool`: 128 slots x ~57 bytes = ~7.3KB (static array - stack/BSS)

**PSRAM Verification (MEM-06):**
```cpp
// Identity.cpp:43 - PSRAM allocation confirmed
_known_destinations_pool = (KnownDestinationSlot*)heap_caps_aligned_alloc(
    8, pool_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

### Destination

| Site | File:Line | Pattern | Frequency | Notes |
|------|-----------|---------|-----------|-------|
| `Destination(identity, dir, type, name)` | Destination.cpp:16 | `new Object(identity)` | Per destination create | ~200 bytes Object |
| `Destination(identity, dir, type, hash)` | Destination.cpp:61 | `new Object(identity)` | Per destination create | Alternative constructor |

**Pool Allocations:**
- `_path_responses`: Fixed array of 8 slots in Object - zero fragmentation
- `_ratchets`: Fixed array of 128 slots in Object - zero fragmentation
- `_links`: `std::set<Link>` - dynamic (per incoming link)

### Link

| Site | File:Line | Pattern | Frequency | Notes |
|------|-----------|---------|-----------|-------|
| `Link(destination, ...)` | Link.cpp:38-39 | `new LinkData(destination)` | Per link establish | ~1KB LinkData object |
| `load_peer()` | Link.cpp:231-234 | `from_public_bytes()` | Per link handshake | 2 public key objects |

**Crypto Key Allocations (per link):**
- `_prv` - X25519PrivateKey (Link.cpp:52-63)
- `_sig_prv` - Ed25519PrivateKey (Link.cpp:63 for initiator)
- `_pub` - X25519PublicKey (Link.cpp:66)
- `_sig_pub` - Ed25519PublicKey (Link.cpp:70)
- `_peer_pub` - X25519PublicKey (Link.cpp:231)
- `_peer_sig_pub` - Ed25519PublicKey (Link.cpp:234)
- `_token` - Token for link encryption

**Fixed-Size Pools in LinkData:**
```cpp
// LinkData.h:101-111 - Zero heap fragmentation design
static constexpr size_t INCOMING_RESOURCES_SIZE = 8;
Resource _incoming_resources[INCOMING_RESOURCES_SIZE];
static constexpr size_t OUTGOING_RESOURCES_SIZE = 8;
Resource _outgoing_resources[OUTGOING_RESOURCES_SIZE];
static constexpr size_t PENDING_REQUESTS_SIZE = 8;
RequestReceipt _pending_requests[PENDING_REQUESTS_SIZE];
```

### Channel

| Site | File:Line | Pattern | Frequency | Notes |
|------|-----------|---------|-----------|-------|
| `Channel(link)` | Channel.cpp (implicit) | `new ChannelData(link)` | Per channel create | Via Link |

**Fixed-Size Ring Buffers (ChannelData.h:135-137):**
```cpp
static constexpr size_t RX_RING_SIZE = 16;
static constexpr size_t TX_RING_SIZE = 16;
Envelope _rx_ring_pool[RX_RING_SIZE];  // ~2KB total
Envelope _tx_ring_pool[TX_RING_SIZE];  // ~2KB total
```

**Dynamic Allocations:**
- `_message_factories`: `std::map` - set up once at registration
- `_message_callbacks`: `std::vector` - set up once at registration

### Resource

| Site | File:Line | Pattern | Frequency | Notes |
|------|-----------|---------|-----------|-------|
| `Resource(data, link, request_id, ...)` | Resource.cpp:29-30 | `new ResourceData(link)` | Per resource transfer | Large object |
| `Resource(data, link, advertise, ...)` | Resource.cpp:36-37 | `new ResourceData(link)` | Per resource transfer | Alternative constructor |

**Dynamic Allocations in ResourceData (per transfer):**
```cpp
// ResourceData.h:59-74 - Potential fragmentation during large transfers
std::vector<Bytes> _parts;      // Resized per transfer
std::vector<Bytes> _hashmap;    // Resized per transfer
std::set<Bytes> _req_hashlist;  // Track request packet hashes
```

**CONCERN:** Resource transfers resize `_parts` and `_hashmap` vectors during operation:
```cpp
// Resource.cpp:185-186
_object->_parts.resize(_object->_total_parts);
_object->_hashmap.resize(_object->_total_parts);
```

For large file transfers, this could be thousands of entries causing fragmentation.

### Buffer (RawChannelReader/Writer)

| Site | File:Line | Pattern | Frequency | Notes |
|------|-----------|---------|-----------|-------|
| `RawChannelReader(stream_id, channel)` | Buffer.cpp:152 | `make_shared<RawChannelReaderData>()` | Per stream | **GOOD: make_shared** |

**Only make_shared usage in session classes!**

---

## Cryptography Allocations

### Ed25519.h / X25519.h

The crypto key classes use `shared_ptr` internally:
```cpp
using Ptr = std::shared_ptr<Ed25519PrivateKey>;  // Ed25519.h:34
using Ptr = std::shared_ptr<X25519PrivateKey>;   // X25519.h:31
```

**Key Generation Frequency:**
| Operation | Ed25519 Keys | X25519 Keys | Total |
|-----------|--------------|-------------|-------|
| Identity create | 2 (prv, pub) | 2 (prv, pub) | 4 |
| Link establish (initiator) | 2 (prv, pub) | 2 (prv, pub) | 4 |
| Link establish (receiver) | 1 (peer) | 1 (peer) | 2 |
| Identity encrypt | 0 | 1 (ephemeral) | 1 |

### HMAC.h

```cpp
// HMAC.h:40-43 - new T() in constructor
_hash = std::unique_ptr<Hash>(new SHA256());
_hash = std::unique_ptr<Hash>(new SHA512());
```
Frequency: Per HMAC operation (moderate, during crypto operations)

---

## Session Object Lifecycle

### Allocation Frequency Summary

| Category | Objects | Trigger | Lifetime | Count Estimate |
|----------|---------|---------|----------|----------------|
| **Startup** | Identity::Object, pools | Boot | Application | 1 per config |
| **Per-destination** | Destination::Object | `Destination()` | Application | 1-10 |
| **Per-link** | LinkData, crypto keys, Token | Link establish | Link duration (mins-hours) | 1-8 concurrent |
| **Per-channel** | ChannelData | Link activate | Link duration | 1 per link |
| **Per-transfer** | ResourceData, parts vectors | Resource send/recv | Transfer duration (secs-mins) | 1-16 concurrent |
| **Per-packet** | Bytes (minimal) | Packet create | Packet duration (ms) | High frequency |

### Pool Overflow Behavior

| Pool | Size | Overflow Strategy |
|------|------|-------------------|
| `_known_destinations_pool` | 192 | Cull oldest by timestamp |
| `_known_ratchets_pool` | 128 | Cull oldest by timestamp |
| `_incoming_resources` | 8 | Reject new transfers |
| `_outgoing_resources` | 8 | Reject new transfers |
| `_pending_requests` | 8 | Reject new requests |
| `_rx_ring_pool` | 16 | Reject new messages (flow control) |
| `_tx_ring_pool` | 16 | Block sending (flow control) |

---

## Issues by Severity

### Medium Priority

**M1. new/shared_ptr pattern throughout codebase**
- **Files:** Identity.cpp, Destination.cpp, Link.cpp, Resource.cpp, Packet.cpp, Reticulum.cpp
- **Impact:** 14 allocation sites use 2 allocations instead of 1
- **Recommendation:** Convert to `make_shared` where appropriate
- **Estimate:** ~560 bytes savings per session (14 sites x 40 bytes)

**M2. Resource vectors can grow unbounded during large transfers**
- **File:** ResourceData.h:59, 68
- **Impact:** `_parts` and `_hashmap` resize during transfer
- **Recommendation:** Pre-allocate with maximum expected size or use fixed pools
- **Estimate:** Fragmentation risk during multi-MB file transfers

### Low Priority

**L1. std::map in ChannelData for message factories**
- **File:** ChannelData.h:347
- **Impact:** Tree nodes allocated per registration
- **Frequency:** One-time at channel setup
- **Recommendation:** Acceptable for now, could use fixed array if needed

**L2. std::vector for message callbacks**
- **File:** ChannelData.h:350
- **Impact:** Vector resize on registration
- **Frequency:** One-time at channel setup
- **Recommendation:** Acceptable, could use fixed array

---

## Recommendations for Phase 5

### High Impact (Implement First)

1. **Convert `new T()` to `make_shared<T>()`** for core session objects:
   - Identity::Object
   - Destination::Object
   - LinkData
   - ResourceData

   Saves ~40 bytes per object, reduces fragmentation.

2. **Pre-allocate Resource vectors** with maximum part count:
   ```cpp
   // Instead of resize, reserve maximum expected
   static constexpr size_t MAX_PARTS = 256;  // For typical transfer sizes
   _parts.reserve(MAX_PARTS);
   _hashmap.reserve(MAX_PARTS);
   ```

### Medium Impact

3. **Consider fixed-size Bytes pool** for common sizes (32, 64, 256 bytes)
   - Bytes.cpp:12 shows FIXME comment already noting this

4. **Add PSRAM placement for ResourceData** when handling large transfers

### Low Impact (Documentation/Style)

5. **Add FIXME(frag) comments** to remaining dynamic allocation sites
6. **Document pool sizes** in configuration for tuning

---

## Files Modified

No source files were modified during this audit phase. This was a documentation/analysis task.

**Files Audited:**
- src/Identity.cpp (1033 lines)
- src/Identity.h (306 lines)
- src/Destination.cpp (719 lines)
- src/Destination.h (documented in prior review)
- src/Link.cpp (1952+ lines, partial review)
- src/Link.h (documented in prior review)
- src/LinkData.h (147 lines)
- src/Channel.cpp (documented in prior review)
- src/Channel.h (121 lines)
- src/ChannelData.h (369 lines)
- src/Resource.cpp (1897 lines)
- src/Resource.h (199 lines)
- src/ResourceData.h (129 lines)
- src/Buffer.cpp (405 lines)
- src/Cryptography/Ed25519.h
- src/Cryptography/X25519.h
- src/Cryptography/HMAC.h

---

## Appendix: Code References

### PSRAM Allocation Example (Identity.cpp:43)
```cpp
_known_destinations_pool = (KnownDestinationSlot*)heap_caps_aligned_alloc(
    8, pool_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

### Fixed Pool Example (LinkData.h:101-111)
```cpp
static constexpr size_t INCOMING_RESOURCES_SIZE = 8;
Resource _incoming_resources[INCOMING_RESOURCES_SIZE];
```

### make_shared Usage (Buffer.cpp:152)
```cpp
: _object(std::make_shared<RawChannelReaderData>(stream_id, channel))
```

### new/shared_ptr Pattern (Identity.cpp:133)
```cpp
Identity::Identity(bool create_keys) : _object(new Object())
```
