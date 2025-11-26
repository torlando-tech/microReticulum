// Auto-generated test vectors from Python RNS
// Do not edit manually - regenerate with: python vector_generator.py
#pragma once

#include <Bytes.h>

namespace TestVectors {

// HKDF Test Vectors
// These validate that microReticulum HKDF output matches Python RNS

struct HKDF_hkdf_aes128 {
    static constexpr const char* name = "hkdf_aes128";
    static constexpr const char* input_hex = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    static constexpr const char* salt_hex = "0123456789abcdef0123456789abcdef";
    static constexpr size_t output_length = 32;
    static constexpr const char* expected_hex = "a02420d9943fba1b5b3c39c16cfc4a83b94c315baa6df7f7e417220164dcee81";
};

struct HKDF_hkdf_aes256 {
    static constexpr const char* name = "hkdf_aes256";
    static constexpr const char* input_hex = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    static constexpr const char* salt_hex = "0123456789abcdef0123456789abcdef";
    static constexpr size_t output_length = 64;
    static constexpr const char* expected_hex = "a02420d9943fba1b5b3c39c16cfc4a83b94c315baa6df7f7e417220164dcee81247a1781049961dab54803827b948bc7f87ec8bee96b3ea49afed8568cea767f";
};

struct HKDF_hkdf_aes128_alt {
    static constexpr const char* name = "hkdf_aes128_alt";
    static constexpr const char* input_hex = "cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe";
    static constexpr const char* salt_hex = "fedcba9876543210fedcba9876543210";
    static constexpr size_t output_length = 32;
    static constexpr const char* expected_hex = "e3ad63b8a717cc76a1df439007492ea5ba25f5167282965b92dc9f12ca5ceadb";
};

struct HKDF_hkdf_aes256_alt {
    static constexpr const char* name = "hkdf_aes256_alt";
    static constexpr const char* input_hex = "cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe";
    static constexpr const char* salt_hex = "fedcba9876543210fedcba9876543210";
    static constexpr size_t output_length = 64;
    static constexpr const char* expected_hex = "e3ad63b8a717cc76a1df439007492ea5ba25f5167282965b92dc9f12ca5ceadb4aa649d76ac490f5de3a1623cb18346500f4ad593be5d3608da0673cdf46f1c0";
};

// Token Test Vectors
// These validate Token encrypt/decrypt compatibility with Python RNS
// Note: encrypt() uses random IV, so we can only test decrypt() against Python ciphertext

struct Token_token_aes128_encrypt {
    static constexpr const char* name = "token_aes128_encrypt";
    static constexpr const char* key_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    static constexpr const char* plaintext_hex = "48656c6c6f2c206d6963726f5265746963756c756d21";
    static constexpr const char* ciphertext_hex = "d31a417f278c0c533d40bc0cbd628afeca2d4dcc0dd82c45bed096c74a43f21184d90e75c9577ecc3fe019cf4bf427532019e1acc39c3a960b379e3ad51dfb8b8a919a90aad0be1d6ce6ac827a0e14f8";
    // AES-128-CBC with 32-byte key
};

struct Token_token_aes256_encrypt {
    static constexpr const char* name = "token_aes256_encrypt";
    static constexpr const char* key_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeffedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    static constexpr const char* plaintext_hex = "54657374696e67204145532d3235362d434243206d6f646520656e6372797074696f6e";
    static constexpr const char* ciphertext_hex = "0d6aa184435f5420bb9cc0641f7d59949fa88a3a694328c86acc4c01a2762fbad7cd86327f5afe0f3fbbd0d58ad87691caa86e3ff16ad9bc34143b16ca75f1a5dba597faf784db3a98b95d7602f2c8c727f2565c8f93e65020f88e7ec9d79cc2";
    // AES-256-CBC with 64-byte key
};

struct Token_token_empty_plaintext {
    static constexpr const char* name = "token_empty_plaintext";
    static constexpr const char* key_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    static constexpr const char* plaintext_hex = "";
    static constexpr const char* ciphertext_hex = "1d932a97cb3800997289ced8ff6cdb0b2cd14ac5fa61f629f7c80b67aef650b6388ccd3bf07bd136839b980cabfac28351048dbbe737c63955f84d4881b02931";
    // Empty plaintext test
};

struct Token_token_binary_data {
    static constexpr const char* name = "token_binary_data";
    static constexpr const char* key_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeffedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    static constexpr const char* plaintext_hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";
    static constexpr const char* ciphertext_hex = "378a1fd828e13ecf02f04c29320aaaaf10fdf4fbb78061edaa447703fa6f9987008d3777bad560058545f39b206f6dca6f095339bc6622bc28e31374eb4baa5dd74b454eaf7a7b908cf2122ad2f2700be673b120f4f4652de98b63acd6a1999481617e3d1b0f6fb0eb9fbab167dc431886dbebb06ed136fc767f076ed1116b7c985e0c580727e57d8a620e76fc8551adbfdf9c667505f199288827a6ecbde582ce818e3bfea8eb3d73dc58e7bdf2aeff13d6facd4592d85342f70415411200931e8e8329bc26efce4d44649a6a02d94dd8fbff4b9d589d0aa76a0e3b7dac91feafe5330c9485fcf8cf2e56fe4ca071711ef6942c43aec22b3774d89522940f5d7ca5ed2a685bdf2616f2781d7f517e3d64cd005703908e10cdd7ba6683ce7b0b48fd8dbba32ec9bab12e13bb37debf4be0862029c1e0ec7ed7c63f4fc5c25978";
    // Binary data 0x00-0xFF
};

} // namespace TestVectors

