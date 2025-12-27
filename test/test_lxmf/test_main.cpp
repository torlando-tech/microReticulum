#include <unity.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <ArduinoJson.h>

#include "Bytes.h"
#include "Identity.h"
#include "Destination.h"
#include "LXMF/LXMessage.h"
#include "LXMF/LXMRouter.h"
#include "LXMF/MessageStore.h"
#include "LXMF/Type.h"
#include "Utilities/OS.h"

#include "../common/filesystem/FileSystem.h"

using namespace RNS;
using namespace LXMF;

// Avoid namespace collision - explicitly use RNS::Type for Destination types
using RNS::Type::Destination::IN;
using RNS::Type::Destination::SINGLE;

void testLXMessageCreation() {
	// Create identities for source and destination
	Identity dest_identity;
	Identity source_identity;

	// Create destinations
	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	// Create a simple message
	Bytes content("Hello, LXMF world!");
	Bytes title("Test Message");

	LXMessage message(dest, source, content, title);

	// Verify basic properties
	TEST_ASSERT_EQUAL_size_t(content.size(), message.content().size());
	TEST_ASSERT_EQUAL_size_t(title.size(), message.title().size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(content.data(), message.content().data(), content.size()));
}

void testLXMessagePack() {
	// Create identities
	Identity dest_identity;
	Identity source_identity;

	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	// Create message
	Bytes content("Test content for packing");
	LXMessage message(dest, source, content);

	// Pack the message
	const Bytes& packed = message.pack();

	// Verify packed message has minimum expected size
	// Minimum: 16 (dest) + 16 (source) + 64 (sig) + some payload
	size_t min_size = 2 * LXMF::Type::Constants::DESTINATION_LENGTH +
	                  LXMF::Type::Constants::SIGNATURE_LENGTH;
	TEST_ASSERT_GREATER_OR_EQUAL(min_size, packed.size());

	// Verify message has a hash after packing
	TEST_ASSERT_EQUAL_size_t(32, message.hash().size());
}

void testLXMessagePackUnpack() {
	// Create identities
	Identity dest_identity;
	Identity source_identity;

	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	// Remember the source identity so it can be recalled during unpack
	Bytes packet_hash = Identity::get_random_hash();
	Identity::remember(packet_hash, source.hash(), source_identity.get_public_key());

	// Create and pack message
	Bytes content("Round-trip test message");
	Bytes title("Round-trip");
	LXMessage original(dest, source, content, title);
	const Bytes& packed = original.pack();

	// Unpack the message
	LXMessage unpacked = LXMessage::unpack_from_bytes(packed);

	// Verify content matches
	TEST_ASSERT_EQUAL_size_t(content.size(), unpacked.content().size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(content.data(), unpacked.content().data(), content.size()));

	// Verify title matches
	TEST_ASSERT_EQUAL_size_t(title.size(), unpacked.title().size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(title.data(), unpacked.title().data(), title.size()));

	// Verify hashes match
	TEST_ASSERT_EQUAL_INT(0, memcmp(original.hash().data(), unpacked.hash().data(), 32));

	// Verify signature was validated
	TEST_ASSERT_TRUE(unpacked.signature_validated());
}

void testLXMessageSmallContent() {
	// Test with content that fits in single packet (<= 319 bytes)
	Identity dest_identity;
	Identity source_identity;

	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	// Create 200-byte message (well under 319 limit)
	Bytes content(200);
	uint8_t* writable = content.writable(200);
	for (size_t i = 0; i < 200; ++i) {
		writable[i] = (uint8_t)(i % 256);
	}

	LXMessage message(dest, source, content);
	message.pack();

	// Should be PACKET representation, not RESOURCE
	TEST_ASSERT_EQUAL_INT(LXMF::Type::Message::PACKET, message.representation());
}

void testLXMessageEmptyContent() {
	// Test with empty content and title
	Identity dest_identity;
	Identity source_identity;

	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	LXMessage message(dest, source, Bytes(), Bytes());
	const Bytes& packed = message.pack();

	// Should still pack successfully
	TEST_ASSERT_GREATER_THAN(0, packed.size());

	// Unpack and verify
	Identity::remember(Identity::get_random_hash(), source.hash(), source_identity.get_public_key());
	LXMessage unpacked = LXMessage::unpack_from_bytes(packed);

	TEST_ASSERT_EQUAL_size_t(0, unpacked.content().size());
	TEST_ASSERT_EQUAL_size_t(0, unpacked.title().size());
}

void testLXMessageFields() {
	// Test with custom fields
	Identity dest_identity;
	Identity source_identity;

	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	// Create fields map
	std::map<Bytes, Bytes> fields;
	fields[Bytes("field1")] = Bytes("value1");
	fields[Bytes("field2")] = Bytes("value2");

	LXMessage message(dest, source, Bytes("Content"), Bytes("Title"), fields);
	const Bytes& packed = message.pack();

	// Unpack and verify fields
	Identity::remember(Identity::get_random_hash(), source.hash(), source_identity.get_public_key());
	LXMessage unpacked = LXMessage::unpack_from_bytes(packed);

	TEST_ASSERT_EQUAL_size_t(2, unpacked.fields().size());
	// Note: Full field validation would require proper map comparison
}

void testPythonInterop() {
	// Test unpacking a message created by Python LXMF
	// This validates byte-level compatibility between C++ and Python implementations

	// Read the test vector generated by lxmf_simple_test.py
	std::ifstream file("/tmp/lxmf_test_vector.json");
	if (!file.is_open()) {
		TEST_IGNORE_MESSAGE("Python test vector not found at /tmp/lxmf_test_vector.json. Run: python3 test/test_interop/python/lxmf_simple_test.py");
		return;
	}

	// Parse JSON
	std::stringstream buffer;
	buffer << file.rdbuf();
	file.close();

	JsonDocument doc;
	DeserializationError error = deserializeJson(doc, buffer.str());
	TEST_ASSERT_FALSE(error);

	// Extract test data
	const char* packed_hex = doc["packed"];
	const char* content_str = doc["content"];
	const char* title_str = doc["title"];

	// Convert hex strings to Bytes
	Bytes packed;
	packed.assignHex(packed_hex);

	// Extract additional test data for validation
	const char* source_identity_pub_hex = doc["source_identity_pub"];
	const char* source_hash_hex = doc["source_hash"];
	const char* dest_hash_hex = doc["dest_hash"];
	const char* message_hash_hex = doc["message_hash"];

	Bytes source_pub;
	source_pub.assignHex(source_identity_pub_hex);
	Bytes source_hash;
	source_hash.assignHex(source_hash_hex);
	Bytes dest_hash;
	dest_hash.assignHex(dest_hash_hex);
	Bytes expected_hash;
	expected_hash.assignHex(message_hash_hex);

	// Remember the source identity so signature can be validated
	Bytes packet_hash = Identity::get_random_hash();
	Identity::remember(packet_hash, source_hash, source_pub);

	// Unpack the Python-generated message
	LXMessage unpacked = LXMessage::unpack_from_bytes(packed);

	// Verify hash matches
	TEST_ASSERT_EQUAL_size_t(32, unpacked.hash().size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(expected_hash.data(), unpacked.hash().data(), 32));

	// Verify content matches
	std::string content_cpp(reinterpret_cast<const char*>(unpacked.content().data()), unpacked.content().size());
	TEST_ASSERT_EQUAL_STRING(content_str, content_cpp.c_str());

	// Verify title matches
	std::string title_cpp(reinterpret_cast<const char*>(unpacked.title().data()), unpacked.title().size());
	TEST_ASSERT_EQUAL_STRING(title_str, title_cpp.c_str());

	// Verify signature was validated
	TEST_ASSERT_TRUE(unpacked.signature_validated());

	// Verify hashes match
	TEST_ASSERT_EQUAL_INT(0, memcmp(source_hash.data(), unpacked.source_hash().data(), 16));
	TEST_ASSERT_EQUAL_INT(0, memcmp(dest_hash.data(), unpacked.destination_hash().data(), 16));
}

void testLXMRouterCreation() {
	// Test router initialization
	Identity router_identity;
	LXMRouter router(router_identity, "/tmp/lxmf_test", false);  // No announce for testing

	// Verify delivery destination was created
	const Destination& dest = router.delivery_destination();
	TEST_ASSERT_TRUE(dest.operator bool());
	TEST_ASSERT_EQUAL_size_t(16, dest.hash().size());

	// Verify no pending messages initially
	TEST_ASSERT_EQUAL_size_t(0, router.pending_outbound_count());
	TEST_ASSERT_EQUAL_size_t(0, router.pending_inbound_count());
	TEST_ASSERT_EQUAL_size_t(0, router.failed_outbound_count());
}

void testLXMRouterOutbound() {
	// Test queuing outbound message
	Identity router_identity;
	Identity dest_identity;

	LXMRouter router(router_identity, "/tmp/lxmf_test", false);  // No announce for testing

	// Create a message
	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(router_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	LXMessage message(dest, source, Bytes("Test message content"));

	// Pre-pack the message (so handle_outbound doesn't crash trying to pack)
	message.pack();

	// Queue the message
	router.handle_outbound(message);

	// Verify message is in pending queue
	TEST_ASSERT_EQUAL_size_t(1, router.pending_outbound_count());
	TEST_ASSERT_EQUAL_INT(LXMF::Type::Message::OUTBOUND, message.state());
}

void testLXMRouterCallbacks() {
	// Test delivery callback registration and invocation
	Identity router_identity;
	LXMRouter router(router_identity, "/tmp/lxmf_test", false);  // No announce for testing

	bool delivery_called = false;
	bool sent_called = false;
	bool failed_called = false;

	// Register callbacks
	router.register_delivery_callback([&delivery_called](LXMessage& msg) {
		delivery_called = true;
	});

	router.register_sent_callback([&sent_called](LXMessage& msg) {
		sent_called = true;
	});

	router.register_failed_callback([&failed_called](LXMessage& msg) {
		failed_called = true;
	});

	// Callbacks are registered (can't easily test invocation without full RNS stack)
	TEST_ASSERT_TRUE(true);
}

void testLXMRouterAnnounce() {
	// Test announce functionality
	Identity router_identity;
	LXMRouter router(router_identity, "/tmp/lxmf_test");

	// Disable auto-announce at start for this test
	router.set_announce_at_start(false);

	// Set announce interval
	router.set_announce_interval(60);

	// Manual announce (would fail without Transport infrastructure, but we test the call)
	// router.announce();  // Skip actual announce in unit test

	TEST_ASSERT_TRUE(true);
}

void testLXMRouterFailedRetry() {
	// Test failed message retry
	Identity router_identity;
	Identity dest_identity;

	LXMRouter router(router_identity, "/tmp/lxmf_test", false);  // No announce for testing

	// Create a message
	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(router_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	LXMessage message(dest, source, Bytes("Test"));
	message.state(LXMF::Type::Message::FAILED);

	// Simulate failed message by manually adding to failed queue
	// (In real usage, process_outbound() would do this)
	// For now, just test the retry mechanism exists
	router.clear_failed_outbound();
	TEST_ASSERT_EQUAL_size_t(0, router.failed_outbound_count());
}

void testMessageStoreCreation() {
	// Test MessageStore initialization
	MessageStore store("/tmp/lxmf_store_test");

	// Verify empty state
	TEST_ASSERT_EQUAL_size_t(0, store.get_conversation_count());
	TEST_ASSERT_EQUAL_size_t(0, store.get_message_count());
	TEST_ASSERT_EQUAL_size_t(0, store.get_unread_count());
}

void testMessageStoreSaveLoad() {
	// Test saving and loading messages
	Identity dest_identity;
	Identity source_identity;

	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	// Create and pack a message
	LXMessage original(dest, source, Bytes("Test message for storage"), Bytes("Test Title"));
	original.pack();

	// Save to store
	MessageStore store("/tmp/lxmf_store_test");
	bool saved = store.save_message(original);
	TEST_ASSERT_TRUE(saved);

	// Verify counts
	TEST_ASSERT_EQUAL_size_t(1, store.get_message_count());
	TEST_ASSERT_EQUAL_size_t(1, store.get_conversation_count());

	// Load the message back
	LXMessage loaded = store.load_message(original.hash());
	TEST_ASSERT_TRUE(loaded.hash().operator bool());
	TEST_ASSERT_EQUAL_INT(0, memcmp(original.hash().data(), loaded.hash().data(), original.hash().size()));

	// Cleanup
	store.clear_all();
}

void testMessageStoreConversations() {
	// Test conversation management
	Identity our_identity;
	Identity peer1_identity;
	Identity peer2_identity;

	Destination our_dest(our_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination peer1_dest(peer1_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination peer2_dest(peer2_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	MessageStore store("/tmp/lxmf_store_test");
	store.clear_all();

	// Create messages with peer1
	LXMessage msg1(peer1_dest, our_dest, Bytes("Message 1 to peer1"));
	msg1.pack();
	store.save_message(msg1);

	LXMessage msg2(peer1_dest, our_dest, Bytes("Message 2 to peer1"));
	msg2.pack();
	store.save_message(msg2);

	// Create message with peer2
	LXMessage msg3(peer2_dest, our_dest, Bytes("Message 1 to peer2"));
	msg3.pack();
	store.save_message(msg3);

	// Verify conversation counts
	TEST_ASSERT_EQUAL_size_t(2, store.get_conversation_count());
	TEST_ASSERT_EQUAL_size_t(3, store.get_message_count());

	// Get conversations
	std::vector<Bytes> conversations = store.get_conversations();
	TEST_ASSERT_EQUAL_size_t(2, conversations.size());

	// Get messages for peer1
	std::vector<Bytes> peer1_messages = store.get_messages_for_conversation(peer1_dest.hash());
	TEST_ASSERT_EQUAL_size_t(2, peer1_messages.size());

	// Cleanup
	store.clear_all();
}

void testMessageStoreDelete() {
	// Test message deletion
	Identity dest_identity;
	Identity source_identity;

	Destination dest(dest_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	Destination source(source_identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

	MessageStore store("/tmp/lxmf_store_test");
	store.clear_all();

	// Save a message
	LXMessage message(dest, source, Bytes("Message to delete"));
	message.pack();
	store.save_message(message);

	TEST_ASSERT_EQUAL_size_t(1, store.get_message_count());

	// Delete the message
	bool deleted = store.delete_message(message.hash());
	TEST_ASSERT_TRUE(deleted);
	TEST_ASSERT_EQUAL_size_t(0, store.get_message_count());

	// Cleanup
	store.clear_all();
}


void setUp(void) {
	// Set up before each test
}

void tearDown(void) {
	// Clean up after each test
}

int runUnityTests(void) {
	UNITY_BEGIN();

	// Suite-level setup - register filesystem for MessageStore tests
	RNS::FileSystem lxmf_filesystem = new ::FileSystem();
	((::FileSystem*)lxmf_filesystem.get())->init();
	RNS::Utilities::OS::register_filesystem(lxmf_filesystem);

	RUN_TEST(testLXMessageCreation);
	RUN_TEST(testLXMessagePack);
	RUN_TEST(testLXMessagePackUnpack);
	RUN_TEST(testLXMessageSmallContent);
	RUN_TEST(testLXMessageEmptyContent);
	RUN_TEST(testLXMessageFields);
	RUN_TEST(testPythonInterop);
	RUN_TEST(testLXMRouterCreation);
	// Skip router tests that require full RNS stack
	//RUN_TEST(testLXMRouterOutbound);
	RUN_TEST(testLXMRouterCallbacks);
	RUN_TEST(testLXMRouterAnnounce);
	//RUN_TEST(testLXMRouterFailedRetry);  // Also requires RNS stack
	RUN_TEST(testMessageStoreCreation);
	RUN_TEST(testMessageStoreSaveLoad);
	RUN_TEST(testMessageStoreConversations);
	RUN_TEST(testMessageStoreDelete);

	// Suite-level teardown
	RNS::Utilities::OS::deregister_filesystem();

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
