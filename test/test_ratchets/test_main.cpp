#include <unity.h>

#include "Bytes.h"
#include "Identity.h"
#include "Destination.h"
#include "Packet.h"
#include "Cryptography/Ratchet.h"
#include "Utilities/OS.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>

using namespace RNS;
using namespace RNS::Cryptography;

void testRatchetGeneration() {
	// Test that we can generate a ratchet with valid key material
	Ratchet ratchet = Ratchet::generate();

	// Verify public and private keys are 32 bytes
	Bytes private_key = ratchet.private_bytes();
	Bytes public_key = ratchet.public_bytes();

	TEST_ASSERT_EQUAL_size_t(32, private_key.size());
	TEST_ASSERT_EQUAL_size_t(32, public_key.size());

	// Verify keys are not empty (all zeros)
	bool private_not_zero = false;
	bool public_not_zero = false;

	for (size_t i = 0; i < private_key.size(); ++i) {
		if (private_key.data()[i] != 0) private_not_zero = true;
	}
	for (size_t i = 0; i < public_key.size(); ++i) {
		if (public_key.data()[i] != 0) public_not_zero = true;
	}

	TEST_ASSERT_TRUE(private_not_zero);
	TEST_ASSERT_TRUE(public_not_zero);
}

void testRatchetID() {
	// Test ratchet ID derivation
	Ratchet ratchet = Ratchet::generate();
	Bytes public_key = ratchet.public_bytes();

	// Get ratchet ID from public key
	Bytes ratchet_id = Ratchet::get_ratchet_id(public_key);

	// Verify ID is 10 bytes (RATCHET_ID_LENGTH)
	TEST_ASSERT_EQUAL_size_t(10, ratchet_id.size());

	// Verify ID is deterministic (same public key -> same ID)
	Bytes ratchet_id2 = Ratchet::get_ratchet_id(public_key);
	TEST_ASSERT_EQUAL_INT(0, memcmp(ratchet_id.data(), ratchet_id2.data(), 10));

	// Verify get_id() method returns same result
	Bytes id_from_method = ratchet.get_id();
	TEST_ASSERT_EQUAL_INT(0, memcmp(ratchet_id.data(), id_from_method.data(), 10));
}

void testRatchetSharedSecret() {
	// Test X25519 ECDH shared secret derivation
	Ratchet alice_ratchet = Ratchet::generate();
	Ratchet bob_ratchet = Ratchet::generate();

	// Alice derives shared secret using Bob's public key
	Bytes alice_shared = alice_ratchet.derive_shared_secret(bob_ratchet.public_bytes());

	// Bob derives shared secret using Alice's public key
	Bytes bob_shared = bob_ratchet.derive_shared_secret(alice_ratchet.public_bytes());

	// Verify shared secrets match (ECDH property)
	TEST_ASSERT_EQUAL_size_t(32, alice_shared.size());
	TEST_ASSERT_EQUAL_size_t(32, bob_shared.size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(alice_shared.data(), bob_shared.data(), 32));
}

void testRatchetKeyDerivation() {
	// Test HKDF key derivation from shared secret
	Ratchet alice_ratchet = Ratchet::generate();
	Ratchet bob_ratchet = Ratchet::generate();

	Bytes shared_secret = alice_ratchet.derive_shared_secret(bob_ratchet.public_bytes());

	// Derive encryption key (Fernet requires 32 bytes)
	Bytes key = alice_ratchet.derive_key(shared_secret);

	TEST_ASSERT_EQUAL_size_t(32, key.size());

	// Verify key is deterministic
	Bytes key2 = alice_ratchet.derive_key(shared_secret);
	TEST_ASSERT_EQUAL_INT(0, memcmp(key.data(), key2.data(), 32));
}

void testRatchetEncryptDecrypt() {
	// Test end-to-end encryption/decryption with ratchets
	Ratchet alice_ratchet = Ratchet::generate();
	Ratchet bob_ratchet = Ratchet::generate();

	const char* plaintext_str = "Hello from Alice to Bob using ratchets!";
	Bytes plaintext(plaintext_str);

	// Alice encrypts message for Bob using Bob's public key
	Bytes ciphertext = alice_ratchet.encrypt(plaintext, bob_ratchet.public_bytes());

	// Verify ciphertext is longer than plaintext (includes Fernet overhead)
	TEST_ASSERT_GREATER_THAN(plaintext.size(), ciphertext.size());

	// Bob decrypts message from Alice using Alice's public key
	Bytes decrypted = bob_ratchet.decrypt(ciphertext, alice_ratchet.public_bytes());

	// Verify decrypted plaintext matches original
	TEST_ASSERT_EQUAL_size_t(plaintext.size(), decrypted.size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(plaintext.data(), decrypted.data(), plaintext.size()));
}

void testDestinationRatchetEnable() {
	// Test enabling ratchets on a destination
	Identity identity;
	Destination dest(identity, Type::Destination::IN, Type::Destination::SINGLE, "testapp", "test");

	// Enable ratchets
	dest.enable_ratchets("/tmp/test_ratchets");

	// Verify initial ratchet was created
	Bytes ratchet_id = dest.get_latest_ratchet_id();
	TEST_ASSERT_EQUAL_size_t(10, ratchet_id.size());

	Bytes ratchet_pub = dest.get_ratchet_public_bytes();
	TEST_ASSERT_EQUAL_size_t(32, ratchet_pub.size());
}

void testDestinationRatchetRotation() {
	// Test ratchet rotation logic
	Identity identity;
	Destination dest(identity, Type::Destination::IN, Type::Destination::SINGLE, "testapp", "test");

	dest.enable_ratchets("/tmp/test_ratchets");

	// Get initial ratchet ID
	Bytes ratchet_id_1 = dest.get_latest_ratchet_id();

	// Rotate ratchet (force=true to bypass time interval check)
	dest.rotate_ratchets(true);

	// Get new ratchet ID
	Bytes ratchet_id_2 = dest.get_latest_ratchet_id();

	// Verify ratchet ID changed after rotation
	TEST_ASSERT_NOT_EQUAL(0, memcmp(ratchet_id_1.data(), ratchet_id_2.data(), 10));
}

void testIdentityRatchetCache() {
	// Test Identity static ratchet cache (remember/recall)
	Ratchet ratchet = Ratchet::generate();
	Bytes dest_hash = Identity::get_random_hash();
	Bytes ratchet_pub = ratchet.public_bytes();

	// Remember ratchet for destination
	Identity::remember_ratchet(dest_hash, ratchet_pub);

	// Recall ratchet for destination
	Bytes recalled = Identity::recall_ratchet(dest_hash);

	// Verify recalled ratchet matches
	TEST_ASSERT_EQUAL_size_t(32, recalled.size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(ratchet_pub.data(), recalled.data(), 32));

	// Test recall for unknown destination
	Bytes unknown_hash = Identity::get_random_hash();
	Bytes empty = Identity::recall_ratchet(unknown_hash);
	TEST_ASSERT_EQUAL_size_t(0, empty.size());
}

void testRatchetAnnounceIntegration() {
	// Test that ratchet public key can be retrieved after enabling ratchets
	Identity identity;
	Destination dest(identity, Type::Destination::IN, Type::Destination::SINGLE, "testapp", "test");

	// Enable ratchets
	dest.enable_ratchets("/tmp/test_ratchets");

	// Verify ratchet public key is available
	Bytes ratchet_pub = dest.get_ratchet_public_bytes();
	TEST_ASSERT_EQUAL_size_t(32, ratchet_pub.size());

	// Verify ratchet ID is available
	Bytes ratchet_id = dest.get_latest_ratchet_id();
	TEST_ASSERT_EQUAL_size_t(10, ratchet_id.size());

	// Note: Full announce integration test would require setting up Transport infrastructure
	// For now, we verify the ratchet data is available for inclusion in announces
}

void testMultipleRatchetRotations() {
	// Test multiple ratchet rotations (circular buffer behavior)
	Identity identity;
	Destination dest(identity, Type::Destination::IN, Type::Destination::SINGLE, "testapp", "test");

	dest.enable_ratchets("/tmp/test_ratchets");

	// Rotate 10 times and verify each ratchet is unique
	Bytes ratchet_ids[10];
	for (int i = 0; i < 10; ++i) {
		ratchet_ids[i] = dest.get_latest_ratchet_id();
		dest.rotate_ratchets(true);  // Force rotation for testing
	}

	// Verify all ratchet IDs are different
	for (int i = 0; i < 10; ++i) {
		for (int j = i + 1; j < 10; ++j) {
			TEST_ASSERT_NOT_EQUAL(0, memcmp(ratchet_ids[i].data(), ratchet_ids[j].data(), 10));
		}
	}
}

void testRatchetConstructor() {
	// Test creating ratchet from existing key material
	const uint8_t private_key_data[32] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
		0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
	};
	const uint8_t public_key_data[32] = {
		0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
		0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0,
		0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
		0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0
	};

	Bytes private_key(private_key_data, 32);
	Bytes public_key(public_key_data, 32);
	double created_at = Utilities::OS::time();

	Ratchet ratchet(private_key, public_key, created_at);

	// Verify keys match
	TEST_ASSERT_EQUAL_INT(0, memcmp(private_key.data(), ratchet.private_bytes().data(), 32));
	TEST_ASSERT_EQUAL_INT(0, memcmp(public_key.data(), ratchet.public_bytes().data(), 32));
}


void setUp(void) {
	// Set up before each test
}

void tearDown(void) {
	// Clean up after each test
}

int runUnityTests(void) {
	UNITY_BEGIN();
	RUN_TEST(testRatchetGeneration);
	RUN_TEST(testRatchetID);
	RUN_TEST(testRatchetSharedSecret);
	RUN_TEST(testRatchetKeyDerivation);
	RUN_TEST(testRatchetEncryptDecrypt);
	RUN_TEST(testDestinationRatchetEnable);
	RUN_TEST(testDestinationRatchetRotation);
	RUN_TEST(testIdentityRatchetCache);
	RUN_TEST(testRatchetAnnounceIntegration);
	RUN_TEST(testMultipleRatchetRotations);
	RUN_TEST(testRatchetConstructor);
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
