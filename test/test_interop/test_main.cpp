#include <unity.h>

#include <Bytes.h>
#include <Cryptography/HKDF.h>
#include <Cryptography/Token.h>
#include <Log.h>

#include "vectors/test_vectors.h"

using namespace RNS;
using namespace RNS::Cryptography;

// Helper to convert hex string to Bytes
Bytes fromHex(const char* hex) {
    Bytes b;
    b.assignHex(hex);
    return b;
}

//
// HKDF Test Cases
//

void test_hkdf_aes128() {
    using V = TestVectors::HKDF_hkdf_aes128;

    Bytes input = fromHex(V::input_hex);
    Bytes salt = fromHex(V::salt_hex);
    Bytes expected = fromHex(V::expected_hex);

    Bytes derived = hkdf(V::output_length, input, salt);

    TEST_ASSERT_EQUAL_UINT(V::output_length, derived.size());
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), derived.data(), V::output_length);
}

void test_hkdf_aes256() {
    using V = TestVectors::HKDF_hkdf_aes256;

    Bytes input = fromHex(V::input_hex);
    Bytes salt = fromHex(V::salt_hex);
    Bytes expected = fromHex(V::expected_hex);

    Bytes derived = hkdf(V::output_length, input, salt);

    TEST_ASSERT_EQUAL_UINT(V::output_length, derived.size());
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), derived.data(), V::output_length);
}

void test_hkdf_aes128_alt() {
    using V = TestVectors::HKDF_hkdf_aes128_alt;

    Bytes input = fromHex(V::input_hex);
    Bytes salt = fromHex(V::salt_hex);
    Bytes expected = fromHex(V::expected_hex);

    Bytes derived = hkdf(V::output_length, input, salt);

    TEST_ASSERT_EQUAL_UINT(V::output_length, derived.size());
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), derived.data(), V::output_length);
}

void test_hkdf_aes256_alt() {
    using V = TestVectors::HKDF_hkdf_aes256_alt;

    Bytes input = fromHex(V::input_hex);
    Bytes salt = fromHex(V::salt_hex);
    Bytes expected = fromHex(V::expected_hex);

    Bytes derived = hkdf(V::output_length, input, salt);

    TEST_ASSERT_EQUAL_UINT(V::output_length, derived.size());
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), derived.data(), V::output_length);
}

//
// Token Test Cases - Decrypt Python-encrypted ciphertext
//

void test_token_decrypt_aes128() {
    using V = TestVectors::Token_token_aes128_encrypt;

    Bytes key = fromHex(V::key_hex);
    Bytes expected_plaintext = fromHex(V::plaintext_hex);
    Bytes ciphertext = fromHex(V::ciphertext_hex);

    Token token(key);
    Bytes decrypted = token.decrypt(ciphertext);

    TEST_ASSERT_EQUAL_UINT(expected_plaintext.size(), decrypted.size());
    TEST_ASSERT_EQUAL_MEMORY(expected_plaintext.data(), decrypted.data(), expected_plaintext.size());
}

void test_token_decrypt_aes256() {
    using V = TestVectors::Token_token_aes256_encrypt;

    Bytes key = fromHex(V::key_hex);
    Bytes expected_plaintext = fromHex(V::plaintext_hex);
    Bytes ciphertext = fromHex(V::ciphertext_hex);

    Token token(key);
    Bytes decrypted = token.decrypt(ciphertext);

    TEST_ASSERT_EQUAL_UINT(expected_plaintext.size(), decrypted.size());
    TEST_ASSERT_EQUAL_MEMORY(expected_plaintext.data(), decrypted.data(), expected_plaintext.size());
}

void test_token_decrypt_empty() {
    using V = TestVectors::Token_token_empty_plaintext;

    Bytes key = fromHex(V::key_hex);
    Bytes expected_plaintext = fromHex(V::plaintext_hex);
    Bytes ciphertext = fromHex(V::ciphertext_hex);

    Token token(key);
    Bytes decrypted = token.decrypt(ciphertext);

    TEST_ASSERT_EQUAL_UINT(0, decrypted.size());
}

void test_token_decrypt_binary() {
    using V = TestVectors::Token_token_binary_data;

    Bytes key = fromHex(V::key_hex);
    Bytes expected_plaintext = fromHex(V::plaintext_hex);
    Bytes ciphertext = fromHex(V::ciphertext_hex);

    Token token(key);
    Bytes decrypted = token.decrypt(ciphertext);

    TEST_ASSERT_EQUAL_UINT(expected_plaintext.size(), decrypted.size());
    TEST_ASSERT_EQUAL_MEMORY(expected_plaintext.data(), decrypted.data(), expected_plaintext.size());
}

//
// Token Test Cases - Encrypt/decrypt roundtrip
//

void test_token_roundtrip_aes128() {
    Bytes key = fromHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    Bytes plaintext = fromHex("48656c6c6f20576f726c6421");  // "Hello World!"

    Token token(key);
    Bytes ciphertext = token.encrypt(plaintext);
    Bytes decrypted = token.decrypt(ciphertext);

    TEST_ASSERT_EQUAL_UINT(plaintext.size(), decrypted.size());
    TEST_ASSERT_EQUAL_MEMORY(plaintext.data(), decrypted.data(), plaintext.size());
}

void test_token_roundtrip_aes256() {
    Bytes key = fromHex(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"
    );
    Bytes plaintext = fromHex("54657374696e67204145532d323536");  // "Testing AES-256"

    Token token(key);
    Bytes ciphertext = token.encrypt(plaintext);
    Bytes decrypted = token.decrypt(ciphertext);

    TEST_ASSERT_EQUAL_UINT(plaintext.size(), decrypted.size());
    TEST_ASSERT_EQUAL_MEMORY(plaintext.data(), decrypted.data(), plaintext.size());
}

//
// Test Runner
//

void setUp(void) {
    // Set up before each test
}

void tearDown(void) {
    // Clean up after each test
}

int runUnityTests(void) {
    UNITY_BEGIN();

    // HKDF tests - validate key derivation matches Python RNS
    RUN_TEST(test_hkdf_aes128);
    RUN_TEST(test_hkdf_aes256);
    RUN_TEST(test_hkdf_aes128_alt);
    RUN_TEST(test_hkdf_aes256_alt);

    // Token decrypt tests - validate we can decrypt Python-encrypted data
    RUN_TEST(test_token_decrypt_aes128);
    RUN_TEST(test_token_decrypt_aes256);
    RUN_TEST(test_token_decrypt_empty);
    RUN_TEST(test_token_decrypt_binary);

    // Token roundtrip tests - validate our own encrypt/decrypt works
    RUN_TEST(test_token_roundtrip_aes128);
    RUN_TEST(test_token_roundtrip_aes256);

    return UNITY_END();
}

int main(void) {
    return runUnityTests();
}

#ifdef ARDUINO
void setup() {
    delay(2000);
    runUnityTests();
}
void loop() {}
#endif

void app_main() {
    runUnityTests();
}
