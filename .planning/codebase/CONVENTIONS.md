# Coding Conventions

**Analysis Date:** 2026-01-23

## Naming Patterns

**Files:**
- Header files: `.h` extension (e.g., `Fernet.h`, `HMAC.h`)
- Implementation files: `.cpp` extension (e.g., `Fernet.cpp`, `Identity.cpp`)
- Class/Type names: PascalCase (e.g., `Fernet`, `X25519PrivateKey`, `PacketReceipt`)
- Namespace grouping: Related functionality grouped in subdirectories (e.g., `Cryptography/`, `BLE/`, `LXMF/`)

**Functions:**
- Public methods: snake_case (e.g., `verify_hmac()`, `public_bytes()`, `generate_key()`)
- Private methods: snake_case with leading underscore (e.g., `_verify_hmac()`)
- Static factory methods: PascalCase prefix with `from_` (e.g., `from_public_bytes()`)
- Camel case for some internal helpers: `isTimedOut()` style mixing observed
- Test functions: `void test[FunctionName]()` pattern (e.g., `testHMAC()`, `testBytesMain()`)

**Variables:**
- Private member variables: leading underscore with snake_case (e.g., `_signing_key`, `_encryption_key`, `_publicKey`)
- Constants: UPPER_SNAKE_CASE or const_name pattern (e.g., `BLOCKSIZE`, `KNOWN_DESTINATIONS_SIZE`, `MIN_EXEC_TIME`)
- Local variables: snake_case (e.g., `received_hmac`, `plaintext`, `ciphertext`)
- Enum values: Upper case (e.g., `DIGEST_SHA256`, `LOG_CRITICAL`)

**Types:**
- Classes: PascalCase
- Namespaces: snake_case with multi-level organization
  - Primary namespace: `RNS` (Reticulum Network Stack)
  - Sub-namespaces: `RNS::Cryptography`, `RNS::Utilities`, `RNS::Type`
- Typedef aliases for shared_ptr: `.Ptr` suffix (e.g., `using Ptr = std::shared_ptr<Fernet>`)
- Enum classes: PascalCase names, UPPER_CASE values

## Code Style

**Formatting:**
- No automated formatter detected (no .clang-format, .editorconfig)
- Brace style: Allman (opening braces on new line for class/function definitions)
- Indentation: Tabs or spaces (inconsistent, appears to be tab-based)
- Line length: Appears to target ~100-120 characters
- One statement per line

**Linting:**
- Tool: Compiler flags from PlatformIO
- Build flags: `-Wall -Wno-missing-field-initializers -Wno-format -Wno-unused-parameter`
- C++ standard: C++11 default, with C++17 and C++20 variants available
- Disabled warnings indicate tolerance for:
  - Missing initializers in aggregates
  - Format string issues
  - Unused function parameters (common in virtual/callback patterns)

## Import Organization

**Order:**
1. Standard library headers: `#include <stdint.h>`, `#include <memory>`, `#include <string>`
2. Third-party headers: `#include <ArduinoJson.h>`, `#include <AES.h>`, `#include <Ed25519.h>`
3. Local headers: `#include "Bytes.h"`, `#include "Log.h"`
4. Relative local headers: `#include "../Bytes.h"`, `#include "../Log.h"`

**Header guards:**
- Pragma once: `#pragma once` (modern, preferred style throughout codebase)
- Traditional guards: Not used in this project

**Path Aliases:**
- Build flag: `-Isrc` enables includes from `src/` root
- Allows includes like `#include "Cryptography/Fernet.h"` from anywhere
- No namespace aliases observed; full qualification preferred

## Error Handling

**Patterns:**
- C++ exceptions: `std::invalid_argument`, `std::runtime_error` commonly used
- Constructor validation: Throw exceptions for invalid parameters (e.g., Fernet.cpp line 16-22)
- Example:
  ```cpp
  if (!key) {
      throw std::invalid_argument("Fernet key cannot be None");
  }
  if (key.size() != 32) {
      throw std::invalid_argument("Fernet key must be 32 bytes, not " + std::to_string(key.size()));
  }
  ```
- Cryptographic operations: Throws on HMAC verification failure
- Decryption failures: Wrapped with try-catch, then re-thrown with simplified message (Fernet.cpp line 98-115)
- Assertions: `assert()` used for internal invariants in header-only classes (e.g., FileStream.h, HMAC.h)

## Logging

**Framework:** Custom macro-based system in `Log.h`

**Levels (in order):**
- `CRITICAL()` / `CRITICALF()`
- `ERROR()` / `ERRORF()`
- `WARNING()` / `WARNINGF()`
- `NOTICE()` / `NOTICEF()`
- `INFO()` / `INFOF()`
- `VERBOSE()` / `VERBOSEF()`
- `DEBUG()` / `DEBUGF()` (compiled out with `NDEBUG`)
- `TRACE()` / `TRACEF()` (compiled out with `NDEBUG`)
- `MEM()` / `MEMF()` (requires `MEM_LOG` define, for memory allocation tracking)

**Patterns:**
- Use macros (not functions) for logging at all levels
- Format string version with `F` suffix: `DEBUGF("Message: %s", value.c_str())`
- All logging calls use single message string or format pattern
- Example from Fernet.cpp:
  ```cpp
  DEBUG("Fernet::encrypt: plaintext length: " + std::to_string(data.size()));
  TRACE("Fernet::encrypt: plaintext:  " + data.toHex());
  MEM("Fernet object created");
  ```
- `HEAD()` function for header messages (different formatting)
- Compile-time stripping: DEBUG/TRACE/MEM are no-ops when not in debug mode

## Comments

**When to Comment:**
- Algorithm explanations: When implementing complex cryptographic operations
- Non-obvious design decisions: e.g., "CBA" prefix comments on implementation notes
- References to external specs or standards: e.g., "This class provides a slightly modified implementation of the Fernet spec found at: https://github.com/fernet/spec/blob/master/Spec.md"
- Workarounds and known limitations: "CBA Can't use virtual methods because they are lost in object copies" (Packet.h line 25)

**JSDoc/TSDoc:**
- Not used; this is C++, not TypeScript/JavaScript
- API documentation in class docstrings: Python-style doc comments for public interfaces (e.g., Packet.h PacketReceipt class)
- Multi-line comments: Standard C++ style `/* ... */`

**Comment prefix conventions:**
- `CBA`: Developer initials used for design comments/notes
- `TODO`: Used for incomplete features (e.g., "TODO: Register resource with link")
- `FIXME`: Not observed; mostly TODO style
- `HACK`: Not observed in current code

## Function Design

**Size:**
- Methods typically range from 5-50 lines
- Larger implementations in .cpp files: Transport.cpp (5214 lines total), Link.cpp (2188 lines)
- Functions tend to be focused on single responsibility

**Parameters:**
- Prefer const references: `const Bytes& key`, `const Bytes& data`
- Use return values for output, not out-parameters (modern C++ style)
- Optional parameters: Not commonly used; overloading preferred instead
- Example: Multiple `validate_proof()` overloads with different signatures (Packet.h lines 71-74)

**Return Values:**
- Return by value for Bytes objects: `const Bytes encrypt(const Bytes& data)`
- Use bool for success/failure: `bool verify_hmac(const Bytes& token)`
- Static factory methods return shared_ptr: `static inline Ptr from_public_bytes(const Bytes& data)`
- Non-nullable pointers use assertions, not checks: `inline void close() { assert(_impl); _impl->close(); }`

## Module Design

**Exports:**
- Header files define complete interface; implementation in .cpp
- All public symbols in namespace `RNS` or sub-namespace
- Private implementation details in unnamed namespaces or private sections
- Example structure: `Fernet.h` declares class, `Fernet.cpp` implements with `using namespace RNS::Cryptography`

**Shared Pointer Idiom:**
- Factory pattern: Static `from_*()` methods return `std::shared_ptr<T>`
- Used extensively for cryptographic key objects (Ed25519, X25519, HMAC)
- Definition: `using Ptr = std::shared_ptr<ClassName>;` at class top level
- Enables implicit object sharing pattern (mentioned in README as API design goal)

**Copy-on-Write Pattern:**
- Bytes class implements COW via shared_ptr to underlying data
- Enabled by build flag: `-DCOW` in platformio.ini
- Reduces memory pressure on embedded systems
- Single Ptr member per class is common pattern

---

*Convention analysis: 2026-01-23*
