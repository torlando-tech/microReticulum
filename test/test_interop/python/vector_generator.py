#!/usr/bin/env python3
"""
Generate test vectors for microReticulum C++ interoperability testing.
Produces known-good HKDF and Token outputs from Python RNS.

Usage:
    python vector_generator.py > ../vectors/test_vectors.h
"""

import os
import sys

# Try to import RNS
try:
    import RNS
    from RNS.Cryptography import HKDF
    from RNS.Cryptography.Token import Token
except ImportError:
    print("Error: Python RNS not installed. Run: pip install rns", file=sys.stderr)
    sys.exit(1)


def generate_hkdf_vectors():
    """Generate HKDF test vectors with various inputs."""
    vectors = []

    # Test case 1: 32-byte output (AES-128-CBC key derivation)
    shared_secret = bytes.fromhex("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
    salt = bytes.fromhex("0123456789abcdef0123456789abcdef")  # 16-byte link_id

    # Python RNS HKDF API: hkdf(length, derive_from, salt, context)
    derived_32 = HKDF.hkdf(32, shared_secret, salt)
    vectors.append({
        "name": "hkdf_aes128",
        "input": shared_secret.hex(),
        "salt": salt.hex(),
        "output_length": 32,
        "output": derived_32.hex()
    })

    # Test case 2: 64-byte output (AES-256-CBC key derivation)
    derived_64 = HKDF.hkdf(64, shared_secret, salt)
    vectors.append({
        "name": "hkdf_aes256",
        "input": shared_secret.hex(),
        "salt": salt.hex(),
        "output_length": 64,
        "output": derived_64.hex()
    })

    # Test case 3: Different shared secret and salt
    shared_secret2 = bytes.fromhex("cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe")
    salt2 = bytes.fromhex("fedcba9876543210fedcba9876543210")

    derived_32_2 = HKDF.hkdf(32, shared_secret2, salt2)
    vectors.append({
        "name": "hkdf_aes128_alt",
        "input": shared_secret2.hex(),
        "salt": salt2.hex(),
        "output_length": 32,
        "output": derived_32_2.hex()
    })

    derived_64_2 = HKDF.hkdf(64, shared_secret2, salt2)
    vectors.append({
        "name": "hkdf_aes256_alt",
        "input": shared_secret2.hex(),
        "salt": salt2.hex(),
        "output_length": 64,
        "output": derived_64_2.hex()
    })

    return vectors


def generate_token_vectors():
    """Generate Token encryption/decryption test vectors."""
    vectors = []

    # Test case 1: AES-128-CBC (32-byte key)
    key_32 = bytes.fromhex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
    plaintext1 = b"Hello, microReticulum!"

    token_128 = Token(key_32)
    ciphertext1 = token_128.encrypt(plaintext1)

    vectors.append({
        "name": "token_aes128_encrypt",
        "key": key_32.hex(),
        "plaintext": plaintext1.hex(),
        "ciphertext": ciphertext1.hex(),
        "note": "AES-128-CBC with 32-byte key"
    })

    # Test case 2: AES-256-CBC (64-byte key)
    key_64 = bytes.fromhex(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"
    )
    plaintext2 = b"Testing AES-256-CBC mode encryption"

    token_256 = Token(key_64)
    ciphertext2 = token_256.encrypt(plaintext2)

    vectors.append({
        "name": "token_aes256_encrypt",
        "key": key_64.hex(),
        "plaintext": plaintext2.hex(),
        "ciphertext": ciphertext2.hex(),
        "note": "AES-256-CBC with 64-byte key"
    })

    # Test case 3: Empty plaintext
    plaintext3 = b""
    ciphertext3 = token_128.encrypt(plaintext3)

    vectors.append({
        "name": "token_empty_plaintext",
        "key": key_32.hex(),
        "plaintext": plaintext3.hex(),
        "ciphertext": ciphertext3.hex(),
        "note": "Empty plaintext test"
    })

    # Test case 4: Binary data
    plaintext4 = bytes(range(256))
    ciphertext4 = token_256.encrypt(plaintext4)

    vectors.append({
        "name": "token_binary_data",
        "key": key_64.hex(),
        "plaintext": plaintext4.hex(),
        "ciphertext": ciphertext4.hex(),
        "note": "Binary data 0x00-0xFF"
    })

    return vectors


def generate_cpp_header(hkdf_vectors, token_vectors):
    """Generate C++ header file with test vectors."""

    output = """// Auto-generated test vectors from Python RNS
// Do not edit manually - regenerate with: python vector_generator.py
#pragma once

#include <Bytes.h>

namespace TestVectors {

// HKDF Test Vectors
// These validate that microReticulum HKDF output matches Python RNS
"""

    for v in hkdf_vectors:
        output += f"""
struct HKDF_{v['name']} {{
    static constexpr const char* name = "{v['name']}";
    static constexpr const char* input_hex = "{v['input']}";
    static constexpr const char* salt_hex = "{v['salt']}";
    static constexpr size_t output_length = {v['output_length']};
    static constexpr const char* expected_hex = "{v['output']}";
}};
"""

    output += """
// Token Test Vectors
// These validate Token encrypt/decrypt compatibility with Python RNS
// Note: encrypt() uses random IV, so we can only test decrypt() against Python ciphertext
"""

    for v in token_vectors:
        output += f"""
struct Token_{v['name']} {{
    static constexpr const char* name = "{v['name']}";
    static constexpr const char* key_hex = "{v['key']}";
    static constexpr const char* plaintext_hex = "{v['plaintext']}";
    static constexpr const char* ciphertext_hex = "{v['ciphertext']}";
    // {v['note']}
}};
"""

    output += """
} // namespace TestVectors
"""
    return output


def main():
    print(f"// Generating test vectors using Python RNS version {RNS.__version__}", file=sys.stderr)

    hkdf_vectors = generate_hkdf_vectors()
    print(f"// Generated {len(hkdf_vectors)} HKDF vectors", file=sys.stderr)

    token_vectors = generate_token_vectors()
    print(f"// Generated {len(token_vectors)} Token vectors", file=sys.stderr)

    header = generate_cpp_header(hkdf_vectors, token_vectors)
    print(header)

    print("// Vector generation complete", file=sys.stderr)


if __name__ == "__main__":
    main()
