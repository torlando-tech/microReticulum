# Testing Patterns

**Analysis Date:** 2026-01-23

## Test Framework

**Runner:**
- Unity framework (embedded-friendly C/C++ unit testing)
- Configured in `platformio.ini`: `test_framework = unity`
- Option: `test_build_src = true` enables full source compilation in tests

**Assertion Library:**
- Unity assertions: `TEST_ASSERT_*` macros
- Standard assertions: `TEST_ASSERT_EQUAL_INT()`, `TEST_ASSERT_EQUAL_size_t()`, `TEST_ASSERT_EQUAL_STRING()`, `TEST_ASSERT_EQUAL_MEMORY()`
- Comparison: `TEST_ASSERT_TRUE()`, `TEST_ASSERT_FALSE()`
- Example from `test/test_crypto/test_main.cpp` line 38:
  ```cpp
  TEST_ASSERT_EQUAL_INT(0, memcmp(hash.data(), result.data(), result.size()));
  ```

**Run Commands:**
```bash
# Run all tests in a PlatformIO project
pio test -e native

# Run specific test
pio test -e native test_crypto

# Watch mode (if supported)
pio test -e native --tb=short
```

## Test File Organization

**Location:**
- Tests in `/test/` directory parallel to `/src/`
- Each test feature in own subdirectory: `test_crypto/`, `test_bytes/`, `test_collections/`, etc.
- Pattern: One test directory per feature/module
- Entry point: `test_main.cpp` in each test directory

**Naming:**
- Test directories: `test_[feature]` (e.g., `test_crypto`, `test_rns_loopback`, `test_tdeck`)
- Test functions: `void test[FeatureName]()`
- Setup/teardown: `void setUp(void)` and `void tearDown(void)` (required by Unity)
- Main runner: `int runUnityTests(void)` or `int main(void)`

**Structure:**
```
test/
├── README
├── common/
│   └── filesystem/
│       └── FileSystem.h
├── test_bytes/
│   └── test_main.cpp
├── test_crypto/
│   └── test_main.cpp
├── test_collections/
├── test_filesystem/
├── test_general/
├── test_interop/
├── test_lxmf/
├── test_msgpack/
├── test_objects/
│   ├── A.h, A.cpp
│   ├── AImpl.h, AImpl.cpp
│   ├── test_main.cpp
├── test_persistence/
└── test_rns_loopback/
```

## Test Structure

**Suite Organization:**
From `test/test_crypto/test_main.cpp`:
```cpp
void testHMAC() {
    // Test setup inline
    const char keystr[] = "key";
    const RNS::Bytes key(keystr);

    // Perform operations
    RNS::Cryptography::HMAC hmac(key, data);
    RNS::Bytes result = hmac.digest();

    // Assert results
    TEST_ASSERT_EQUAL_INT(0, memcmp(hash.data(), result.data(), result.size()));
}

void testPKCS7() {
    // Multiple test cases within single function
    {
        size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE / 2;
        RNS::Bytes bytes(str, len);
        bytes = RNS::Cryptography::PKCS7::pad(bytes);
        TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());
    }

    // Another test case
    {
        size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE - 1;
        // ... more assertions
    }
}

void setUp(void) {
    // Called before each test - currently empty in most tests
}

void tearDown(void) {
    // Called after each test - currently empty in most tests
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(testHMAC);
    RUN_TEST(testPKCS7);
    return UNITY_END();
}
```

**Patterns:**
- Setup/teardown: Defined but typically not used (empty implementations)
- Test grouping: Multiple related test cases in braces within single test function
- Flat structure: No test classes; global functions with test name prefix
- Main function: Different entry points for Arduino vs native
  ```cpp
  // For native dev-platform or for some embedded frameworks
  int main(void) {
      return runUnityTests();
  }

  #ifdef ARDUINO
  // For Arduino framework
  void setup() {
      delay(2000);  // Wait for serial connection
      runUnityTests();
  }
  void loop() {}
  #endif

  // For ESP-IDF framework
  void app_main() {
      runUnityTests();
  }
  ```

## Mocking

**Framework:**
- No external mocking library detected (gtest, gmock, etc.)
- Manual mocking using test objects and stubs
- Example in `test/test_objects/`: Test implementations A, B with separate interface/impl patterns

**Patterns:**
- Test-specific implementations: Create test versions of classes for testing
- Example from `test_objects/`:
  - Interface: `A.h` (abstract interface)
  - Implementation: `AImpl.h` / `AImpl.cpp` (actual implementation)
  - Test fixture: Also in test directory for testing the pattern
- Spy/stub pattern: Not observed
- No dependency injection framework; tests construct objects directly

**What to Mock:**
- External I/O operations: File system access mocked in `test_filesystem/`
- Timing operations: Custom time helpers in `test_general/test_main.cpp` (lines 11-37):
  ```cpp
  unsigned long test_millis() {
      timeval time;
      ::gettimeofday(&time, NULL);
      return (uint64_t)(time.tv_sec * 1000) + (uint64_t)(time.tv_usec / 1000);
  }
  ```
- Platform-specific behavior: Arduino vs native implementations in conditional blocks

**What NOT to Mock:**
- Core cryptographic operations: Always test with real crypto (security critical)
- Bytes class: Core data structure tested directly
- CRC calculations: Tested against known vectors
- HMAC/PKCS7: No mocking; real algorithms validated

## Fixtures and Factories

**Test Data:**
From `test/test_crypto/test_main.cpp` line 25-33:
```cpp
const uint8_t hasharr[] = {
    0xf7, 0xbc, 0x83, 0xf4, 0x30, 0x53, 0x84, 0x24,
    0xb1, 0x32, 0x98, 0xe6, 0xaa, 0x6f, 0xb1, 0x43,
    // ... more bytes
};
RNS::Bytes hash(hasharr, sizeof(hasharr));
```
- Known test vectors embedded in test files
- Reference values hardcoded (for HMAC, CRC tests against standard test vectors)
- No external fixture files (JSON, YAML, etc.)

**Location:**
- Inline in test functions: Most common pattern
- Shared constants: In test_main.cpp top-level or within specific test function
- Common test utilities: `test/common/filesystem/FileSystem.h` provides cross-platform FS test support
- Helper functions: Time utilities, memory tracking in test headers

## Coverage

**Requirements:**
- Not enforced; no coverage target configured in `platformio.ini`
- No coverage reports generated by default

**View Coverage:**
```bash
# Would require additional tools (gcov, lcov)
# Not currently configured in project
```

## Test Types

**Unit Tests:**
- Scope: Individual classes/functions tested in isolation
- Approach: Direct instantiation and method calls
- Examples:
  - `test_crypto`: Tests cryptographic primitives (HMAC, PKCS7, CRC)
  - `test_bytes`: Tests Bytes class operations (concatenation, slicing)
  - `test_collections`: Tests container implementations
- No mocking; all dependencies constructed
- Cryptographic tests validate against known test vectors (HMAC spec examples, CRC standards)

**Integration Tests:**
- Scope: Multiple modules working together
- Examples:
  - `test_rns_loopback`: Tests Reticulum network stack in loopback (local node communicating with itself)
  - `test_rns_persistence`: Tests persistence layer with actual transport
  - `test_msgpack`: Tests MessagePack serialization with Bytes class
- Some still unit-like (test_example is minimal example app)
- Use real filesystem if applicable

**E2E Tests:**
- Framework: Not observed in standard test suite
- Alternative: `test_interop/` directory may contain interop tests with Python reference implementation
- BLE tests: `test_ble/` directory (likely exercises real BLE interfaces on hardware)
- T-Deck tests: `test_tdeck/` directory for device-specific UI tests

## Common Patterns

**Async Testing:**
From `test/test_general/test_main.cpp`:
```cpp
uint64_t ltime() {
    // handle roll-over of 32-bit millis (approx. 49 days)
    static uint32_t low32, high32;
    uint32_t new_low32 = test_millis();
    if (new_low32 < low32) high32++;
    low32 = new_low32;
    return ((uint64_t)high32 << 32 | low32) + timeOffset;
}

void testTime() {
    uint64_t start = ltime();
    testsleep(0.1);  // Sleep function that works on Arduino and native
    uint64_t end = ltime();
    TEST_ASSERT_TRUE((end - start) > 50);
    TEST_ASSERT_TRUE((end - start) < 200);
}
```
- Custom time abstraction for cross-platform testing
- Sleep implemented separately for Arduino vs native platforms
- Timing assertions include tolerance windows (50-200ms for 100ms sleep)

**Error Testing:**
From `test/test_crypto/test_main.cpp` and cryptographic implementations:
```cpp
// Constructor tests exception handling
{
    const char keystr[] = "key";
    const RNS::Bytes key(keystr);
    RNS::Cryptography::HMAC hmac(key, data);
    RNS::Bytes result = hmac.digest();
    TEST_ASSERT_EQUAL_INT(0, memcmp(hash.data(), result.data(), result.size()));
}
```
- No explicit exception testing observed
- Validation happens in constructors and throws on failure
- Test assumes construction succeeds; no try-catch in tests
- Implicit assumption: Invalid parameters will fail at compile-time or during construction

**Memory Testing:**
From `src/Log.h` and code comments:
- `MEM()` logging macro tracks object creation/destruction
- Test output includes memory allocation traces: `"Bytes object created from chunk"`
- Not formalized; manual inspection of logs for memory leaks
- PSRAM allocation tracking in Identity.cpp (comments about heap management)

---

*Testing analysis: 2026-01-23*
