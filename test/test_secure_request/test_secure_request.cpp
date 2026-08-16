#include <unity.h>

#include "microReticulum/Bytes.h"
#include "microReticulum/Cryptography/Ed25519.h"
#include "microReticulum/Cryptography/Fernet.h"
#include "microReticulum/Cryptography/PKCS7.h"
#include "microReticulum/Cryptography/Token.h"
#include "microReticulum/Cryptography/X25519.h"
#include "microReticulum/Identity.h"
#include "microReticulum/Link.h"
#include "microReticulum/Log.h"

#include <stdexcept>
#include <string>

using namespace RNS;

namespace {
std::string captured_log;

void capture_log(const char* message, LogLevel) {
    if (message != nullptr) captured_log += message;
}
}  // namespace

void setUp() {}
void tearDown() {
    set_log_callback(nullptr);
}

void test_secure_clear_scrubs_all_shared_views() {
    const uint8_t secret[] = {0x73, 0x65, 0x63, 0x72, 0x65, 0x74};
    Bytes bytes(secret, sizeof(secret));
    Bytes shared = bytes;

    bytes.secure_clear();

    TEST_ASSERT_EQUAL_UINT32(0, bytes.size());
    TEST_ASSERT_EQUAL_UINT32(0, shared.size());
}

void test_request_exposes_opt_in_sensitive_handoff() {
    using SensitiveRequest = const RequestReceipt (Link::*)(
        const Bytes&, const Bytes&, RequestReceipt::Callbacks::response,
        RequestReceipt::Callbacks::failed, RequestReceipt::Callbacks::progress,
        double, size_t, bool);
    SensitiveRequest request = &Link::request;
    TEST_ASSERT_NOT_NULL(request);
}

void test_secure_guard_scrubs_shared_views_during_exception_unwind() {
    Bytes secret("exception-secret");
    Bytes shared = secret;

    try {
        SecureBytesGuard guard(secret);
        throw std::runtime_error("expected");
    }
    catch (const std::runtime_error&) {
    }

    TEST_ASSERT_EQUAL_UINT32(0, secret.size());
    TEST_ASSERT_EQUAL_UINT32(0, shared.size());
}

void test_token_encrypt_does_not_log_plaintext_at_trace_level() {
    uint8_t key_data[64] = {};
    Bytes key(key_data, sizeof(key_data));
    Bytes secret("trace-log-secret");
    const std::string secret_hex = secret.toHex();

    captured_log.clear();
    set_log_callback(capture_log);
    loglevel(LOG_TRACE);
    Cryptography::Token token(key);
    const Bytes encrypted = token.encrypt(secret);
    TEST_ASSERT_TRUE(encrypted.size() > 0);
    TEST_ASSERT_EQUAL_UINT32(std::string::npos, captured_log.find(secret_hex));

    captured_log.clear();
    const Bytes decrypted = token.decrypt(encrypted);
    TEST_ASSERT_EQUAL_UINT32(secret.size(), decrypted.size());
    TEST_ASSERT_EQUAL_UINT32(std::string::npos, captured_log.find(secret_hex));
}

void test_wrapper_crypto_does_not_log_plaintext_at_trace_level() {
    uint8_t key_data[32] = {};
    Bytes key(key_data, sizeof(key_data));
    Bytes secret("wrapper-trace-secret");
    const std::string secret_hex = secret.toHex();

    captured_log.clear();
    set_log_callback(capture_log);
    loglevel(LOG_TRACE);
    Cryptography::Fernet fernet(key);
    const Bytes encrypted = fernet.encrypt(secret);
    const Bytes decrypted = fernet.decrypt(encrypted);
    TEST_ASSERT_EQUAL_UINT32(secret.size(), decrypted.size());
    TEST_ASSERT_EQUAL_UINT32(std::string::npos, captured_log.find(secret_hex));
}

void test_pkcs7_secure_transaction_round_trip() {
    Bytes secret("secure-pad-secret");
    const Bytes padded = Cryptography::PKCS7::pad(secret);
    TEST_ASSERT_TRUE(padded.size() > secret.size());
    const Bytes unpadded = Cryptography::PKCS7::unpad(padded);
    TEST_ASSERT_EQUAL_UINT32(secret.size(), unpadded.size());
    TEST_ASSERT_EQUAL_MEMORY(secret.data(), unpadded.data(), secret.size());
}

void test_pkcs7_rejects_malformed_padding() {
    uint8_t malformed_bytes[16] = {};
    malformed_bytes[14] = 0x01;
    malformed_bytes[15] = 0x02;
    Bytes malformed(malformed_bytes, sizeof(malformed_bytes));
    bool malformed_rejected = false;
    try {
        Cryptography::PKCS7::unpad(malformed);
    }
    catch (const std::invalid_argument&) {
        malformed_rejected = true;
    }
    TEST_ASSERT_TRUE(malformed_rejected);

    Bytes unaligned("123456789012345");
    bool unaligned_rejected = false;
    try {
        Cryptography::PKCS7::unpad(unaligned);
    }
    catch (const std::invalid_argument&) {
        unaligned_rejected = true;
    }
    TEST_ASSERT_TRUE(unaligned_rejected);
}

void test_identity_crypto_does_not_log_secrets_at_trace_level() {
    Identity identity;
    Bytes secret("identity-trace-secret");
    const std::string secret_hex = secret.toHex();

    captured_log.clear();
    set_log_callback(capture_log);
    loglevel(LOG_TRACE);
    const Bytes ciphertext = identity.encrypt(secret);
    const Bytes plaintext = identity.decrypt(ciphertext);
    const Bytes signature = identity.sign(secret);
    TEST_ASSERT_TRUE(identity.validate(signature, secret));
    TEST_ASSERT_TRUE(plaintext == secret);
    TEST_ASSERT_EQUAL_UINT32(std::string::npos, captured_log.find(secret_hex));
    TEST_ASSERT_EQUAL_UINT32(std::string::npos, captured_log.find("shared key:"));
    TEST_ASSERT_EQUAL_UINT32(std::string::npos, captured_log.find("derived key:"));
}

void test_token_and_fernet_destructors_scrub_owned_key_slices() {
    Bytes token_signing;
    Bytes token_encryption;
    {
        Cryptography::Token token(Cryptography::Token::generate_key());
        token_signing = token.test_signing_key_view();
        token_encryption = token.test_encryption_key_view();
        TEST_ASSERT_TRUE(token_signing.size() > 0);
        TEST_ASSERT_TRUE(token_encryption.size() > 0);
    }
    TEST_ASSERT_EQUAL_UINT32(0, token_signing.size());
    TEST_ASSERT_EQUAL_UINT32(0, token_encryption.size());

    Bytes fernet_signing;
    Bytes fernet_encryption;
    {
        Cryptography::Fernet fernet(Cryptography::Fernet::generate_key());
        fernet_signing = fernet.test_signing_key_view();
        fernet_encryption = fernet.test_encryption_key_view();
        TEST_ASSERT_TRUE(fernet_signing.size() > 0);
        TEST_ASSERT_TRUE(fernet_encryption.size() > 0);
    }
    TEST_ASSERT_EQUAL_UINT32(0, fernet_signing.size());
    TEST_ASSERT_EQUAL_UINT32(0, fernet_encryption.size());
}

void test_private_key_destructors_scrub_owned_keys() {
    Bytes x25519_view;
    {
        Cryptography::X25519PrivateKey::Ptr key = Cryptography::X25519PrivateKey::generate();
        x25519_view = key->test_private_key_view();
        TEST_ASSERT_EQUAL_UINT32(32, x25519_view.size());
        key.reset();
    }
    TEST_ASSERT_EQUAL_UINT32(0, x25519_view.size());

    Bytes ed25519_view;
    {
        Cryptography::Ed25519PrivateKey::Ptr key = Cryptography::Ed25519PrivateKey::generate();
        ed25519_view = key->test_private_key_view();
        TEST_ASSERT_EQUAL_UINT32(32, ed25519_view.size());
        key.reset();
    }
    TEST_ASSERT_EQUAL_UINT32(0, ed25519_view.size());
}

void test_private_key_round_trip_preserves_identity_public_key() {
    Identity source;
    const Bytes private_key = source.get_private_key();
    const Bytes public_key = source.get_public_key();

    Cryptography::X25519PrivateKey::Ptr x25519 =
        Cryptography::X25519PrivateKey::from_private_bytes(private_key.left(32));
    Cryptography::Ed25519PrivateKey::Ptr ed25519 =
        Cryptography::Ed25519PrivateKey::from_private_bytes(private_key.mid(32));

    TEST_ASSERT_EQUAL_UINT8(0, private_key[0] & 0x07);
    TEST_ASSERT_EQUAL_UINT8(0, private_key[31] & 0x80);
    TEST_ASSERT_EQUAL_UINT8(0x40, private_key[31] & 0x40);
    TEST_ASSERT_TRUE(x25519->public_key()->public_bytes() == public_key.left(32));
    TEST_ASSERT_TRUE(ed25519->public_key()->public_bytes() == public_key.mid(32));

    Identity loaded(false);
    TEST_ASSERT_TRUE(loaded.load_private_key(private_key));
    TEST_ASSERT_TRUE(loaded.hash() == source.hash());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_secure_clear_scrubs_all_shared_views);
    RUN_TEST(test_request_exposes_opt_in_sensitive_handoff);
    RUN_TEST(test_secure_guard_scrubs_shared_views_during_exception_unwind);
    RUN_TEST(test_token_encrypt_does_not_log_plaintext_at_trace_level);
    RUN_TEST(test_wrapper_crypto_does_not_log_plaintext_at_trace_level);
    RUN_TEST(test_pkcs7_secure_transaction_round_trip);
    RUN_TEST(test_pkcs7_rejects_malformed_padding);
    RUN_TEST(test_identity_crypto_does_not_log_secrets_at_trace_level);
    RUN_TEST(test_token_and_fernet_destructors_scrub_owned_key_slices);
    RUN_TEST(test_private_key_destructors_scrub_owned_keys);
    RUN_TEST(test_private_key_round_trip_preserves_identity_public_key);
    return UNITY_END();
}
