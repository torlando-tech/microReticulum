# Codebase Concerns

**Analysis Date:** 2026-01-23

## Tech Debt

### Packet and Link Reference Ambiguity
- Issue: Unclear distinction between using `_link` (assigned late by Transport) vs `_destination_link` (assigned in constructor). Potential for null dereferences or stale references.
- Files: `src/Packet.h` (line 283), `src/Link.cpp` (line 290)
- Impact: Logic errors in packet routing, potential crashes if wrong reference is used
- Fix approach: Consolidate into single reference, update Transport's late-binding pattern to initialize at construction time

### Hardcoded Proof Strategy
- Issue: Packet proofs and link proofs hardcoded as explicit proofs instead of configurable
- Files: `src/Packet.cpp` (lines 851, 885), `src/Link.cpp` (lines 304)
- Impact: No flexibility for proof-of-concept modes or optimization; blocks future protocol variations
- Fix approach: Implement configurable proof strategy system with factory pattern

### Buffer Management Patterns
- Issue: AES operations marked "EXPERIMENTAL - overwrites passed buffer" with unclear mutation contracts
- Files: `src/Cryptography/AES.h` (lines 32, 40, 71, 79)
- Impact: Risk of data corruption if buffers are unexpectedly modified; callers may not expect in-place mutations
- Fix approach: Clarify and document mutation contracts; consider separate in-place vs output-buffer variants

### Msgpack Serialization Optimization Placeholders
- Issue: Multiple TODOs regarding inefficient msgpack serialization, particularly in Link packet handling
- Files: `src/Link.cpp` (lines 962, 1121)
- Impact: Excess memory allocations during message encoding; performance degradation on resource-constrained devices
- Fix approach: Implement streaming msgpack encoder to avoid intermediate buffering

### Persistence Serialization Order
- Issue: Packet serialization happens before destination table serialization, rendering packets useless on reload
- Files: `src/Utilities/Persistence.h` (lines 294, 395)
- Impact: Packets cannot be reconstructed from persisted state; violates dependency ordering
- Fix approach: Reorder serialization to persist destination table first, then packets with table references

### Stream-based Persistence
- Issue: Large structures buffered entirely in memory before serialization instead of streaming
- Files: `src/Utilities/Persistence.h` (lines 595, 638)
- Impact: Memory spikes when persisting large routing tables or message stores; fails on memory-constrained systems
- Fix approach: Implement streaming serialization to JSON for large containers

## Known Bugs

### NimBLE Dual-Mode Stability Issues
- Symptoms: Connection establishment failures with error codes rc=22 (BLE_HS_ENOTSYNCED) and rc=574 (connection establishment failure)
- Files: `src/BLE/platforms/NimBLEPlatform.cpp` (line 1033), `src/BLE/platforms/NimBLEPlatform.h` (lines 251, 255)
- Trigger: Running BLE in dual-role (central + peripheral) mode, especially under WiFi coexistence
- Workaround: State machine tracks rc=574 occurrences and triggers host reset after threshold (3 consecutive failures)
- Status: Partial fix in place but not fully resolved; may still occur under high load

### T-Deck Touch Controller Interrupt Crashes
- Symptoms: System crashes when using interrupt-driven touch handling
- Files: `src/Hardware/TDeck/Config.h` (line 111), `src/Hardware/TDeck/Touch.h` (line 20)
- Trigger: Any attempt to enable interrupt mode on touch controller
- Workaround: Hardcoded polling mode only; polling interval set conservatively to avoid missing events
- Status: Root cause unknown; interrupts permanently disabled

### Empty Payload Msgpack Crash
- Symptoms: Msgpack encoder crashes with empty payload
- Files: `src/LXMF/LXMessage.cpp` (line 253)
- Trigger: Creating LXMF message with no message body
- Workaround: Manual check added to prevent msgpack processing of empty payloads
- Status: Workaround in place but root cause in underlying msgpack library not addressed

## Security Considerations

### Identity Serialization to JSON
- Risk: Identities stored in plaintext JSON without encryption; private keys exposed if filesystem compromised
- Files: `src/Identity.cpp` (lines 995-1004)
- Current mitigation: JSON persistence not implemented; currently disabled
- Recommendations:
  1. Implement encrypted JSON storage using AES-256 with filesystem key
  2. Set restrictive file permissions (0600) on identity files
  3. Consider using secure enclave if available on platform

### Ratchet State Persistence
- Risk: Forward secrecy lost if ratchet state not persisted or persisted incorrectly
- Files: `src/Destination.cpp` (lines 548, 619)
- Current mitigation: Ratchet persistence not implemented; ratchets regenerated on startup (loses history)
- Recommendations:
  1. Implement secure ratchet state persistence
  2. Validate ratchet sequence numbers on load to detect tampering
  3. Add ratchet state versioning for protocol updates

### Packet Plaintext Fallback
- Risk: Code returns plaintext for PROOF packets; potential information leak
- Files: `src/Destination.cpp` (line 460)
- Current mitigation: Marked as TODO; decision deferred
- Recommendations: Audit all code paths that handle PROOF packets to ensure no sensitive data in plaintext

## Performance Bottlenecks

### Transport Destination Table Linear Search
- Problem: Path lookups perform linear scan of destination table
- Files: `src/Transport.h` (line 739)
- Cause: Current `std::set` enforces uniqueness but doesn't support O(1) lookups by hash
- Improvement path: Migrate to `std::map<hash, DestinationEntry>` for O(log n) or hash-based container for O(1)
- Impact: Lookup time scales with number of known destinations; critical for path finding performance

### Router Registry Linear Search
- Problem: Router registry uses linear pool search (16 slots max)
- Files: `src/LXMF/LXMRouter.cpp` (lines 41-57)
- Cause: Fixed-size pool to avoid heap fragmentation, but no fast lookup
- Improvement path: Add hash-based quick lookup cache if needed
- Current impact: 16-router maximum acceptable for typical deployments, but linear scan on every message

### LVGL and BLE Event Loop Blocking
- Problem: LVGL and BLE run on main event loop; long operations block both
- Files: `src/UI/LVGL/LVGLInit.cpp`, `src/BLE/platforms/NimBLEPlatform.cpp`
- Cause: Recent change (commit 2d7a999) separated tasks, but synchronization overhead remains
- Impact: UI responsiveness suffers during heavy BLE scanning/connecting
- Improvement path: Implement event-driven architecture with minimal synchronization points

### Exponential RSSI Averaging
- Problem: BLE RSSI calculation uses fixed coefficients (0.7/0.3) not adaptive to signal quality
- Files: `src/BLE/BLEPeerManager.cpp` (line 67)
- Impact: Poor signal tracking in noisy environments
- Improvement path: Implement adaptive smoothing based on recent variance

## Fragile Areas

### Memory Exhaustion Under Dual-Role BLE
- Files: `src/BLE/BLEPeerManager.h`, `src/BLE/BLEPlatform.h`, `src/BLE/platforms/NimBLEPlatform.cpp`
- Why fragile:
  - Fixed pool sizes (PEERS_POOL_SIZE, MAC_IDENTITY_POOL_SIZE) hard-coded without overflow protection
  - Dynamic peer discovery can exceed pool capacity
  - NimBLE internal state machine has multiple failure modes under simultaneous scanning/advertising
- Safe modification:
  - Add pool exhaustion detection and graceful degradation
  - Implement LRU eviction policy for peer pool overflow
  - Add assertions on pool slot allocation
- Test coverage: Limited; mostly integration testing, no unit tests for pool exhaustion scenarios

### Transport Destination Table Memory Leaks
- Files: `src/Transport.cpp`, `src/Transport.h`
- Why fragile:
  - Manual culling logic spread across multiple functions
  - "Deprecated paths" concept (Transport.cpp lines 3371-3404) uses temporary lists during iteration
  - Path expiration timing heuristics may not catch all stale entries
- Safe modification: Add comprehensive audit of all path lifecycle (creation -> expiration -> removal)
- Test coverage: No automated tests for path table memory behavior over time

### Fixed Pool Ordering Assumptions
- Files: `src/LXMF/LXMRouter.cpp` (lines 14-100), `src/BLE/BLEIdentityManager.cpp`
- Why fragile: Multiple files assume fixed pool slot ordering; reordering or resizing breaks assumptions
- Safe modification:
  - Convert pool slots to handle-based references
  - Document pool lifecycle assumptions
  - Add consistency checks in debug builds
- Test coverage: None for pool ordering assumptions

### Link State Machine Complexity
- Files: `src/Link.cpp` (850+ lines of state transitions)
- Why fragile:
  - Multiple TODOs about unclear design patterns (lines 236, 290, 304, 333, 750, 905, 1274, 1309)
  - Potential packet reference issues if Transport late-binding fails
  - Resource timeouts hardcoded without external configuration
- Safe modification:
  - Extract state machine to formal model with exhaustive case coverage
  - Add comprehensive logging of state transitions for debugging
  - Implement watchdog (mentioned in TODO at line 750) for hung links
- Test coverage: Limited; link establishment tested but not all edge cases

## Scaling Limits

### Router Registry Fixed Capacity
- Current capacity: 4 simultaneous routers (ROUTER_REGISTRY_SIZE)
- Limit: Applications with >4 concurrent LXMF routers will fail to register
- Scaling path: Increase pool size or implement dynamic routing with overflow handling

### Outbound Resources Fixed Capacity
- Current capacity: 16 simultaneous outbound resources (OUTBOUND_RESOURCES_SIZE)
- Limit: Large batch transfers or heavy messaging will exhaust pool
- Scaling path: Implement multi-tiered pool with PSRAM fallback

### MAC Identity Mapping Pool
- Current capacity: MAC_IDENTITY_POOL_SIZE (value not defined but appears limited)
- Limit: In dense deployments with many discoverable peers, will hit ceiling
- Scaling path: Implement LRU eviction with persistence layer

### Maximum Concurrent BLE Connections
- Current capacity: Limited by handle pool (appears to be 20-30 based on NimBLE)
- Limit: Cannot connect to >N peers simultaneously
- Scaling path: Implement connection queuing and dynamic peer prioritization

## Dependencies at Risk

### NimBLE Stack (Arduino)
- Risk: Upstream library stability issues (dual-role crashes, memory leaks); no alternative implementation without major refactoring
- Impact: Can't upgrade without extensive regression testing; can't downgrade to fix behavioral regressions
- Migration plan: Maintain fork with critical patches; plan alternative BLE stack (ZephyrBLE, custom) for long-term

### Bluedroid Stack (Bluedroid platform)
- Risk: Deprecated in favor of NimBLE upstream; receiving fewer updates
- Impact: Security fixes may lag; bugs in Bluedroid won't be addressed
- Migration plan: T-Deck already uses NimBLE; deprecate Bluedroid support in future release

### ArduinoJson (External Library)
- Risk: JsonVariant ambiguity noted in code; serialization performance suboptimal
- Impact: Blocking msgpack optimization; unclear when Bytes/JsonVariant conversions happen
- Recommendations: Profile JSON usage; consider msgpack-only serialization to bypass ArduinoJson

### Custom Memory Allocators (tlsf.h)
- Risk: tlsf allocator not well-tested on all platforms; behavior under fragmentation unknown
- Impact: Memory exhaustion handling may fail; hard to debug allocation failures
- Recommendations: Add instrumentation to allocator; profile fragmentation over time

## Missing Critical Features

### Resource Watchdog Timeout Handler
- Problem: Links can hang indefinitely waiting for resources without automatic cleanup
- Blocks: Reliable long-running operation; blocks robustness for mesh networks
- Files: `src/Link.cpp` (line 750), `src/Resource.cpp` (lines 1256-1267)
- Priority: High - affects network reliability

### Link Identify/Request Protocol Methods
- Problem: LXMF sync protocol incomplete; can't fully synchronize link state
- Blocks: Robust peer synchronization; message delivery guarantees
- Files: `src/LXMF/LXMRouter.cpp` (lines 1543, 1583, 1588)
- Priority: High - required for reliable message delivery

### Unread Message Tracking
- Problem: No persistent unread state; UI always shows 0 unread
- Blocks: User experience; message prioritization
- Files: `src/UI/LXMF/UIManager.cpp` (line 591), `src/UI/LXMF/ConversationListScreen.cpp` (line 250)
- Priority: Medium - UX feature, not critical to networking

### Configuration Callbacks for Resources
- Problem: Resource strategy callbacks not implemented; no application-defined transfer policies
- Blocks: Application-specific resource optimization
- Files: `src/Link.cpp` (line 1274), `src/Link.cpp` (line 1309)
- Priority: Low - current hardcoded strategy sufficient for most use cases

## Test Coverage Gaps

### BLE Pool Exhaustion
- What's not tested: Behavior when peer pools exceed capacity; overflow handling
- Files: `src/BLE/BLEPeerManager.cpp`
- Risk: Silent failures or crashes under peer density load
- Priority: High

### Transport Path Table Expiration
- What's not tested: All path lifecycle scenarios; edge cases in expiration logic
- Files: `src/Transport.cpp` (3000+ lines of path management)
- Risk: Memory leaks or stale path entries affecting routing
- Priority: High

### NimBLE Dual-Mode Recovery
- What's not tested: Systematic testing of rc=22 and rc=574 recovery paths
- Files: `src/BLE/platforms/NimBLEPlatform.cpp`
- Risk: Incomplete recovery may leave system in inconsistent state
- Priority: High

### LVGL Task Synchronization
- What's not tested: Race conditions between LVGL and BLE task updates
- Files: `src/UI/LVGL/LVGLInit.cpp`
- Risk: Mutex deadlock or missed UI updates
- Priority: Medium

### Link State Machine Edge Cases
- What's not tested: Packet loss, reordering, timing edge cases in Link state machine
- Files: `src/Link.cpp`
- Risk: Stalled connections or resource leaks
- Priority: Medium

### Persistence Data Corruption
- What's not tested: Recovery from corrupted JSON/binary state files
- Files: `src/Utilities/Persistence.h`
- Risk: Cascading failures on load; no validation of persisted structures
- Priority: Medium

---

*Concerns audit: 2026-01-23*
