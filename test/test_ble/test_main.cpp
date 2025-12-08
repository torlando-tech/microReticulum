/**
 * @file test_main.cpp
 * @brief Unit tests for BLE-Reticulum Protocol v2.2
 *
 * Comprehensive test coverage for:
 * - BLETypes: Protocol constants, UUIDs, timing values
 * - BLEFragmenter: Packet fragmentation
 * - BLEReassembler: Fragment reassembly
 * - BLEPeerManager: Peer tracking and scoring
 * - BLEIdentityManager: Identity handshake protocol
 */

#include <unity.h>

#include <Utilities/OS.h>
#include "BLE/BLETypes.h"
#include "BLE/BLEFragmenter.h"
#include "BLE/BLEReassembler.h"
#include "BLE/BLEPeerManager.h"
#include "BLE/BLEIdentityManager.h"
#include "Bytes.h"
#include "Log.h"

#include <map>
#include <cstring>

//=============================================================================
// Test 1: BLETypes - Protocol Constants & UUIDs
//=============================================================================

void testBLEProtocolVersion() {
    TEST_ASSERT_EQUAL_UINT8(2, RNS::BLE::PROTOCOL_VERSION_MAJOR);
    TEST_ASSERT_EQUAL_UINT8(2, RNS::BLE::PROTOCOL_VERSION_MINOR);
}

void testServiceUUIDs() {
    // Verify exact v2.2 protocol UUIDs
    TEST_ASSERT_EQUAL_STRING("37145b00-442d-4a94-917f-8f42c5da28e3", RNS::BLE::UUID::SERVICE);
    TEST_ASSERT_EQUAL_STRING("37145b00-442d-4a94-917f-8f42c5da28e4", RNS::BLE::UUID::TX_CHAR);
    TEST_ASSERT_EQUAL_STRING("37145b00-442d-4a94-917f-8f42c5da28e5", RNS::BLE::UUID::RX_CHAR);
    TEST_ASSERT_EQUAL_STRING("37145b00-442d-4a94-917f-8f42c5da28e6", RNS::BLE::UUID::IDENTITY_CHAR);
}

void testProtocolConstants() {
    // MTU constants
    TEST_ASSERT_EQUAL_UINT16(517, RNS::BLE::MTU::REQUESTED);
    TEST_ASSERT_EQUAL_UINT16(23, RNS::BLE::MTU::MINIMUM);

    // Timing constants (v2.2 spec)
    TEST_ASSERT_FLOAT_WITHIN(0.001, 15.0, RNS::BLE::Timing::KEEPALIVE_INTERVAL);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 30.0, RNS::BLE::Timing::REASSEMBLY_TIMEOUT);

    // Limits
    TEST_ASSERT_EQUAL_size_t(16, RNS::BLE::Limits::IDENTITY_SIZE);
    TEST_ASSERT_EQUAL_size_t(6, RNS::BLE::Limits::MAC_SIZE);
    TEST_ASSERT_EQUAL_size_t(7, RNS::BLE::Limits::MAX_PEERS);

    // Fragment header
    TEST_ASSERT_EQUAL_size_t(5, RNS::BLE::Fragment::HEADER_SIZE);
    TEST_ASSERT_EQUAL_UINT8(0x01, RNS::BLE::Fragment::START);
    TEST_ASSERT_EQUAL_UINT8(0x02, RNS::BLE::Fragment::CONTINUE);
    TEST_ASSERT_EQUAL_UINT8(0x03, RNS::BLE::Fragment::END);

    // Scoring weights (v2.2: 60/30/10)
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.60f, RNS::BLE::Scoring::RSSI_WEIGHT);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.30f, RNS::BLE::Scoring::HISTORY_WEIGHT);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.10f, RNS::BLE::Scoring::RECENCY_WEIGHT);
}

void testBLEAddress() {
    // Test construction
    uint8_t addr1[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t addr2[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    RNS::BLE::BLEAddress a1(addr1);
    RNS::BLE::BLEAddress a2(addr2);

    // Test comparison (MAC sorting for connection direction)
    TEST_ASSERT_TRUE(a1.isLowerThan(a2));
    TEST_ASSERT_FALSE(a2.isLowerThan(a1));

    // Test string conversion
    TEST_ASSERT_EQUAL_STRING("06:05:04:03:02:01", a1.toString().c_str());

    // Test fromString
    auto parsed = RNS::BLE::BLEAddress::fromString("AA:BB:CC:DD:EE:FF");
    TEST_ASSERT_EQUAL_UINT8(0xFF, parsed.addr[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, parsed.addr[5]);

    // Test toBytes
    RNS::Bytes b = a1.toBytes();
    TEST_ASSERT_EQUAL_size_t(6, b.size());

    // Test isZero
    RNS::BLE::BLEAddress zero;
    TEST_ASSERT_TRUE(zero.isZero());
    TEST_ASSERT_FALSE(a1.isZero());
}

//=============================================================================
// Test 2: BLEFragmenter - Packet Fragmentation
//=============================================================================

void testFragmenterMTU() {
    RNS::BLE::BLEFragmenter frag(23);  // Minimum BLE MTU
    TEST_ASSERT_EQUAL_size_t(23, frag.getMTU());
    TEST_ASSERT_EQUAL_size_t(18, frag.getPayloadSize());  // 23 - 5 header

    frag.setMTU(512);
    TEST_ASSERT_EQUAL_size_t(512, frag.getMTU());
    TEST_ASSERT_EQUAL_size_t(507, frag.getPayloadSize());
}

void testFragmenterSingleFragment() {
    RNS::BLE::BLEFragmenter frag(100);  // 95 byte payload

    RNS::Bytes small_data("Hello");  // 5 bytes - fits in one fragment
    TEST_ASSERT_FALSE(frag.needsFragmentation(small_data));
    TEST_ASSERT_EQUAL_UINT16(1, frag.calculateFragmentCount(small_data.size()));

    auto frags = frag.fragment(small_data);
    TEST_ASSERT_EQUAL_size_t(1, frags.size());
    TEST_ASSERT_EQUAL_size_t(10, frags[0].size());  // 5 header + 5 payload

    // Verify header
    RNS::BLE::Fragment::Type type;
    uint16_t seq, total;
    TEST_ASSERT_TRUE(RNS::BLE::BLEFragmenter::parseHeader(frags[0], type, seq, total));
    TEST_ASSERT_EQUAL_UINT8(RNS::BLE::Fragment::END, type);  // Single = END
    TEST_ASSERT_EQUAL_UINT16(0, seq);
    TEST_ASSERT_EQUAL_UINT16(1, total);
}

void testFragmenterMultipleFragments() {
    RNS::BLE::BLEFragmenter frag(23);  // 18 byte payload per fragment

    // Create 50-byte packet (needs 3 fragments: 18+18+14)
    RNS::Bytes data(50);
    uint8_t* buf = data.writable(50);
    for (int i = 0; i < 50; i++) buf[i] = static_cast<uint8_t>(i);
    data.resize(50);

    TEST_ASSERT_TRUE(frag.needsFragmentation(data));
    TEST_ASSERT_EQUAL_UINT16(3, frag.calculateFragmentCount(50));

    auto frags = frag.fragment(data);
    TEST_ASSERT_EQUAL_size_t(3, frags.size());

    // Verify first fragment (START)
    RNS::BLE::Fragment::Type type;
    uint16_t seq, total;
    TEST_ASSERT_TRUE(RNS::BLE::BLEFragmenter::parseHeader(frags[0], type, seq, total));
    TEST_ASSERT_EQUAL_UINT8(RNS::BLE::Fragment::START, type);
    TEST_ASSERT_EQUAL_UINT16(0, seq);
    TEST_ASSERT_EQUAL_UINT16(3, total);
    TEST_ASSERT_EQUAL_size_t(23, frags[0].size());

    // Verify middle fragment (CONTINUE)
    TEST_ASSERT_TRUE(RNS::BLE::BLEFragmenter::parseHeader(frags[1], type, seq, total));
    TEST_ASSERT_EQUAL_UINT8(RNS::BLE::Fragment::CONTINUE, type);
    TEST_ASSERT_EQUAL_UINT16(1, seq);
    TEST_ASSERT_EQUAL_UINT16(3, total);

    // Verify last fragment (END)
    TEST_ASSERT_TRUE(RNS::BLE::BLEFragmenter::parseHeader(frags[2], type, seq, total));
    TEST_ASSERT_EQUAL_UINT8(RNS::BLE::Fragment::END, type);
    TEST_ASSERT_EQUAL_UINT16(2, seq);
    TEST_ASSERT_EQUAL_UINT16(3, total);
}

void testFragmentHeaderFormat() {
    // Create fragment with known values
    RNS::Bytes payload("Test");
    auto frag = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::START, 0x1234, 0x5678, payload);

    // Verify 5-byte header format (big-endian)
    TEST_ASSERT_EQUAL_UINT8(0x01, frag.data()[0]);  // Type: START
    TEST_ASSERT_EQUAL_UINT8(0x12, frag.data()[1]);  // Seq high byte
    TEST_ASSERT_EQUAL_UINT8(0x34, frag.data()[2]);  // Seq low byte
    TEST_ASSERT_EQUAL_UINT8(0x56, frag.data()[3]);  // Total high byte
    TEST_ASSERT_EQUAL_UINT8(0x78, frag.data()[4]);  // Total low byte

    // Verify payload extraction
    RNS::Bytes extracted = RNS::BLE::BLEFragmenter::extractPayload(frag);
    TEST_ASSERT_EQUAL_size_t(4, extracted.size());
    TEST_ASSERT_EQUAL_MEMORY("Test", extracted.data(), 4);
}

void testFragmentValidation() {
    // Too short (< 5 bytes)
    RNS::Bytes too_short("Hi");
    TEST_ASSERT_FALSE(RNS::BLE::BLEFragmenter::isValidFragment(too_short));

    // Invalid type
    uint8_t invalid[] = {0xFF, 0x00, 0x01, 0x00, 0x01, 'X'};
    RNS::Bytes invalid_type(invalid, 6);
    TEST_ASSERT_FALSE(RNS::BLE::BLEFragmenter::isValidFragment(invalid_type));

    // Valid fragment
    uint8_t valid[] = {0x03, 0x00, 0x00, 0x00, 0x01, 'X'};
    RNS::Bytes valid_frag(valid, 6);
    TEST_ASSERT_TRUE(RNS::BLE::BLEFragmenter::isValidFragment(valid_frag));
}

//=============================================================================
// Test 3: BLEReassembler - Fragment Reassembly
//=============================================================================

static RNS::Bytes g_reassembled_packet;
static RNS::Bytes g_reassembled_identity;
static bool g_reassembly_called = false;

void reassemblyCallback(const RNS::Bytes& identity, const RNS::Bytes& packet) {
    g_reassembled_identity = identity;
    g_reassembled_packet = packet;
    g_reassembly_called = true;
}

void testReassemblerSingleFragment() {
    g_reassembly_called = false;
    RNS::BLE::BLEReassembler reassembler;
    reassembler.setReassemblyCallback(reassemblyCallback);

    RNS::Bytes identity(16);  // 16-byte peer identity
    memset(identity.writable(16), 0xAA, 16);
    identity.resize(16);

    // Create single-fragment packet (type=END, total=1)
    RNS::Bytes payload("Hello");
    auto frag = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::END, 0, 1, payload);

    TEST_ASSERT_TRUE(reassembler.processFragment(identity, frag));
    TEST_ASSERT_TRUE(g_reassembly_called);
    TEST_ASSERT_EQUAL_size_t(5, g_reassembled_packet.size());
    TEST_ASSERT_EQUAL_MEMORY("Hello", g_reassembled_packet.data(), 5);
}

void testReassemblerMultipleFragments() {
    g_reassembly_called = false;
    RNS::BLE::BLEReassembler reassembler;
    reassembler.setReassemblyCallback(reassemblyCallback);

    RNS::Bytes identity(16);
    memset(identity.writable(16), 0xBB, 16);
    identity.resize(16);

    // Fragment 1: START
    auto frag1 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::START, 0, 3, RNS::Bytes("AAA"));
    TEST_ASSERT_TRUE(reassembler.processFragment(identity, frag1));
    TEST_ASSERT_FALSE(g_reassembly_called);  // Not complete yet
    TEST_ASSERT_EQUAL_size_t(1, reassembler.pendingCount());

    // Fragment 2: CONTINUE
    auto frag2 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::CONTINUE, 1, 3, RNS::Bytes("BBB"));
    TEST_ASSERT_TRUE(reassembler.processFragment(identity, frag2));
    TEST_ASSERT_FALSE(g_reassembly_called);

    // Fragment 3: END
    auto frag3 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::END, 2, 3, RNS::Bytes("CCC"));
    TEST_ASSERT_TRUE(reassembler.processFragment(identity, frag3));
    TEST_ASSERT_TRUE(g_reassembly_called);
    TEST_ASSERT_EQUAL_size_t(0, reassembler.pendingCount());

    // Verify reassembled data
    TEST_ASSERT_EQUAL_size_t(9, g_reassembled_packet.size());
    TEST_ASSERT_EQUAL_MEMORY("AAABBBCCC", g_reassembled_packet.data(), 9);
}

void testReassemblerOutOfOrderFragments() {
    g_reassembly_called = false;
    RNS::BLE::BLEReassembler reassembler;
    reassembler.setReassemblyCallback(reassemblyCallback);

    RNS::Bytes identity(16);
    memset(identity.writable(16), 0xCC, 16);
    identity.resize(16);

    // Send fragments out of order: 0, 2, 1
    auto frag0 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::START, 0, 3, RNS::Bytes("111"));
    auto frag2 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::END, 2, 3, RNS::Bytes("333"));
    auto frag1 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::CONTINUE, 1, 3, RNS::Bytes("222"));

    TEST_ASSERT_TRUE(reassembler.processFragment(identity, frag0));
    TEST_ASSERT_FALSE(g_reassembly_called);

    TEST_ASSERT_TRUE(reassembler.processFragment(identity, frag2));
    TEST_ASSERT_FALSE(g_reassembly_called);

    TEST_ASSERT_TRUE(reassembler.processFragment(identity, frag1));
    TEST_ASSERT_TRUE(g_reassembly_called);
    TEST_ASSERT_EQUAL_MEMORY("111222333", g_reassembled_packet.data(), 9);
}

void testReassemblerTimeout() {
    // Test that incomplete reassembly times out
    RNS::BLE::BLEReassembler reassembler;
    reassembler.setTimeout(0.001);  // 1ms for testing

    RNS::Bytes identity(16);
    memset(identity.writable(16), 0xDD, 16);
    identity.resize(16);

    auto frag = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::START, 0, 3, RNS::Bytes("partial"));
    reassembler.processFragment(identity, frag);
    TEST_ASSERT_EQUAL_size_t(1, reassembler.pendingCount());

    // Simulate timeout
    RNS::Utilities::OS::sleep(0.005);  // Wait 5ms
    reassembler.checkTimeouts();
    TEST_ASSERT_EQUAL_size_t(0, reassembler.pendingCount());
}

void testReassemblerClearForPeer() {
    RNS::BLE::BLEReassembler reassembler;

    RNS::Bytes id1(16), id2(16);
    memset(id1.writable(16), 0x11, 16); id1.resize(16);
    memset(id2.writable(16), 0x22, 16); id2.resize(16);

    auto frag1 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::START, 0, 2, RNS::Bytes("A"));
    auto frag2 = RNS::BLE::BLEFragmenter::createFragment(
        RNS::BLE::Fragment::START, 0, 2, RNS::Bytes("B"));

    reassembler.processFragment(id1, frag1);
    reassembler.processFragment(id2, frag2);
    TEST_ASSERT_EQUAL_size_t(2, reassembler.pendingCount());

    reassembler.clearForPeer(id1);
    TEST_ASSERT_EQUAL_size_t(1, reassembler.pendingCount());
    TEST_ASSERT_FALSE(reassembler.hasPending(id1));
    TEST_ASSERT_TRUE(reassembler.hasPending(id2));
}

//=============================================================================
// Test 4: BLEIdentityManager - Handshake Protocol
//=============================================================================

static RNS::Bytes g_handshake_mac;
static RNS::Bytes g_handshake_identity;
static bool g_handshake_is_central = false;
static bool g_handshake_complete = false;

void handshakeCallback(const RNS::Bytes& mac, const RNS::Bytes& identity, bool is_central) {
    g_handshake_mac = mac;
    g_handshake_identity = identity;
    g_handshake_is_central = is_central;
    g_handshake_complete = true;
}

void testIdentityManagerLocalIdentity() {
    RNS::BLE::BLEIdentityManager mgr;
    TEST_ASSERT_FALSE(mgr.hasLocalIdentity());

    RNS::Bytes identity(16);
    memset(identity.writable(16), 0x42, 16);
    identity.resize(16);

    mgr.setLocalIdentity(identity);
    TEST_ASSERT_TRUE(mgr.hasLocalIdentity());
    TEST_ASSERT_EQUAL_size_t(16, mgr.getLocalIdentity().size());
    TEST_ASSERT_EQUAL_MEMORY(identity.data(), mgr.getLocalIdentity().data(), 16);
}

void testIdentityHandshakeDetection() {
    RNS::BLE::BLEIdentityManager mgr;
    RNS::Bytes mac(6);
    memset(mac.writable(6), 0xAA, 6);
    mac.resize(6);

    // 16 bytes + no existing identity = handshake
    RNS::Bytes sixteen_bytes(16);
    memset(sixteen_bytes.writable(16), 0x11, 16);
    sixteen_bytes.resize(16);
    TEST_ASSERT_TRUE(mgr.isHandshakeData(sixteen_bytes, mac));

    // Not 16 bytes = not handshake
    RNS::Bytes fifteen_bytes(15);
    memset(fifteen_bytes.writable(15), 0x22, 15);
    fifteen_bytes.resize(15);
    TEST_ASSERT_FALSE(mgr.isHandshakeData(fifteen_bytes, mac));

    RNS::Bytes seventeen_bytes(17);
    memset(seventeen_bytes.writable(17), 0x33, 17);
    seventeen_bytes.resize(17);
    TEST_ASSERT_FALSE(mgr.isHandshakeData(seventeen_bytes, mac));
}

void testIdentityHandshakeComplete() {
    g_handshake_complete = false;
    RNS::BLE::BLEIdentityManager mgr;
    mgr.setHandshakeCompleteCallback(handshakeCallback);

    RNS::Bytes local_id(16);
    memset(local_id.writable(16), 0x11, 16);
    local_id.resize(16);
    mgr.setLocalIdentity(local_id);

    RNS::Bytes mac(6);
    memset(mac.writable(6), 0xAA, 6);
    mac.resize(6);

    RNS::Bytes peer_id(16);
    memset(peer_id.writable(16), 0x22, 16);
    peer_id.resize(16);

    // Process handshake as peripheral (receiving from central)
    bool was_handshake = mgr.processReceivedData(mac, peer_id, false);
    TEST_ASSERT_TRUE(was_handshake);
    TEST_ASSERT_TRUE(g_handshake_complete);
    TEST_ASSERT_FALSE(g_handshake_is_central);
    TEST_ASSERT_EQUAL_MEMORY(peer_id.data(), g_handshake_identity.data(), 16);
}

void testIdentityMapping() {
    RNS::BLE::BLEIdentityManager mgr;

    RNS::Bytes mac(6), identity(16);
    memset(mac.writable(6), 0xAB, 6); mac.resize(6);
    memset(identity.writable(16), 0xCD, 16); identity.resize(16);

    // Complete handshake creates mapping
    mgr.completeHandshake(mac, identity, true);

    // Verify bidirectional lookup
    TEST_ASSERT_TRUE(mgr.hasIdentity(mac));
    TEST_ASSERT_EQUAL_size_t(16, mgr.getIdentityForMac(mac).size());
    TEST_ASSERT_EQUAL_MEMORY(identity.data(), mgr.getIdentityForMac(mac).data(), 16);
    TEST_ASSERT_EQUAL_size_t(6, mgr.getMacForIdentity(identity).size());
    TEST_ASSERT_EQUAL_MEMORY(mac.data(), mgr.getMacForIdentity(identity).data(), 6);

    TEST_ASSERT_EQUAL_size_t(1, mgr.knownPeerCount());
}

void testIdentityMacRotation() {
    RNS::BLE::BLEIdentityManager mgr;

    RNS::Bytes old_mac(6), new_mac(6), identity(16);
    memset(old_mac.writable(6), 0x11, 6); old_mac.resize(6);
    memset(new_mac.writable(6), 0x22, 6); new_mac.resize(6);
    memset(identity.writable(16), 0xFF, 16); identity.resize(16);

    // Create initial mapping
    mgr.completeHandshake(old_mac, identity, true);
    TEST_ASSERT_TRUE(mgr.hasIdentity(old_mac));

    // Simulate MAC rotation
    mgr.updateMacForIdentity(identity, new_mac);

    // Old MAC no longer valid, new MAC works
    TEST_ASSERT_FALSE(mgr.hasIdentity(old_mac));
    TEST_ASSERT_TRUE(mgr.hasIdentity(new_mac));
    TEST_ASSERT_EQUAL_MEMORY(new_mac.data(), mgr.getMacForIdentity(identity).data(), 6);
}

void testIdentityRemoveMapping() {
    RNS::BLE::BLEIdentityManager mgr;

    RNS::Bytes mac(6), identity(16);
    memset(mac.writable(6), 0xAA, 6); mac.resize(6);
    memset(identity.writable(16), 0xBB, 16); identity.resize(16);

    mgr.completeHandshake(mac, identity, true);
    TEST_ASSERT_EQUAL_size_t(1, mgr.knownPeerCount());

    mgr.removeMapping(mac);
    TEST_ASSERT_EQUAL_size_t(0, mgr.knownPeerCount());
    TEST_ASSERT_FALSE(mgr.hasIdentity(mac));
}

//=============================================================================
// Test 5: BLEPeerManager - Peer Tracking & Scoring
//=============================================================================

void testPeerManagerLocalMac() {
    RNS::BLE::BLEPeerManager mgr;
    RNS::Bytes mac(6);
    memset(mac.writable(6), 0x12, 6);
    mac.resize(6);

    mgr.setLocalMac(mac);
    TEST_ASSERT_EQUAL_MEMORY(mac.data(), mgr.getLocalMac().data(), 6);
}

void testPeerManagerAddDiscoveredPeer() {
    RNS::BLE::BLEPeerManager mgr;
    RNS::Bytes mac(6);
    memset(mac.writable(6), 0xAA, 6);
    mac.resize(6);

    TEST_ASSERT_TRUE(mgr.addDiscoveredPeer(mac, -50));
    TEST_ASSERT_EQUAL_size_t(1, mgr.totalPeerCount());

    auto* peer = mgr.getPeerByMac(mac);
    TEST_ASSERT_NOT_NULL(peer);
    TEST_ASSERT_EQUAL_INT8(-50, peer->rssi);
    TEST_ASSERT_EQUAL(RNS::BLE::PeerState::DISCOVERED, peer->state);
}

void testPeerManagerSetIdentity() {
    RNS::BLE::BLEPeerManager mgr;
    RNS::Bytes mac(6), identity(16);
    memset(mac.writable(6), 0xBB, 6); mac.resize(6);
    memset(identity.writable(16), 0xCC, 16); identity.resize(16);

    mgr.addDiscoveredPeer(mac, -60);
    TEST_ASSERT_TRUE(mgr.setPeerIdentity(mac, identity));

    // Now peer should be findable by identity
    auto* peer = mgr.getPeerByIdentity(identity);
    TEST_ASSERT_NOT_NULL(peer);
    TEST_ASSERT_TRUE(peer->hasIdentity());
}

void testMacSortingConnectionDirection() {
    // Lower MAC should initiate (become central)
    RNS::Bytes lower_mac(6), higher_mac(6);
    memset(lower_mac.writable(6), 0x01, 6); lower_mac.resize(6);
    memset(higher_mac.writable(6), 0xFF, 6); higher_mac.resize(6);

    // If we have lower MAC, we should initiate
    TEST_ASSERT_TRUE(RNS::BLE::BLEPeerManager::shouldInitiateConnection(lower_mac, higher_mac));
    TEST_ASSERT_FALSE(RNS::BLE::BLEPeerManager::shouldInitiateConnection(higher_mac, lower_mac));

    // Equal MAC - neither initiates
    TEST_ASSERT_FALSE(RNS::BLE::BLEPeerManager::shouldInitiateConnection(lower_mac, lower_mac));
}

void testPeerScoring() {
    RNS::BLE::BLEPeerManager mgr;

    // Add two peers with different RSSI
    RNS::Bytes mac1(6), mac2(6);
    memset(mac1.writable(6), 0x11, 6); mac1.resize(6);
    memset(mac2.writable(6), 0x22, 6); mac2.resize(6);

    mgr.addDiscoveredPeer(mac1, -40);  // Strong signal
    mgr.addDiscoveredPeer(mac2, -90);  // Weak signal

    mgr.recalculateScores();

    auto* peer1 = mgr.getPeerByMac(mac1);
    auto* peer2 = mgr.getPeerByMac(mac2);

    // Stronger signal should have higher score
    TEST_ASSERT_TRUE(peer1->score > peer2->score);
}

void testPeerBlacklist() {
    RNS::BLE::BLEPeerManager mgr;
    RNS::Bytes mac(6);
    memset(mac.writable(6), 0xDD, 6);
    mac.resize(6);

    mgr.addDiscoveredPeer(mac, -50);

    // Fail 3 times to trigger blacklist (threshold = 3)
    mgr.connectionFailed(mac);
    mgr.connectionFailed(mac);
    mgr.connectionFailed(mac);

    auto* peer = mgr.getPeerByMac(mac);
    TEST_ASSERT_EQUAL(RNS::BLE::PeerState::BLACKLISTED, peer->state);
    TEST_ASSERT_TRUE(peer->blacklisted_until > 0);
}

void testPeerBlacklistBackoff() {
    // Verify exponential backoff: 60s × min(2^(failures-3), 8)
    RNS::BLE::BLEPeerManager mgr;
    RNS::Bytes mac(6);
    memset(mac.writable(6), 0xEE, 6);
    mac.resize(6);

    mgr.addDiscoveredPeer(mac, -50);

    // 3 failures = 60s × 2^0 = 60s
    for (int i = 0; i < 3; i++) mgr.connectionFailed(mac);
    auto* peer = mgr.getPeerByMac(mac);
    double now = RNS::Utilities::OS::time();
    double backoff1 = peer->blacklisted_until - now;
    TEST_ASSERT_FLOAT_WITHIN(5.0, 60.0, backoff1);

    // Reset and try 4 failures = 60s × 2^1 = 120s
    peer->state = RNS::BLE::PeerState::DISCOVERED;
    peer->consecutive_failures = 0;
    for (int i = 0; i < 4; i++) mgr.connectionFailed(mac);
    double backoff2 = peer->blacklisted_until - RNS::Utilities::OS::time();
    TEST_ASSERT_FLOAT_WITHIN(5.0, 120.0, backoff2);
}

void testBestConnectionCandidate() {
    RNS::BLE::BLEPeerManager mgr;

    // Must set local MAC for connection direction check
    RNS::Bytes local_mac(6);
    memset(local_mac.writable(6), 0x01, 6); local_mac.resize(6);  // Lower than peers
    mgr.setLocalMac(local_mac);

    RNS::Bytes mac1(6), mac2(6);
    memset(mac1.writable(6), 0x11, 6); mac1.resize(6);
    memset(mac2.writable(6), 0x22, 6); mac2.resize(6);

    mgr.addDiscoveredPeer(mac1, -80);  // Weak
    mgr.addDiscoveredPeer(mac2, -40);  // Strong
    mgr.recalculateScores();

    auto* best = mgr.getBestConnectionCandidate();
    TEST_ASSERT_NOT_NULL(best);
    TEST_ASSERT_EQUAL_MEMORY(mac2.data(), best->mac_address.data(), 6);
}

void testPeerConnectionTracking() {
    RNS::BLE::BLEPeerManager mgr;
    RNS::Bytes mac(6);
    memset(mac.writable(6), 0xFF, 6);
    mac.resize(6);

    mgr.addDiscoveredPeer(mac, -50);
    TEST_ASSERT_EQUAL_size_t(0, mgr.connectedCount());

    mgr.setPeerState(mac, RNS::BLE::PeerState::CONNECTED);
    TEST_ASSERT_EQUAL_size_t(1, mgr.connectedCount());

    mgr.connectionSucceeded(mac);
    auto* peer = mgr.getPeerByMac(mac);
    TEST_ASSERT_EQUAL_UINT32(1, peer->connection_successes);
}

//=============================================================================
// Test 6: End-to-End Fragment/Reassemble Roundtrip
//=============================================================================

void testFragmentReassembleRoundtrip() {
    g_reassembly_called = false;

    // Create fragmenter and reassembler
    RNS::BLE::BLEFragmenter frag(50);  // 45 byte payload
    RNS::BLE::BLEReassembler reassembler;
    reassembler.setReassemblyCallback(reassemblyCallback);

    RNS::Bytes identity(16);
    memset(identity.writable(16), 0xAB, 16);
    identity.resize(16);

    // Create 100-byte test packet
    RNS::Bytes original(100);
    uint8_t* buf = original.writable(100);
    for (int i = 0; i < 100; i++) buf[i] = static_cast<uint8_t>(i);
    original.resize(100);

    // Fragment
    auto frags = frag.fragment(original);
    TEST_ASSERT_EQUAL_size_t(3, frags.size());  // ceil(100/45) = 3

    // Reassemble
    for (auto& f : frags) {
        reassembler.processFragment(identity, f);
    }

    TEST_ASSERT_TRUE(g_reassembly_called);
    TEST_ASSERT_EQUAL_size_t(100, g_reassembled_packet.size());
    TEST_ASSERT_EQUAL_MEMORY(original.data(), g_reassembled_packet.data(), 100);
}

void testMultiplePeersFragmentation() {
    RNS::BLE::BLEReassembler reassembler;

    int callback_count = 0;
    std::map<RNS::Bytes, RNS::Bytes> received;

    reassembler.setReassemblyCallback([&](const RNS::Bytes& id, const RNS::Bytes& pkt) {
        received[id] = pkt;
        callback_count++;
    });

    RNS::Bytes id1(16), id2(16);
    memset(id1.writable(16), 0x11, 16); id1.resize(16);
    memset(id2.writable(16), 0x22, 16); id2.resize(16);

    RNS::BLE::BLEFragmenter frag(28);  // 23 byte payload

    // Fragment data for two peers
    auto frags1 = frag.fragment(RNS::Bytes("Hello from peer 1!"));
    auto frags2 = frag.fragment(RNS::Bytes("Hello from peer 2!"));

    // Interleaved delivery (both fit in single fragment)
    reassembler.processFragment(id1, frags1[0]);
    reassembler.processFragment(id2, frags2[0]);

    TEST_ASSERT_EQUAL_INT(2, callback_count);
    TEST_ASSERT_EQUAL_size_t(18, received[id1].size());
    TEST_ASSERT_EQUAL_MEMORY("Hello from peer 1!", received[id1].data(), 18);
    TEST_ASSERT_EQUAL_size_t(18, received[id2].size());
    TEST_ASSERT_EQUAL_MEMORY("Hello from peer 2!", received[id2].data(), 18);
}

//=============================================================================
// Test Runner
//=============================================================================

void setUp(void) {
    // Reset global test state before each test
    g_reassembled_packet = RNS::Bytes();
    g_reassembled_identity = RNS::Bytes();
    g_reassembly_called = false;
    g_handshake_mac = RNS::Bytes();
    g_handshake_identity = RNS::Bytes();
    g_handshake_is_central = false;
    g_handshake_complete = false;
}

void tearDown(void) {
    // Clean up after each test
}

int runUnityTests(void) {
    UNITY_BEGIN();

    // Suite-level setup
    RNS::Utilities::OS::dump_heap_stats();
    size_t pre_memory = RNS::Utilities::OS::heap_available();
    TRACEF("testBLE: pre-mem: %u", pre_memory);

    // BLETypes tests
    RUN_TEST(testBLEProtocolVersion);
    RUN_TEST(testServiceUUIDs);
    RUN_TEST(testProtocolConstants);
    RUN_TEST(testBLEAddress);

    // BLEFragmenter tests
    RUN_TEST(testFragmenterMTU);
    RUN_TEST(testFragmenterSingleFragment);
    RUN_TEST(testFragmenterMultipleFragments);
    RUN_TEST(testFragmentHeaderFormat);
    RUN_TEST(testFragmentValidation);

    // BLEReassembler tests
    RUN_TEST(testReassemblerSingleFragment);
    RUN_TEST(testReassemblerMultipleFragments);
    RUN_TEST(testReassemblerOutOfOrderFragments);
    RUN_TEST(testReassemblerTimeout);
    RUN_TEST(testReassemblerClearForPeer);

    // BLEIdentityManager tests
    RUN_TEST(testIdentityManagerLocalIdentity);
    RUN_TEST(testIdentityHandshakeDetection);
    RUN_TEST(testIdentityHandshakeComplete);
    RUN_TEST(testIdentityMapping);
    RUN_TEST(testIdentityMacRotation);
    RUN_TEST(testIdentityRemoveMapping);

    // BLEPeerManager tests
    RUN_TEST(testPeerManagerLocalMac);
    RUN_TEST(testPeerManagerAddDiscoveredPeer);
    RUN_TEST(testPeerManagerSetIdentity);
    RUN_TEST(testMacSortingConnectionDirection);
    RUN_TEST(testPeerScoring);
    RUN_TEST(testPeerBlacklist);
    RUN_TEST(testPeerBlacklistBackoff);
    RUN_TEST(testBestConnectionCandidate);
    RUN_TEST(testPeerConnectionTracking);

    // End-to-end tests
    RUN_TEST(testFragmentReassembleRoundtrip);
    RUN_TEST(testMultiplePeersFragmentation);

    // Suite-level teardown
    size_t post_memory = RNS::Utilities::OS::heap_available();
    int diff_memory = static_cast<int>(pre_memory) - static_cast<int>(post_memory);
    TRACEF("testBLE: post-mem: %u", post_memory);
    TRACEF("testBLE: diff-mem: %d", diff_memory);

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
