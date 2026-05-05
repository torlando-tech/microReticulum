#include <unity.h>

#include "Bytes.h"
#include "Utilities/Crc.h"
#include "Cryptography/HMAC.h"
#include "Cryptography/PKCS7.h"
#include "Cryptography/X25519.h"

#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdio.h>

// Imported from Crypto.cpp
// Computes CRC-8/HITAG checksum
// CBA Doesn't appear to support incremental checksum building
extern uint8_t crypto_crc8(uint8_t tag, const void *data, unsigned size);

void testHMAC() {

	const char keystr[] = "key";
	const char datastr[] = "The quick brown fox jumps over the lazy dog";
	// HMAC-SHA256(key="key", "The quick brown fox jumps over the lazy dog")
	// canonical reference vector (e.g. matches Python's `hmac.new(b"key",
	// b"The quick...", hashlib.sha256).digest()`).
	const uint8_t hasharr[] = {
		0xf7, 0xbc, 0x83, 0xf4, 0x30, 0x53, 0x84, 0x24,
		0xb1, 0x32, 0x98, 0xe6, 0xaa, 0x6f, 0xb1, 0x43,
		0xef, 0x4d, 0x59, 0xa1, 0x49, 0x46, 0x17, 0x59,
		0x97, 0x47, 0x9d, 0xbc, 0x2d, 0x1a, 0x3c, 0xd8
	};

	// Class form — was already tested before this commit.
	{
		RNS::Bytes key(keystr);
		RNS::Bytes data(datastr);
		RNS::Bytes hash(hasharr, sizeof(hasharr));
		RNS::Cryptography::HMAC hmac(key, data);
		RNS::Bytes result = hmac.digest();
		TEST_ASSERT_EQUAL_INT(0, memcmp(hash.data(), result.data(), result.size()));
	}

	// Inline `digest()` helper. Before this commit, the helper called
	// hmac.update(msg) AFTER the HMAC(key, msg, ...) constructor had
	// already consumed msg, producing HMAC(msg||msg) instead of HMAC(msg).
	// Assert it now matches the canonical vector — same input as the class
	// case above must produce the same hash.
	{
		RNS::Bytes key(keystr);
		RNS::Bytes data(datastr);
		RNS::Bytes hash(hasharr, sizeof(hasharr));
		RNS::Bytes result = RNS::Cryptography::digest(key, data);
		TEST_ASSERT_EQUAL_size_t(sizeof(hasharr), result.size());
		TEST_ASSERT_EQUAL_INT(0, memcmp(hash.data(), result.data(), result.size()));
	}
}

void testPKCS7() {
	
	const uint8_t str[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

	// test buffer of half blocksize
	{
		size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE / 2;
		RNS::Bytes bytes(str, len);
		bytes = RNS::Cryptography::PKCS7::pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());

		bytes = RNS::Cryptography::PKCS7::unpad(bytes);
		TEST_ASSERT_EQUAL_size_t(len, bytes.size());
		TEST_ASSERT_EQUAL_INT(0, memcmp(bytes.data(), str, len));
	}

	// test buffer of one less blocksize
	{
		size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE - 1;
		RNS::Bytes bytes(str, len);
		bytes = RNS::Cryptography::PKCS7::pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());

		bytes = RNS::Cryptography::PKCS7::unpad(bytes);
		TEST_ASSERT_EQUAL_size_t(len, bytes.size());
		TEST_ASSERT_EQUAL_INT(0, memcmp(bytes.data(), str, len));
	}

	// test buffer of blocksize
	{
		size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE;
		RNS::Bytes bytes(str, len);
		bytes = RNS::Cryptography::PKCS7::pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE * 2, bytes.size());

		bytes = RNS::Cryptography::PKCS7::unpad(bytes);
		TEST_ASSERT_EQUAL_size_t(len, bytes.size());
		TEST_ASSERT_EQUAL_INT(0, memcmp(bytes.data(), str, len));
	}

	// test inplace buffer of half blocksize
	{
		size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE / 2;
		RNS::Bytes bytes(str, len);
		RNS::Cryptography::PKCS7::inplace_pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());

		RNS::Cryptography::PKCS7::inplace_unpad(bytes);
		TEST_ASSERT_EQUAL_size_t(len, bytes.size());
		TEST_ASSERT_EQUAL_INT(0, memcmp(bytes.data(), str, len));
	}

	// test inplace buffer of one less blocksize
	{
		size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE - 1;
		RNS::Bytes bytes(str, len);
		RNS::Cryptography::PKCS7::inplace_pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());

		RNS::Cryptography::PKCS7::inplace_unpad(bytes);
		TEST_ASSERT_EQUAL_size_t(len, bytes.size());
		TEST_ASSERT_EQUAL_INT(0, memcmp(bytes.data(), str, len));
	}

	// test inplace buffer of blocksize
	{
		size_t len = RNS::Cryptography::PKCS7::BLOCKSIZE;
		RNS::Bytes bytes(str, len);
		RNS::Cryptography::PKCS7::inplace_pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE * 2, bytes.size());

		RNS::Cryptography::PKCS7::inplace_unpad(bytes);
		TEST_ASSERT_EQUAL_size_t(len, bytes.size());
		TEST_ASSERT_EQUAL_INT(0, memcmp(bytes.data(), str, len));
	}

	// Spec-conformance: PKCS#7 (RFC 5652 §6.3) requires the pad to consist
	// of `padlen` octets each having value `padlen` — NOT zeros with the
	// last byte set to padlen. Self-roundtrip works either way (unpad
	// reads the last byte as padlen) so the older tests above don't catch
	// this. The cases below assert the exact byte pattern.
	{
		// 13 bytes input → 3 padding bytes, each = 0x03.
		RNS::Bytes bytes(str, 13);
		bytes = RNS::Cryptography::PKCS7::pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());
		TEST_ASSERT_EQUAL_UINT8(0x03, bytes.data()[13]);
		TEST_ASSERT_EQUAL_UINT8(0x03, bytes.data()[14]);
		TEST_ASSERT_EQUAL_UINT8(0x03, bytes.data()[15]);
	}
	{
		// Empty input → 16 padding bytes, each = 0x10.
		RNS::Bytes bytes;
		bytes = RNS::Cryptography::PKCS7::pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());
		for (size_t i = 0; i < 16; ++i) {
			TEST_ASSERT_EQUAL_UINT8(0x10, bytes.data()[i]);
		}
	}
	{
		// 1 byte input → 15 padding bytes, each = 0x0F.
		RNS::Bytes bytes(str, 1);
		bytes = RNS::Cryptography::PKCS7::pad(bytes);
		TEST_ASSERT_EQUAL_size_t(RNS::Cryptography::PKCS7::BLOCKSIZE, bytes.size());
		for (size_t i = 1; i < 16; ++i) {
			TEST_ASSERT_EQUAL_UINT8(0x0F, bytes.data()[i]);
		}
	}
}

void testCrc8() {
	char data[32];
	uint8_t crc;

	strcpy(data, "foo");
	crc = crypto_crc8(0, data, strlen(data));
	TEST_ASSERT_EQUAL_UINT8(0x48, crc);

	strcpy(data, "bar");
	crc = crypto_crc8(0, data, strlen(data));
	TEST_ASSERT_EQUAL_UINT8(0xED, crc);

	strcpy(data, "foo");
	crc = crypto_crc8(0, data, strlen(data));
	TEST_ASSERT_EQUAL_UINT8(0x48, crc);

	strcpy(data, "foobarfoo");
	crc = crypto_crc8(0, data, strlen(data));
	TEST_ASSERT_EQUAL_UINT8(0x00, crc);

}

void testCrc32() {
	char data[16];
	uint32_t crc;

	strcpy(data, "foo");
	crc = RNS::Utilities::Crc::crc32(0, data);
	TEST_ASSERT_EQUAL_UINT32(0x8C736521, crc);

	strcpy(data, "bar");
	crc = RNS::Utilities::Crc::crc32(0, data);
	TEST_ASSERT_EQUAL_UINT32(0x76FF8CAA, crc);

	strcpy(data, "foo");
	crc = RNS::Utilities::Crc::crc32(0, data);
	TEST_ASSERT_EQUAL_UINT32(0x8C736521, crc);

	strcpy(data, "foobarfoo");
	crc = RNS::Utilities::Crc::crc32(0, data);
	TEST_ASSERT_EQUAL_UINT32(0xEE2F4613, crc);
}

void testIncrementalCrc32() {
	char data[16];
	uint32_t crc;

	strcpy(data, "foo");
	crc = RNS::Utilities::Crc::crc32(0, data);
	TEST_ASSERT_EQUAL_UINT32(0x8C736521, crc);

	strcpy(data, "bar");
	crc = RNS::Utilities::Crc::crc32(crc, data);
	TEST_ASSERT_EQUAL_UINT32(0x9EF61F95, crc);

	strcpy(data, "foo");
	crc = RNS::Utilities::Crc::crc32(crc, data);
	TEST_ASSERT_EQUAL_UINT32(0xEE2F4613, crc);

	strcpy(data, "foobarfoo");
	crc = RNS::Utilities::Crc::crc32(0, data);
	TEST_ASSERT_EQUAL_UINT32(0xEE2F4613, crc);
}

void testByteCrc32() {
	char data[16];
	strcpy(data, "foobarfoo");

	uint32_t crc = 0;
	for (int i = 0; i < strlen(data); i++) {
		crc = RNS::Utilities::Crc::crc32(crc, data[i]);
	}
	TEST_ASSERT_EQUAL_UINT32(0xEE2F4613, crc);
}


void setUp(void) {
    // set stuff up here before each test
}

void tearDown(void) {
    // clean stuff up here after each test
}

// X25519 scalar clamping (RFC 7748 §5). Every private scalar must have:
//   - bottom 3 bits of byte 0 cleared
//   - top bit of byte 31 cleared
//   - second-from-top bit of byte 31 set
// Python's `cryptography` X25519PrivateKey.from_private_bytes applies these
// before scalar multiplication; without matching clamping in C++, every ECDH
// against Python RNS produces different bytes. This test feeds a 32-byte
// scalar with all clamping-affected bits in the "wrong" state and verifies
// the resulting `private_bytes()` reflects the canonical clamping.
void testX25519Clamping() {
	// Input: bottom 3 bits set (0x07), top bit set (0x80), second-from-top
	// bit clear in byte 31. After clamping: byte 0 should have low 3 bits
	// cleared, byte 31 should have top bit cleared and bit 6 set.
	uint8_t raw[32];
	memset(raw, 0x55, 32);   // 0x55 = 01010101 — middle bits arbitrary
	raw[0]  = 0xFF;          // bottom 3 bits set, will be cleared
	raw[31] = 0x80;          // top bit set, bit 6 clear — both must flip

	RNS::Bytes priv_bytes(raw, 32);
	auto priv = RNS::Cryptography::X25519PrivateKey::from_private_bytes(priv_bytes);
	const RNS::Bytes& clamped = priv->private_bytes();

	TEST_ASSERT_EQUAL_size_t(32, clamped.size());
	// byte 0: low 3 bits must be zero
	TEST_ASSERT_EQUAL_UINT8(0x00, clamped.data()[0] & 0x07);
	// byte 31: top bit must be zero, bit 6 must be set
	TEST_ASSERT_EQUAL_UINT8(0x00, clamped.data()[31] & 0x80);
	TEST_ASSERT_EQUAL_UINT8(0x40, clamped.data()[31] & 0x40);
	// concrete values: byte 0 = 0xFF & 0xF8 = 0xF8; byte 31 = (0x80 & 0x7F)
	// | 0x40 = 0x40.
	TEST_ASSERT_EQUAL_UINT8(0xF8, clamped.data()[0]);
	TEST_ASSERT_EQUAL_UINT8(0x40, clamped.data()[31]);
	// Middle bytes pass through unchanged (clamping affects only [0] and [31]).
	for (size_t i = 1; i < 31; ++i) {
		TEST_ASSERT_EQUAL_UINT8(0x55, clamped.data()[i]);
	}
}

int runUnityTests(void) {
    UNITY_BEGIN();
	RUN_TEST(testHMAC);
	RUN_TEST(testPKCS7);
	RUN_TEST(testX25519Clamping);
	RUN_TEST(testCrc8);
	RUN_TEST(testCrc32);
	RUN_TEST(testIncrementalCrc32);
	RUN_TEST(testByteCrc32);
    return UNITY_END();
}

// For native dev-platform or for some embedded frameworks
int main(void) {
    return runUnityTests();
}

#ifdef ARDUINO
// For Arduino framework
void setup() {
    // Wait ~2 seconds before the Unity test runner
    // establishes connection with a board Serial interface
    delay(2000);
    
    runUnityTests();
}
void loop() {}
#endif

// For ESP-IDF framework
void app_main() {
    runUnityTests();
}
