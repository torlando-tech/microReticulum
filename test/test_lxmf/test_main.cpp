#include <unity.h>

#include "Bytes.h"
#include "Identity.h"
#include "Link.h"
#include "Destination.h"
#include "LXMF/LXMFTypes.h"
#include "LXMF/LXMessage.h"
#include "LXMF/LXMRouter.h"

#include <string.h>
#include <stdint.h>
#include <string>

using namespace RNS;
using namespace RNS::LXMF;

// Test keys from Python test vector generator
// These are deterministic test keys - DO NOT USE IN PRODUCTION
static const uint8_t TEST_SENDER_PRIV_KEY[] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
};

static const uint8_t TEST_RECEIVER_PRIV_KEY[] = {
    0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
    0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
    0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
    0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe
};

// Expected values from Python test vectors
// simple_message vector
static const char* SIMPLE_MESSAGE_HASH_HEX = "27430b46915b90983bc7e94c3c99725741d8ef8a3a2f2b0826465e27c14a19cd";
static const char* SIMPLE_MESSAGE_PACKED_HEX =
    "d8712e3207f8c7e25692d2d34168201f"  // dest hash
    "33e49dde9a96f3952b456cc2afc1e058"  // src hash
    "5431801d5d9eb120342d84f011adcff4"  // signature (first 16 bytes)
    "e935e28ee83bbf896e7c0c3f71e867bf"
    "ef307a5ba4d408f6a75d6cbffcf448b7"
    "20fd26c61e843bcfa90ee390c7b0b10d"
    "94cb41d954fc40000000"              // payload: array(4), float64 timestamp
    "c40548656c6c6f"                    // bin(5) "Hello"
    "c4165468697320697320612074657374206d657373616765"  // bin(22) content
    "80";                               // map(0)

// empty_message vector
static const char* EMPTY_MESSAGE_HASH_HEX = "6cbdb21ff826a6f3a5047b0dbf05c160442cd6b960ca84044b07a504392bc2d2";

// Helper to create test identities
static Identity createTestIdentity(const uint8_t* privKey) {
    Identity identity(false);  // Don't create keys
    Bytes priv(privKey, 64);
    identity.load_private_key(priv);
    return identity;
}

// Helper to convert hex string to Bytes
static Bytes hexToBytes(const char* hex) {
    Bytes result;
    size_t len = strlen(hex);
    for (size_t i = 0; i < len; i += 2) {
        char byte_str[3] = {hex[i], hex[i+1], 0};
        uint8_t byte = (uint8_t)strtol(byte_str, nullptr, 16);
        result.append(&byte, 1);
    }
    return result;
}

void testWireConstants() {
    // Verify wire format constants match Python LXMF
    TEST_ASSERT_EQUAL_INT(16, Wire::DESTINATION_LENGTH);
    TEST_ASSERT_EQUAL_INT(64, Wire::SIGNATURE_LENGTH);
    TEST_ASSERT_EQUAL_INT(111, Wire::LXMF_OVERHEAD);  // 16+16+64+9+6
    TEST_ASSERT_EQUAL_INT(9, Wire::TIMESTAMP_SIZE);   // msgpack float64: marker + 8 bytes
    TEST_ASSERT_EQUAL_INT(6, Wire::STRUCT_OVERHEAD);  // fixarray(4) + 2*bin8(0) + fixmap(0)
}

void testMessageStates() {
    // Verify state enum values match Python
    TEST_ASSERT_EQUAL_INT(0x00, static_cast<uint8_t>(MessageState::GENERATING));
    TEST_ASSERT_EQUAL_INT(0x01, static_cast<uint8_t>(MessageState::OUTBOUND));
    TEST_ASSERT_EQUAL_INT(0x02, static_cast<uint8_t>(MessageState::SENDING));
    TEST_ASSERT_EQUAL_INT(0x04, static_cast<uint8_t>(MessageState::SENT));
    TEST_ASSERT_EQUAL_INT(0x08, static_cast<uint8_t>(MessageState::DELIVERED));
    TEST_ASSERT_EQUAL_INT(0xFD, static_cast<uint8_t>(MessageState::REJECTED));
    TEST_ASSERT_EQUAL_INT(0xFE, static_cast<uint8_t>(MessageState::CANCELLED));
    TEST_ASSERT_EQUAL_INT(0xFF, static_cast<uint8_t>(MessageState::FAILED));
}

void testDeliveryMethods() {
    // Verify delivery method enum values match Python
    TEST_ASSERT_EQUAL_INT(0x00, static_cast<uint8_t>(DeliveryMethod::UNKNOWN));
    TEST_ASSERT_EQUAL_INT(0x01, static_cast<uint8_t>(DeliveryMethod::OPPORTUNISTIC));
    TEST_ASSERT_EQUAL_INT(0x02, static_cast<uint8_t>(DeliveryMethod::DIRECT));
    TEST_ASSERT_EQUAL_INT(0x03, static_cast<uint8_t>(DeliveryMethod::PROPAGATED));
}

void testNoneConstructor() {
    // Test Type::NONE constructor pattern
    LXMessage msg{Type::NONE};
    TEST_ASSERT_FALSE(msg);  // Should be falsy
    TEST_ASSERT_EQUAL_INT(0, msg.packed_size());
}

void testCopyConstructor() {
    // Create a message
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    LXMessage msg1 = LXMessage::create(receiver_dest, sender_dest, "Test content");
    TEST_ASSERT_TRUE(msg1);

    // Copy constructor should share the same object
    LXMessage msg2(msg1);
    TEST_ASSERT_TRUE(msg2);
    TEST_ASSERT_TRUE(msg1 == msg2);  // Same underlying object
}

void testSimpleMessagePack() {
    // Create identities from test keys
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    // Create destinations
    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Create message with known content
    std::map<uint8_t, Bytes> fields;
    LXMessage msg = LXMessage::create(receiver_dest, sender_dest, "This is a test message", "Hello", fields, DeliveryMethod::DIRECT);

    // Verify initial state
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(MessageState::GENERATING),
                          static_cast<uint8_t>(msg.state()));

    // Pack the message
    TEST_ASSERT_TRUE(msg.pack());

    // Verify state changed
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(MessageState::OUTBOUND),
                          static_cast<uint8_t>(msg.state()));

    // Verify packed size is reasonable (should be around 138 bytes for this message)
    TEST_ASSERT_GREATER_THAN(Wire::LXMF_OVERHEAD, msg.packed_size());
    TEST_ASSERT_LESS_THAN(300, msg.packed_size());

    // Verify message has a hash
    TEST_ASSERT_EQUAL_INT(32, msg.hash().size());

    // Verify signature is present
    TEST_ASSERT_EQUAL_INT(64, msg.signature().size());

    // Verify content is preserved
    TEST_ASSERT_EQUAL_STRING("Hello", msg.title_as_string().c_str());
    TEST_ASSERT_EQUAL_STRING("This is a test message", msg.content_as_string().c_str());

    // Verify signature was validated during pack
    TEST_ASSERT_TRUE(msg.signature_validated());
}

void testEmptyMessagePack() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Empty message
    LXMessage msg = LXMessage::create(receiver_dest, sender_dest, "", "");
    TEST_ASSERT_TRUE(msg.pack());

    // Empty message should still have overhead
    TEST_ASSERT_GREATER_OR_EQUAL(Wire::LXMF_OVERHEAD - 1, msg.packed_size());  // -1 for empty payload optimization

    // Content should be empty
    TEST_ASSERT_EQUAL_STRING("", msg.title_as_string().c_str());
    TEST_ASSERT_EQUAL_STRING("", msg.content_as_string().c_str());
}

void testUnpackFromBytes() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Remember identities using destination hashes (not identity hashes)
    // This is how they would be recalled from announce data
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());
    Identity::remember(Bytes(), receiver_dest.hash(), receiver.get_public_key(), Bytes());

    // Create and pack a message
    LXMessage original = LXMessage::create(receiver_dest, sender_dest, "Test content", "Test title");
    TEST_ASSERT_TRUE(original.pack());

    // Unpack from the packed bytes
    LXMessage unpacked = LXMessage::unpack_from_bytes(original.packed(), DeliveryMethod::DIRECT);
    TEST_ASSERT_TRUE(unpacked);

    // Verify unpacked content matches
    TEST_ASSERT_EQUAL_STRING(original.title_as_string().c_str(), unpacked.title_as_string().c_str());
    TEST_ASSERT_EQUAL_STRING(original.content_as_string().c_str(), unpacked.content_as_string().c_str());

    // Verify hashes match
    TEST_ASSERT_TRUE(original.hash() == unpacked.hash());

    // Verify destination/source hashes
    TEST_ASSERT_TRUE(original.destination_hash() == unpacked.destination_hash());
    TEST_ASSERT_TRUE(original.source_hash() == unpacked.source_hash());

    // Verify timestamp matches (compare as int64 to avoid Unity double precision requirement)
    TEST_ASSERT_EQUAL_INT64((int64_t)original.timestamp(), (int64_t)unpacked.timestamp());

    // Unpacked message should be marked as incoming
    TEST_ASSERT_TRUE(unpacked.incoming());

    // Signature should be validated (since we remembered the identity)
    TEST_ASSERT_TRUE(unpacked.signature_validated());
}

void testMessageWithFields() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Create message with fields
    std::map<uint8_t, Bytes> fields;
    fields[Fields::RENDERER] = Bytes({Renderer::MARKDOWN});

    LXMessage msg = LXMessage::create(receiver_dest, sender_dest, "Content with fields", "Test", fields);
    TEST_ASSERT_TRUE(msg.pack());

    // Verify fields are preserved
    TEST_ASSERT_EQUAL_INT(1, msg.fields().size());
    TEST_ASSERT_TRUE(msg.fields().count(Fields::RENDERER) > 0);

    // Remember identities for unpack (use destination hashes)
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());
    Identity::remember(Bytes(), receiver_dest.hash(), receiver.get_public_key(), Bytes());

    // Unpack and verify fields
    LXMessage unpacked = LXMessage::unpack_from_bytes(msg.packed());
    TEST_ASSERT_TRUE(unpacked);
    TEST_ASSERT_EQUAL_INT(1, unpacked.fields().size());
}

void testBinaryContent() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Binary content
    const uint8_t binary_data[] = {0x00, 0x01, 0x02, 0xff, 0xfe, 0xfd};
    Bytes binary_content(binary_data, sizeof(binary_data));

    LXMessage msg(receiver_dest, sender_dest, binary_content, Bytes("Binary"), {});
    TEST_ASSERT_TRUE(msg.pack());

    // Verify binary content is preserved
    TEST_ASSERT_EQUAL_INT(6, msg.content().size());
    TEST_ASSERT_EQUAL_HEX8(0x00, msg.content()[0]);
    TEST_ASSERT_EQUAL_HEX8(0xff, msg.content()[3]);

    // Unpack and verify
    Identity::remember(Bytes(), sender.hash(), sender.get_public_key(), Bytes());
    Identity::remember(Bytes(), receiver.hash(), receiver.get_public_key(), Bytes());

    LXMessage unpacked = LXMessage::unpack_from_bytes(msg.packed());
    TEST_ASSERT_TRUE(unpacked);
    TEST_ASSERT_TRUE(msg.content() == unpacked.content());
}

void testRepresentationDetermination() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Small message should use PACKET representation
    LXMessage small_msg = LXMessage::create(receiver_dest, sender_dest, "Small");
    TEST_ASSERT_TRUE(small_msg.pack());
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(Representation::PACKET),
                          static_cast<uint8_t>(small_msg.representation()));

    // Large message should use RESOURCE representation
    std::string large_content(400, 'X');  // Bigger than LINK_PACKET_MAX_CONTENT
    LXMessage large_msg = LXMessage::create(receiver_dest, sender_dest, large_content);
    TEST_ASSERT_TRUE(large_msg.pack());
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(Representation::RESOURCE),
                          static_cast<uint8_t>(large_msg.representation()));
}

void testOpportunisticFallback() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Large message with OPPORTUNISTIC should fall back to DIRECT
    std::string large_content(300, 'X');  // Bigger than ENCRYPTED_PACKET_MAX_CONTENT
    LXMessage msg = LXMessage::create(receiver_dest, sender_dest, large_content, "", {}, DeliveryMethod::OPPORTUNISTIC);
    TEST_ASSERT_TRUE(msg.pack());

    // Should have fallen back to DIRECT
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(DeliveryMethod::DIRECT),
                          static_cast<uint8_t>(msg.method()));
}

void testInvalidUnpack() {
    // Too short data
    const uint8_t short_bytes[] = {0x01, 0x02, 0x03};
    Bytes short_data(short_bytes, sizeof(short_bytes));
    LXMessage msg1 = LXMessage::unpack_from_bytes(short_data);
    TEST_ASSERT_FALSE(msg1);

    // Invalid msgpack payload
    Bytes invalid_data(120);  // All zeros
    LXMessage msg2 = LXMessage::unpack_from_bytes(invalid_data);
    TEST_ASSERT_FALSE(msg2);
}

void testContentSetters() {
    LXMessage msg{Type::NONE};

    // Setting content on null message should create object
    msg.set_content(std::string("Test content"));
    TEST_ASSERT_TRUE(msg);
    TEST_ASSERT_EQUAL_STRING("Test content", msg.content_as_string().c_str());

    msg.set_title(std::string("Test title"));
    TEST_ASSERT_EQUAL_STRING("Test title", msg.title_as_string().c_str());
}

//=============================================================================
// LXMRouter Tests
//=============================================================================

void testRouterConstruction() {
    // Router should construct without error
    LXMRouter router;

    // Initial state should be empty
    TEST_ASSERT_FALSE(router.delivery_destination());
    TEST_ASSERT_FALSE(router.delivery_identity());
    TEST_ASSERT_EQUAL_INT(0, router.pending_outbound_count());
    TEST_ASSERT_EQUAL_INT(0, router.messages_sent());
    TEST_ASSERT_EQUAL_INT(0, router.messages_received());
}

void testRouterRegisterDelivery() {
    LXMRouter router;

    // Create test identity
    Identity identity = createTestIdentity(TEST_SENDER_PRIV_KEY);
    TEST_ASSERT_TRUE(identity);

    // Register delivery identity
    Destination dest = router.register_delivery_identity(identity, "Test Node", 0);

    // Should have created a valid destination
    TEST_ASSERT_TRUE(dest);
    TEST_ASSERT_TRUE(router.delivery_destination());
    TEST_ASSERT_TRUE(router.delivery_identity());

    // Destination should be IN direction, SINGLE type
    TEST_ASSERT_EQUAL_INT(Type::Destination::IN, dest.direction());
    TEST_ASSERT_EQUAL_INT(Type::Destination::SINGLE, dest.type());
}

void testRouterQueueOutbound() {
    LXMRouter router;

    // Create identities
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Create a message
    LXMessage msg = LXMessage::create(receiver_dest, sender_dest, "Test content", "Test");

    // Queue for outbound
    TEST_ASSERT_TRUE(router.handle_outbound(msg));

    // Should have one pending message
    TEST_ASSERT_EQUAL_INT(1, router.pending_outbound_count());

    // Message state should be OUTBOUND
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(MessageState::OUTBOUND),
                          static_cast<uint8_t>(msg.state()));
}

void testRouterCancelOutbound() {
    LXMRouter router;

    // Create identities
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Create and queue a message
    LXMessage msg = LXMessage::create(receiver_dest, sender_dest, "Test content");
    router.handle_outbound(msg);
    TEST_ASSERT_EQUAL_INT(1, router.pending_outbound_count());

    // Cancel the message
    TEST_ASSERT_TRUE(router.cancel_outbound(msg));

    // Should have no pending messages
    TEST_ASSERT_EQUAL_INT(0, router.pending_outbound_count());

    // Message state should be CANCELLED
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(MessageState::CANCELLED),
                          static_cast<uint8_t>(msg.state()));
}

//=============================================================================
// Opportunistic Delivery Tests
//=============================================================================

void testOpportunisticPacking() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Create message for opportunistic delivery
    LXMessage msg(
        receiver_dest,
        sender_dest,
        Bytes((const uint8_t*)"Short message", 13),
        Bytes((const uint8_t*)"Title", 5),
        {},
        DeliveryMethod::OPPORTUNISTIC
    );

    // Pack it
    TEST_ASSERT_TRUE(msg.pack());

    // Check full packed size
    TEST_ASSERT_TRUE(msg.packed().size() > 0);

    // Get opportunistic format (without dest_hash)
    Bytes opp_packed = msg.packed_opportunistic();
    TEST_ASSERT_TRUE(opp_packed.size() > 0);

    // Should be 16 bytes shorter than full packed
    TEST_ASSERT_EQUAL_INT(msg.packed().size() - Wire::DESTINATION_LENGTH, opp_packed.size());

    // First 16 bytes of full packed should be dest_hash
    TEST_ASSERT_TRUE(msg.packed().mid(0, Wire::DESTINATION_LENGTH) == receiver_dest.hash());

    // Rest of full packed should match opportunistic
    TEST_ASSERT_TRUE(msg.packed().mid(Wire::DESTINATION_LENGTH) == opp_packed);

    // Method should be OPPORTUNISTIC
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(DeliveryMethod::OPPORTUNISTIC),
                          static_cast<uint8_t>(msg.method()));

    // Representation should be PACKET (small message)
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(Representation::PACKET),
                          static_cast<uint8_t>(msg.representation()));
}

void testOpportunisticUnpack() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Remember sender identity for signature validation
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    // Create and pack a message
    LXMessage original(
        receiver_dest,
        sender_dest,
        Bytes((const uint8_t*)"Opportunistic content", 21),
        Bytes((const uint8_t*)"Opp Title", 9),
        {},
        DeliveryMethod::OPPORTUNISTIC
    );
    TEST_ASSERT_TRUE(original.pack());

    // Get opportunistic format
    Bytes opp_packed = original.packed_opportunistic();

    // Unpack using the opportunistic method
    LXMessage unpacked = LXMessage::unpack_from_opportunistic(opp_packed, receiver_dest);

    // Verify message was unpacked correctly
    TEST_ASSERT_TRUE(unpacked);

    // Check content
    TEST_ASSERT_EQUAL_INT(original.content().size(), unpacked.content().size());
    TEST_ASSERT_TRUE(original.content() == unpacked.content());

    // Check title
    TEST_ASSERT_EQUAL_INT(original.title().size(), unpacked.title().size());
    TEST_ASSERT_TRUE(original.title() == unpacked.title());

    // Check destination hash was reconstructed correctly
    TEST_ASSERT_TRUE(original.destination_hash() == unpacked.destination_hash());

    // Check source hash
    TEST_ASSERT_TRUE(original.source_hash() == unpacked.source_hash());

    // Check hash matches
    TEST_ASSERT_TRUE(original.hash() == unpacked.hash());

    // Verify method was set correctly
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(DeliveryMethod::OPPORTUNISTIC),
                          static_cast<uint8_t>(unpacked.method()));

    // Signature should validate
    TEST_ASSERT_TRUE(unpacked.signature_validated());
}

void testOpportunisticRoundTrip() {
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Remember sender identity
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    // Test with various content sizes
    const char* test_contents[] = {
        "A",
        "Short message",
        "This is a somewhat longer message for testing opportunistic delivery format",
    };

    for (const char* content : test_contents) {
        size_t len = strlen(content);

        LXMessage original(
            receiver_dest,
            sender_dest,
            Bytes((const uint8_t*)content, len),
            Bytes((const uint8_t*)"Test", 4),
            {},
            DeliveryMethod::OPPORTUNISTIC
        );
        original.pack();

        // Simulate opportunistic send/receive
        Bytes wire_data = original.packed_opportunistic();
        LXMessage received = LXMessage::unpack_from_opportunistic(wire_data, receiver_dest);

        // Verify
        TEST_ASSERT_TRUE(received);
        TEST_ASSERT_EQUAL_INT(len, received.content().size());
        std::string received_content = received.content_as_string();
        TEST_ASSERT_EQUAL_STRING(content, received_content.c_str());
        TEST_ASSERT_TRUE(original.hash() == received.hash());
    }
}

//=============================================================================
// Python Interoperability Tests
// These test vectors were generated by Python LXMF and must be unpacked by C++
//=============================================================================

// Python-generated test vector: simple_message
static const char* PY_SIMPLE_PACKED_HEX =
    "d8712e3207f8c7e25692d2d34168201f33e49dde9a96f3952b456cc2afc1e058"
    "8370885e7ccd9578bc73c683a9be251bb2937beb4812d6491113a137c44fe863"
    "9bd6ade088e09a123abc015c4b19fdd24a61bdd01ff78b3b393c4a1e8484ec0c"
    "94cb41d954fc40000000c40454657374c41248656c6c6f2066726f6d20507974686f6e2180";
static const char* PY_SIMPLE_HASH_HEX = "46758053ab378a55cfba98ac213085405a208a146b442e06d96547aee9a396f3";

// Python-generated test vector: empty_message
static const char* PY_EMPTY_PACKED_HEX =
    "d8712e3207f8c7e25692d2d34168201f33e49dde9a96f3952b456cc2afc1e058"
    "0364b03120cdabe77001c6472c65b80c86e62cd5c5b148768d548c258b41b985"
    "5b60643d5e024508e3c010e904b4995ec1bbd21c349a718a29e2e8a200f84804"
    "94cb41d954fc40400000c400c40080";
static const char* PY_EMPTY_HASH_HEX = "6cbdb21ff826a6f3a5047b0dbf05c160442cd6b960ca84044b07a504392bc2d2";

// Python-generated test vector: opportunistic_message (full format)
static const char* PY_OPP_PACKED_HEX =
    "d8712e3207f8c7e25692d2d34168201f33e49dde9a96f3952b456cc2afc1e058"
    "375562f92fcf4b10fd5cf064ff35b30b7efebb65a1ef596b12c737963979ff38"
    "8d2197884653d0393e607cb65b86fc6a1d263c43ef92200c138cf291a91bd603"
    "94cb41d954fc40c00000c4034f7070c40e4f70706f7274756e69737469632180";
// Opportunistic format (no dest_hash)
static const char* PY_OPP_PACKED_OPP_HEX =
    "33e49dde9a96f3952b456cc2afc1e058"
    "375562f92fcf4b10fd5cf064ff35b30b7efebb65a1ef596b12c737963979ff38"
    "8d2197884653d0393e607cb65b86fc6a1d263c43ef92200c138cf291a91bd603"
    "94cb41d954fc40c00000c4034f7070c40e4f70706f7274756e69737469632180";
static const char* PY_OPP_HASH_HEX = "ec0082537132056957d2e058292fa070dc7b1bc217f972a03a6a292037087cee";

// Python-generated test vector: binary_message
static const char* PY_BINARY_PACKED_HEX =
    "d8712e3207f8c7e25692d2d34168201f33e49dde9a96f3952b456cc2afc1e058"
    "71e7c42b66c69d7f5d1e236c68f461950a040935e2b22b6780e5eb49d2e77602"
    "344e1d4afb374f033736904741e58e227a630d1887323b8d383989c701eec004"
    "94cb41d954fc41000000c40642696e617279c406000102fffefd80";
static const char* PY_BINARY_HASH_HEX = "02bde505fcd600f42aed3835a82ac652189f9dbfb58c7c347a0f525dbe03e89b";

// Sender public key for signature validation
static const char* SENDER_PUBLIC_KEY_HEX =
    "e91e79b4de059792ab2e0b883450a04c23da3d810d72784f7ad02e6502222952"
    "568fa5531c7d74d27140d1ae964ff3c0b19f556fc23a879b251495fc1ba78631";

// Expected destination hashes from Python
static const char* PY_SENDER_DEST_HASH_HEX = "33e49dde9a96f3952b456cc2afc1e058";
static const char* PY_RECEIVER_DEST_HASH_HEX = "d8712e3207f8c7e25692d2d34168201f";

// Expected public keys from Python (generated from same private keys)
static const char* PY_SENDER_PUBLIC_KEY_HEX =
    "e91e79b4de059792ab2e0b883450a04c23da3d810d72784f7ad02e6502222952"
    "568fa5531c7d74d27140d1ae964ff3c0b19f556fc23a879b251495fc1ba78631";
static const char* PY_RECEIVER_PUBLIC_KEY_HEX =
    "13d492634f816aed4b679c0eb6d0c994bbc4321b175681e4e52357c76eff6568"
    "0c9b093a15c4384414c8f4f3db4952646a4b365bb3f647d1fe7ec6c65983c962";

void testPublicKeysMatchPython() {
    // First verify the public keys match between C++ and Python
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Bytes expected_sender_pubkey = hexToBytes(PY_SENDER_PUBLIC_KEY_HEX);
    Bytes expected_receiver_pubkey = hexToBytes(PY_RECEIVER_PUBLIC_KEY_HEX);

    // Debug: print what C++ generates (will show in test output)
    printf("\nC++ sender public key:   %s\n", sender.get_public_key().toHex().c_str());
    printf("Python sender public key: %s\n", PY_SENDER_PUBLIC_KEY_HEX);
    printf("C++ receiver public key:   %s\n", receiver.get_public_key().toHex().c_str());
    printf("Python receiver public key: %s\n", PY_RECEIVER_PUBLIC_KEY_HEX);

    // Check sender public key
    TEST_ASSERT_EQUAL_INT(64, sender.get_public_key().size());
    TEST_ASSERT_TRUE(sender.get_public_key() == expected_sender_pubkey);

    // Check receiver public key
    TEST_ASSERT_EQUAL_INT(64, receiver.get_public_key().size());
    TEST_ASSERT_TRUE(receiver.get_public_key() == expected_receiver_pubkey);
}

void testDestinationHashesMatchPython() {
    // Verify that C++ generates the same destination hashes as Python
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Verify sender destination hash matches Python
    Bytes expected_sender_hash = hexToBytes(PY_SENDER_DEST_HASH_HEX);
    TEST_ASSERT_EQUAL_INT(16, sender_dest.hash().size());
    TEST_ASSERT_TRUE(sender_dest.hash() == expected_sender_hash);

    // Verify receiver destination hash matches Python
    Bytes expected_receiver_hash = hexToBytes(PY_RECEIVER_DEST_HASH_HEX);
    TEST_ASSERT_EQUAL_INT(16, receiver_dest.hash().size());
    TEST_ASSERT_TRUE(receiver_dest.hash() == expected_receiver_hash);
}

void testPythonSimpleMessage() {
    // Set up sender identity for signature validation
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    // Parse the packed message
    Bytes packed = hexToBytes(PY_SIMPLE_PACKED_HEX);
    TEST_ASSERT_TRUE(packed.size() > 0);

    // Unpack
    LXMessage msg = LXMessage::unpack_from_bytes(packed);
    TEST_ASSERT_TRUE(msg);

    // Verify the source hash in the message matches our sender destination hash
    TEST_ASSERT_TRUE(msg.source_hash() == sender_dest.hash());

    // Verify hash
    Bytes expected_hash = hexToBytes(PY_SIMPLE_HASH_HEX);
    TEST_ASSERT_TRUE(msg.hash() == expected_hash);

    // Verify content
    TEST_ASSERT_EQUAL_STRING("Test", msg.title_as_string().c_str());
    TEST_ASSERT_EQUAL_STRING("Hello from Python!", msg.content_as_string().c_str());

    // Verify timestamp
    TEST_ASSERT_EQUAL_INT64((int64_t)1700000000, (int64_t)msg.timestamp());

    // Verify signature
    TEST_ASSERT_TRUE(msg.signature_validated());
}

void testPythonEmptyMessage() {
    // Set up sender identity
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    Bytes packed = hexToBytes(PY_EMPTY_PACKED_HEX);
    LXMessage msg = LXMessage::unpack_from_bytes(packed);

    TEST_ASSERT_TRUE(msg);

    // Verify hash
    Bytes expected_hash = hexToBytes(PY_EMPTY_HASH_HEX);
    TEST_ASSERT_TRUE(msg.hash() == expected_hash);

    // Verify empty content
    TEST_ASSERT_EQUAL_INT(0, msg.title().size());
    TEST_ASSERT_EQUAL_INT(0, msg.content().size());

    // Verify signature
    TEST_ASSERT_TRUE(msg.signature_validated());
}

void testPythonOpportunisticMessage() {
    // Set up identities
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::IN, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    // Test full format unpack
    Bytes packed_full = hexToBytes(PY_OPP_PACKED_HEX);
    LXMessage msg_full = LXMessage::unpack_from_bytes(packed_full);
    TEST_ASSERT_TRUE(msg_full);

    Bytes expected_hash = hexToBytes(PY_OPP_HASH_HEX);
    TEST_ASSERT_TRUE(msg_full.hash() == expected_hash);
    TEST_ASSERT_EQUAL_STRING("Opp", msg_full.title_as_string().c_str());
    TEST_ASSERT_EQUAL_STRING("Opportunistic!", msg_full.content_as_string().c_str());

    // Test opportunistic format unpack (no dest_hash)
    Bytes packed_opp = hexToBytes(PY_OPP_PACKED_OPP_HEX);
    LXMessage msg_opp = LXMessage::unpack_from_opportunistic(packed_opp, receiver_dest);
    TEST_ASSERT_TRUE(msg_opp);

    // Should produce same hash when reconstructed
    TEST_ASSERT_TRUE(msg_opp.hash() == expected_hash);
    TEST_ASSERT_EQUAL_STRING("Opp", msg_opp.title_as_string().c_str());
    TEST_ASSERT_EQUAL_STRING("Opportunistic!", msg_opp.content_as_string().c_str());

    // Both should have valid signatures
    TEST_ASSERT_TRUE(msg_full.signature_validated());
    TEST_ASSERT_TRUE(msg_opp.signature_validated());
}

void testPythonBinaryMessage() {
    // Set up sender identity
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    Bytes packed = hexToBytes(PY_BINARY_PACKED_HEX);
    LXMessage msg = LXMessage::unpack_from_bytes(packed);

    TEST_ASSERT_TRUE(msg);

    // Verify hash
    Bytes expected_hash = hexToBytes(PY_BINARY_HASH_HEX);
    TEST_ASSERT_TRUE(msg.hash() == expected_hash);

    // Verify binary content
    Bytes expected_content = hexToBytes("000102fffefd");
    TEST_ASSERT_EQUAL_INT(6, msg.content().size());
    TEST_ASSERT_TRUE(msg.content() == expected_content);

    // Verify signature
    TEST_ASSERT_TRUE(msg.signature_validated());
}

void testCppToPythonRoundTrip() {
    // Create a message in C++ and verify it can be unpacked
    // (We compare the packed format to what Python produces with same inputs)
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Create message with same parameters as Python simple_message
    LXMessage msg(
        receiver_dest,
        sender_dest,
        Bytes((const uint8_t*)"Hello from Python!", 18),
        Bytes((const uint8_t*)"Test", 4),
        {},
        DeliveryMethod::DIRECT
    );

    // Set same timestamp as Python
    msg.set_timestamp(1700000000.0);

    // Pack
    TEST_ASSERT_TRUE(msg.pack());

    // Verify we get the same hash (this proves payload is identical)
    Bytes expected_hash = hexToBytes(PY_SIMPLE_HASH_HEX);
    TEST_ASSERT_TRUE(msg.hash() == expected_hash);

    // Verify the packed format structure is correct
    // First 16 bytes = dest hash
    TEST_ASSERT_TRUE(msg.packed().mid(0, 16) == receiver_dest.hash());
    // Next 16 bytes = src hash
    TEST_ASSERT_TRUE(msg.packed().mid(16, 16) == sender_dest.hash());
    // Signature is 64 bytes
    TEST_ASSERT_EQUAL_INT(64, msg.signature().size());
}

// Global callback counter for testing
static int delivery_callback_count = 0;
static void test_delivery_callback(const LXMessage& message) {
    delivery_callback_count++;
}

void testRouterDeliveryCallback() {
    // Register callback - simple test without creating destinations
    delivery_callback_count = 0;

    {
        LXMRouter router;
        router.register_delivery_callback(test_delivery_callback);

        // Callback should not have been called yet
        TEST_ASSERT_EQUAL_INT(0, delivery_callback_count);
    }
}

//=============================================================================
// Additional Coverage Tests
//=============================================================================

void testSignatureValidationUnknownIdentity() {
    // When sender identity is not known, signature should NOT be validated
    // This is the case seen in live testing where Python sends to C++
    // and C++ hasn't received an announce yet

    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Create and pack a message
    LXMessage original = LXMessage::create(receiver_dest, sender_dest, "Test content", "Test");
    TEST_ASSERT_TRUE(original.pack());

    // Clear any remembered identities to simulate unknown sender
    // (Note: We don't have a clear function, so we just don't remember the sender)

    // Create a temporary identity storage context
    // We'll unpack without remembering the sender identity
    Identity unknown_sender(false);  // Create with different keys
    Bytes fake_public_key(64);  // All zeros
    // Remember a different identity for the source hash (simulating wrong/no identity)
    // Actually, let's just not remember anything for this destination

    // For this test, we unpack a Python-generated message without setting up the identity
    Bytes packed = hexToBytes(PY_SIMPLE_PACKED_HEX);

    // Try to unpack without having the sender identity remembered
    // First, let's make sure the known destinations are cleared for this source
    // Since we can't easily clear, we'll test by using a message packed by a different identity

    // Create message with unknown sender (not in known destinations)
    Identity unknown = Identity();  // Random new identity
    Destination unknown_dest(unknown, Type::Destination::OUT, Type::Destination::SINGLE,
                             LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    LXMessage msg_from_unknown = LXMessage::create(receiver_dest, unknown_dest, "From unknown");
    TEST_ASSERT_TRUE(msg_from_unknown.pack());

    // Unpack it without remembering the identity
    LXMessage unpacked = LXMessage::unpack_from_bytes(msg_from_unknown.packed());
    TEST_ASSERT_TRUE(unpacked);

    // Signature should NOT be validated since we don't know the sender
    TEST_ASSERT_FALSE(unpacked.signature_validated());

    // Message should still be successfully unpacked though
    TEST_ASSERT_EQUAL_STRING("From unknown", unpacked.content_as_string().c_str());
}

void testMaxOpportunisticSize() {
    // Test opportunistic delivery size limits
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Test various message sizes
    // Small message should stay OPPORTUNISTIC
    std::string small_content(50, 'A');
    LXMessage small_msg = LXMessage::create(receiver_dest, sender_dest, small_content, "", {}, DeliveryMethod::OPPORTUNISTIC);
    TEST_ASSERT_TRUE(small_msg.pack());
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(DeliveryMethod::OPPORTUNISTIC),
                          static_cast<uint8_t>(small_msg.method()));

    // Very large message should fall back to DIRECT
    std::string large_content(500, 'B');
    LXMessage large_msg = LXMessage::create(receiver_dest, sender_dest, large_content, "", {}, DeliveryMethod::OPPORTUNISTIC);
    TEST_ASSERT_TRUE(large_msg.pack());
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(DeliveryMethod::DIRECT),
                          static_cast<uint8_t>(large_msg.method()));

    // Find the boundary by binary search (for documentation purposes)
    int low = 50, high = 500;
    while (high - low > 1) {
        int mid = (low + high) / 2;
        std::string test_content(mid, 'X');
        LXMessage test_msg = LXMessage::create(receiver_dest, sender_dest, test_content, "", {}, DeliveryMethod::OPPORTUNISTIC);
        test_msg.pack();
        if (test_msg.method() == DeliveryMethod::OPPORTUNISTIC) {
            low = mid;
        } else {
            high = mid;
        }
    }

    // Document the boundary we found (informational only, not a strict assertion)
    // The boundary depends on LXMF overhead + msgpack encoding
    printf("  Opportunistic boundary: content size %d bytes -> OPPORTUNISTIC, %d bytes -> DIRECT\n", low, high);

    // Verify messages at the boundary
    std::string at_limit(low, 'Y');
    LXMessage limit_msg = LXMessage::create(receiver_dest, sender_dest, at_limit, "", {}, DeliveryMethod::OPPORTUNISTIC);
    TEST_ASSERT_TRUE(limit_msg.pack());
    // Should be OPPORTUNISTIC at the limit
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(DeliveryMethod::OPPORTUNISTIC),
                          static_cast<uint8_t>(limit_msg.method()));

    std::string over_limit(high, 'Z');
    LXMessage over_msg = LXMessage::create(receiver_dest, sender_dest, over_limit, "", {}, DeliveryMethod::OPPORTUNISTIC);
    TEST_ASSERT_TRUE(over_msg.pack());
    // Should fall back to DIRECT over the limit
    TEST_ASSERT_EQUAL_INT(static_cast<uint8_t>(DeliveryMethod::DIRECT),
                          static_cast<uint8_t>(over_msg.method()));
}

void testUnicodeContent() {
    // Test handling of Unicode/UTF-8 content
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Remember identity for unpacking
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    // Test various Unicode strings
    const char* unicode_content = "Hello 世界! Привет мир! 🌍🚀";
    const char* unicode_title = "Тест 测试";

    LXMessage msg = LXMessage::create(receiver_dest, sender_dest, unicode_content, unicode_title);
    TEST_ASSERT_TRUE(msg.pack());

    // Unpack and verify
    LXMessage unpacked = LXMessage::unpack_from_bytes(msg.packed());
    TEST_ASSERT_TRUE(unpacked);

    // Content should be preserved byte-for-byte
    TEST_ASSERT_EQUAL_STRING(unicode_content, unpacked.content_as_string().c_str());
    TEST_ASSERT_EQUAL_STRING(unicode_title, unpacked.title_as_string().c_str());

    // Verify hash matches (proves byte-exact preservation)
    TEST_ASSERT_TRUE(msg.hash() == unpacked.hash());
}

void testAnnounceRegistration() {
    // Test that announce with display_name and stamp_cost can be registered
    LXMRouter router;

    // Register with display name and stamp cost
    Identity identity = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Destination dest = router.register_delivery_identity(identity, "Test Display Name", 8);

    // Should have created a valid destination
    TEST_ASSERT_TRUE(dest);
    TEST_ASSERT_TRUE(router.delivery_destination());

    // Destination should be valid LXMF delivery destination
    TEST_ASSERT_EQUAL_INT(Type::Destination::IN, dest.direction());
    TEST_ASSERT_EQUAL_INT(Type::Destination::SINGLE, dest.type());
    TEST_ASSERT_EQUAL_INT(16, dest.hash().size());

    // Test registering with empty display name
    LXMRouter router2;
    Destination dest2 = router2.register_delivery_identity(identity, "", 0);
    TEST_ASSERT_TRUE(dest2);
    TEST_ASSERT_TRUE(router2.delivery_destination());
}

void testMessageTimestampPrecision() {
    // Test that timestamp is preserved with proper precision
    Identity sender = createTestIdentity(TEST_SENDER_PRIV_KEY);
    Identity receiver = createTestIdentity(TEST_RECEIVER_PRIV_KEY);

    Destination sender_dest(sender, Type::Destination::OUT, Type::Destination::SINGLE,
                            LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);
    Destination receiver_dest(receiver, Type::Destination::OUT, Type::Destination::SINGLE,
                              LXMF::APP_NAME, LXMF::ASPECT_DELIVERY);

    // Remember identity for unpacking
    Identity::remember(Bytes(), sender_dest.hash(), sender.get_public_key(), Bytes());

    // Test with various timestamps
    double test_timestamps[] = {
        0.0,                    // Unix epoch
        1700000000.0,           // 2023
        1700000000.123456,      // With fractional seconds
        2147483647.0,           // Max 32-bit signed
        4294967295.0,           // Max 32-bit unsigned
    };

    for (double ts : test_timestamps) {
        LXMessage msg = LXMessage::create(receiver_dest, sender_dest, "Test");
        msg.set_timestamp(ts);
        TEST_ASSERT_TRUE(msg.pack());

        LXMessage unpacked = LXMessage::unpack_from_bytes(msg.packed());
        TEST_ASSERT_TRUE(unpacked);

        // Compare as int64 for integer timestamps, or with tolerance for fractional
        if (ts == (double)(int64_t)ts) {
            TEST_ASSERT_EQUAL_INT64((int64_t)ts, (int64_t)unpacked.timestamp());
        } else {
            // For fractional timestamps, check they're close (IEEE 754 double precision)
            double diff = ts - unpacked.timestamp();
            if (diff < 0) diff = -diff;
            TEST_ASSERT_TRUE(diff < 0.001);  // Within 1ms
        }
    }
}

void setUp(void) {
    // Set up before each test
}

void tearDown(void) {
    // Clean up after each test
}

int runUnityTests(void) {
    UNITY_BEGIN();

    // Wire format constant tests
    RUN_TEST(testWireConstants);
    RUN_TEST(testMessageStates);
    RUN_TEST(testDeliveryMethods);

    // Constructor tests
    RUN_TEST(testNoneConstructor);
    RUN_TEST(testCopyConstructor);

    // Pack tests
    RUN_TEST(testSimpleMessagePack);
    RUN_TEST(testEmptyMessagePack);
    RUN_TEST(testMessageWithFields);
    RUN_TEST(testBinaryContent);

    // Unpack tests
    RUN_TEST(testUnpackFromBytes);
    RUN_TEST(testInvalidUnpack);

    // Delivery method tests
    RUN_TEST(testRepresentationDetermination);
    RUN_TEST(testOpportunisticFallback);

    // Other tests
    RUN_TEST(testContentSetters);

    // Opportunistic delivery tests
    RUN_TEST(testOpportunisticPacking);
    RUN_TEST(testOpportunisticUnpack);
    RUN_TEST(testOpportunisticRoundTrip);

    // Router tests
    RUN_TEST(testRouterConstruction);
    RUN_TEST(testRouterRegisterDelivery);
    RUN_TEST(testRouterQueueOutbound);
    RUN_TEST(testRouterCancelOutbound);
    RUN_TEST(testRouterDeliveryCallback);

    // Python interoperability tests (C++ unpacking Python-generated messages)
    RUN_TEST(testPublicKeysMatchPython);
    RUN_TEST(testDestinationHashesMatchPython);
    RUN_TEST(testPythonSimpleMessage);
    RUN_TEST(testPythonEmptyMessage);
    RUN_TEST(testPythonOpportunisticMessage);
    RUN_TEST(testPythonBinaryMessage);
    RUN_TEST(testCppToPythonRoundTrip);

    // Additional coverage tests
    RUN_TEST(testSignatureValidationUnknownIdentity);
    RUN_TEST(testMaxOpportunisticSize);
    RUN_TEST(testUnicodeContent);
    RUN_TEST(testAnnounceRegistration);
    RUN_TEST(testMessageTimestampPrecision);

    return UNITY_END();
}

// For native dev-platform
int main(void) {
    return runUnityTests();
}

#ifdef ARDUINO
// For Arduino framework
void setup() {
    delay(2000);
    runUnityTests();
}
void loop() {}
#endif

// For ESP-IDF framework
void app_main() {
    runUnityTests();
}
