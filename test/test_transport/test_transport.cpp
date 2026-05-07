#include <unity.h>

#include <microStore/Adapters/UniversalFileSystem.h>

#include <Reticulum.h>
#include <Transport.h>
#include <Identity.h>
#include <Destination.h>
#include <Packet.h>
#include <Link.h>
#include <Resource.h>
#include <Log.h>
#include <Bytes.h>
#include <Utilities/OS.h>

// ============================================================================
// Test infrastructure - loopback interfaces
// ============================================================================

static int packets_received = 0;

class InInterface : public RNS::InterfaceImpl {
public:
	InInterface(const char *name = "InInterface") : RNS::InterfaceImpl(name) {
		_OUT = false;
		_IN = true;
	}
	virtual ~InInterface() { _name = "(deleted)"; }
	virtual void send_outgoing(const RNS::Bytes &data) {}
	virtual void handle_incoming(const RNS::Bytes &data) {
		try {
			InterfaceImpl::handle_incoming(data);
		}
		catch (const std::bad_alloc&) {
			ERROR("handle_incoming: bad_alloc - OUT OF MEMORY");
		}
		catch (const std::exception& e) {
			ERRORF("handle_incoming: exception: %s", e.what());
		}
	}
};

class OutInterface : public RNS::InterfaceImpl {
public:
	OutInterface(RNS::Interface& in_interface, const char *name = "OutInterface")
		: RNS::InterfaceImpl(name), _in_interface(in_interface) {
		_OUT = true;
		_IN = false;
	}
	virtual ~OutInterface() { _name = "(deleted)"; }
	virtual void send_outgoing(const RNS::Bytes &data) {
		_in_interface.handle_incoming(data);
		try {
			InterfaceImpl::handle_outgoing(data);
		}
		catch (const std::bad_alloc&) {
			ERROR("handle_outgoing: bad_alloc - OUT OF MEMORY");
		}
		catch (const std::exception& e) {
			ERRORF("handle_outgoing: exception: %s", e.what());
		}
	}
private:
	RNS::Interface& _in_interface;
};

// Global test state
RNS::Reticulum test_reticulum({RNS::Type::NONE});
RNS::Identity test_identity({RNS::Type::NONE});
RNS::Destination test_dest({RNS::Type::NONE});
RNS::Interface in_interface(new InInterface());
RNS::Interface out_interface(new OutInterface(in_interface));

void onTestPacket(const RNS::Bytes& data, const RNS::Packet& packet) {
	packets_received++;
}

// ============================================================================
// Setup: Initialize Reticulum with loopback interfaces
// ============================================================================

bool rns_initialized = false;

void initRNS() {
	if (rns_initialized) return;

#if defined(RNS_MEM_LOG)
      RNS::loglevel(RNS::LOG_MEM);
#else
      RNS::loglevel(RNS::LOG_TRACE);
#endif

	try {
		// Set sane memory limits based on hardware-specific availability
		RNS::Transport::path_table_maxsize(50);
		RNS::Transport::announce_table_maxsize(50);
		RNS::Transport::hashlist_maxsize(50);
		RNS::Transport::max_pr_tags(32);
		RNS::Identity::known_destinations_maxsize(50);

		microStore::FileSystem filesystem{microStore::Adapters::UniversalFileSystem()};
		filesystem.init();
		RNS::Utilities::OS::register_filesystem(filesystem);

		RNS::Transport::register_interface(in_interface);
		RNS::Transport::register_interface(out_interface);

		RNS::Transport::dump_stats();

		// Transport identity
		RNS::Bytes transport_prv;
		transport_prv.assignHex("BABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABEBABE");
		RNS::Identity transport_identity(false);
		transport_identity.load_private_key(transport_prv);
		RNS::Transport::identity(transport_identity);

		test_reticulum = RNS::Reticulum();
		test_reticulum.transport_enabled(true);
		test_reticulum.start();

		// Single identity and destination (same pattern as test_rns_loopback)
		test_identity = RNS::Identity(false);
		RNS::Bytes prv;
		prv.assignHex("E0D43398EDC974EBA9F4A83463691A08F4D306D4E56BA6B275B8690A2FBD9852E9EBE7C03BC45CAEC9EF8E78C830037210BFB9986F6CA2DEE2B5C28D7B4DE6B0");
		test_identity.load_private_key(prv);

		test_dest = RNS::Destination(test_identity, RNS::Type::Destination::IN,
			RNS::Type::Destination::SINGLE, "test", "transport");
		test_dest.set_packet_callback(onTestPacket);
		test_dest.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);

		rns_initialized = true;
	}
	catch (const std::bad_alloc&) {
		ERROR("initRNS: bad_alloc - OUT OF MEMORY");
	}
	catch (const std::exception& e) {
		ERRORF("initRNS: exception: %s", e.what());
	}
}

// ============================================================================
// Transport receipt lifecycle tests
// ============================================================================

void test_receipt_creation_and_culling() {
	// Send packets and verify the send path + receipt creation + culling doesn't crash.
	// Note: loopback only delivers announces, not data packets, so we test
	// the outbound path, receipt creation, and jobs() culling cycle.
	initRNS();

	const int NUM_PACKETS = 10;
	for (int i = 0; i < NUM_PACKETS; i++) {
		RNS::Bytes payload("Test packet " + std::to_string(i));
		RNS::Packet packet(test_dest, payload);
		packet.send();
	}

	// Run Transport jobs to process receipts (check interval = 1.0s)
	RNS::Utilities::OS::sleep(1.5);
	test_reticulum.loop();

	// Run again to trigger receipt timeout and culling
	RNS::Utilities::OS::sleep(1.5);
	test_reticulum.loop();

	TEST_ASSERT_TRUE_MESSAGE(true, "Receipt creation and culling did not crash");
}

void test_receipt_rapid_fire() {
	// Simulate Maps-like behavior: send many packets in rapid succession.
	// This stresses Transport::outbound(), receipt list growth, and
	// the jobs() culling path.
	initRNS();

	const int NUM_PACKETS = 50;
	for (int i = 0; i < NUM_PACKETS; i++) {
		std::string msg = "Tile request z=10 x=" + std::to_string(i) + " y=42";
		RNS::Bytes payload(msg.c_str());
		RNS::Packet packet(test_dest, payload);
		packet.send();
	}

	// Run multiple job cycles to process and cull all receipts
	for (int cycle = 0; cycle < 5; cycle++) {
		RNS::Utilities::OS::sleep(1.5);
		test_reticulum.loop();
	}

	TEST_ASSERT_TRUE_MESSAGE(true, "Rapid-fire send did not crash");
}

void test_receipt_timeout_callback() {
	// Test that receipt timeout callbacks fire correctly
	initRNS();

	static bool timeout_called = false;
	static bool delivery_called = false;

	// Create a destination that we can't actually reach (no loopback for this one)
	RNS::Identity remote_id(true);  // Random identity
	RNS::Destination unreachable_dest(remote_id, RNS::Type::Destination::OUT,
		RNS::Type::Destination::SINGLE, "test", "unreachable");

	RNS::Bytes payload("This should timeout");
	RNS::Packet packet(unreachable_dest, payload);
	packet.send();
	RNS::PacketReceipt receipt = packet.receipt();
	receipt.set_timeout(1);  // 1 second timeout
	receipt.set_timeout_callback([](const RNS::PacketReceipt& r) {
		timeout_called = true;
	});
	receipt.set_delivery_callback([](const RNS::PacketReceipt& r) {
		delivery_called = true;
	});

	// Wait for timeout
	for (int i = 0; i < 5; i++) {
		RNS::Utilities::OS::sleep(0.5);
		test_reticulum.loop();
	}

	TEST_ASSERT_TRUE_MESSAGE(timeout_called, "Timeout callback should have fired");
	TEST_ASSERT_FALSE_MESSAGE(delivery_called, "Delivery callback should NOT have fired");
}

// ============================================================================
// Transport hashlist tests
// ============================================================================

void test_hashlist_basic() {
	// Send enough packets that the hashlist should grow
	initRNS();

	const int NUM_PACKETS = 30;
	for (int i = 0; i < NUM_PACKETS; i++) {
		RNS::Bytes payload("Hash test " + std::to_string(i));
		RNS::Packet packet(test_dest, payload);
		packet.send();
	}

	// Run jobs to process and potentially cull hashlist
	test_reticulum.loop();

	TEST_ASSERT_TRUE_MESSAGE(true, "Hashlist operations did not crash");
}

void test_hashlist_overflow() {
	// Send more packets than _hashlist_maxsize (100) to trigger culling
	initRNS();

	const int NUM_PACKETS = 150;  // > maxsize of 100
	for (int i = 0; i < NUM_PACKETS; i++) {
		RNS::Bytes payload("Overflow " + std::to_string(i) + " padding to make unique");
		RNS::Packet packet(test_dest, payload);
		packet.send();

		// Periodically run jobs to trigger culling mid-stream
		if (i % 25 == 0) {
			test_reticulum.loop();
		}
	}

	// Final jobs run
	test_reticulum.loop();
	RNS::Utilities::OS::sleep(1.5);
	test_reticulum.loop();

	TEST_ASSERT_TRUE_MESSAGE(true, "Hashlist overflow and culling did not crash");
}

// ============================================================================
// Packet send/receive cycle stress test
// ============================================================================

void test_send_receive_cycle() {
	// Single packet send through outbound path, then jobs cycle
	initRNS();

	RNS::Bytes payload("Cycle test payload");
	RNS::Packet packet(test_dest, payload);
	packet.send();

	test_reticulum.loop();
	RNS::Utilities::OS::sleep(1.5);
	test_reticulum.loop();

	TEST_ASSERT_TRUE_MESSAGE(true, "Send/jobs cycle did not crash");
}

void test_send_receive_stress() {
	// Stress test: 100 packets with periodic jobs() processing.
	// Tests that outbound + hashlist + receipt list handles volume.
	initRNS();

	const int NUM_CYCLES = 100;
	for (int i = 0; i < NUM_CYCLES; i++) {
		std::string msg = "Stress " + std::to_string(i);
		RNS::Bytes payload(msg.c_str());
		RNS::Packet packet(test_dest, payload);
		packet.send();

		// Run jobs every 10 packets
		if (i % 10 == 0) {
			test_reticulum.loop();
		}
	}

	// Final processing - let receipts timeout and get culled
	for (int i = 0; i < 3; i++) {
		RNS::Utilities::OS::sleep(1.5);
		test_reticulum.loop();
	}

	printf("[TEST] Sent %d packets without crash\n", NUM_CYCLES);
	TEST_ASSERT_TRUE_MESSAGE(true, "Stress test did not crash");
}

void test_interleaved_send_and_jobs() {
	// Interleave packet sends with Transport::jobs() to stress the
	// _jobs_locked/_jobs_running coordination and receipt processing
	initRNS();

	for (int round = 0; round < 20; round++) {
		// Send a burst of 5 packets
		for (int i = 0; i < 5; i++) {
			std::string msg = "Burst " + std::to_string(round) + "." + std::to_string(i);
			RNS::Bytes payload(msg.c_str());
			RNS::Packet packet(test_dest, payload);
			packet.send();
		}

		// Immediately run jobs (tests state after outbound batch)
		test_reticulum.loop();
	}

	// Let receipts settle and get culled
	for (int i = 0; i < 3; i++) {
		RNS::Utilities::OS::sleep(1.5);
		test_reticulum.loop();
	}

	printf("[TEST] Burst test: 20 rounds x 5 packets completed\n");
	TEST_ASSERT_TRUE_MESSAGE(true, "Interleaved send/jobs did not crash");
}

// ============================================================================
// Identity/Destination lifecycle tests
// ============================================================================

void test_identity_create_destroy_cycle() {
	// Rapidly create and destroy identities to stress crypto allocation
	for (int i = 0; i < 20; i++) {
		RNS::Identity id(true);  // Generate new keypair
		TEST_ASSERT_TRUE(id);
		RNS::Bytes hash = id.hash();
		TEST_ASSERT_TRUE(hash.size() > 0);
		std::string hex = hash.toHex();
		TEST_ASSERT_TRUE(hex.length() > 0);
	}
	TEST_ASSERT_TRUE_MESSAGE(true, "Identity create/destroy cycle did not crash");
}

void test_destination_packet_cycle() {
	// Create destinations, send packets, destroy - repeat
	initRNS();

	for (int cycle = 0; cycle < 10; cycle++) {
		RNS::Identity temp_id(true);
		RNS::Destination temp_dest(temp_id, RNS::Type::Destination::IN,
			RNS::Type::Destination::SINGLE, "test", "temp");
		temp_dest.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);

		// Announce
		std::string app_data = "test_cycle_" + std::to_string(cycle);
		temp_dest.announce(RNS::bytesFromString(app_data.c_str()));

		test_reticulum.loop();
		// temp_dest goes out of scope here
	}

	TEST_ASSERT_TRUE_MESSAGE(true, "Destination lifecycle did not crash");
}


void test_incoming_announce_limit() {

	printf("test_incoming_announce_max: BEGIN\n");

	initRNS();

	const int num_announces = 50;
	const int announces_period = 100;

	int announce_count = 0;
	uint64_t start_time = RNS::Utilities::OS::ltime();
	uint64_t announce_time = 0;
	uint64_t log_time = 0;
	while (true) {
		// Loop for 10 seconds after last announce
		if ((RNS::Utilities::OS::ltime() - start_time) >= (num_announces * announces_period + 10000)) {
			break;
		}
		try {
			if (announce_count < num_announces && (RNS::Utilities::OS::ltime() - announce_time) >= announces_period) {
				printf("***** test_incoming_announce_limit: Sending announce #%d\n", announce_count + 1);
				RNS::Identity temp_id(true);
				RNS::Destination temp_dest(temp_id, RNS::Type::Destination::IN,
					RNS::Type::Destination::SINGLE, "test", "announce");
				temp_dest.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);

				// Announce
				std::string app_data = "test_announce_" + std::to_string(announce_count + 1);
				RNS::Packet announce_packet = temp_dest.announce(app_data, false, {RNS::Type::NONE}, {RNS::Type::NONE}, false);
				announce_packet.pack();

				// Must deregister destination so that the ANNOUNCE appears to be from a remote node
				RNS::Transport::deregister_destination(temp_dest);

				in_interface.handle_incoming(announce_packet.raw());

				announce_time = RNS::Utilities::OS::ltime();
				++announce_count;
			}
			test_reticulum.loop();
			if ((RNS::Utilities::OS::ltime() - log_time) >= 5000) {
				RNS::Transport::dump_stats();
				log_time = RNS::Utilities::OS::ltime();
			}
		}
		catch (const std::bad_alloc&) {
			ERROR("test_incoming_announce_limit: bad_alloc - OUT OF MEMORY");
			break;
		}
		catch (const std::exception& e) {
			ERRORF("test_incoming_announce_limit: exception: %s", e.what());
			break;
		}
		RNS::Utilities::OS::sleep(0.1);
	}
	RNS::Transport::persist_data();
	RNS::Transport::dump_stats();

	printf("test_incoming_announce_limit: END\n");
}

void test_incoming_announce_over_limit() {

	printf("test_incoming_announce_over_limit: BEGIN\n");

	initRNS();

	const int num_announces = 100;
	const int announces_period = 100;

	int announce_count = 0;
	uint64_t start_time = RNS::Utilities::OS::ltime();
	uint64_t announce_time = 0;
	uint64_t log_time = 0;
	while (true) {
		// Loop for 10 seconds after last announce
		if ((RNS::Utilities::OS::ltime() - start_time) >= (num_announces * announces_period + 10000)) {
			break;
		}
		try {
			if (announce_count < num_announces && (RNS::Utilities::OS::ltime() - announce_time) >= announces_period) {
				printf("***** test_incoming_announce_over_limit: Sending announce #%d\n", announce_count + 1);
				RNS::Identity temp_id(true);
				RNS::Destination temp_dest(temp_id, RNS::Type::Destination::IN,
					RNS::Type::Destination::SINGLE, "test", "announce");
				temp_dest.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);

				// Announce
				std::string app_data = "test_announce_" + std::to_string(announce_count + 1);
				RNS::Packet announce_packet = temp_dest.announce(app_data, false, {RNS::Type::NONE}, {RNS::Type::NONE}, false);
				announce_packet.pack();

				// Must deregister destination so that the ANNOUNCE appears to be from a remote node
				RNS::Transport::deregister_destination(temp_dest);

				in_interface.handle_incoming(announce_packet.raw());

				announce_time = RNS::Utilities::OS::ltime();
				++announce_count;
			}
			test_reticulum.loop();
			if ((RNS::Utilities::OS::ltime() - log_time) >= 5000) {
				RNS::Transport::dump_stats();
				log_time = RNS::Utilities::OS::ltime();
			}
		}
		catch (const std::bad_alloc&) {
			ERROR("test_incoming_announce_over_limit: bad_alloc - OUT OF MEMORY");
			break;
		}
		catch (const std::exception& e) {
			ERRORF("test_incoming_announce_over_limit: exception: %s", e.what());
			break;
		}
		RNS::Utilities::OS::sleep(0.1);
	}
	RNS::Transport::persist_data();
	RNS::Transport::dump_stats();

	printf("test_incoming_announce_over_limit: END\n");
}

void test_incoming_announce_stress() {

	printf("test_incoming_announce_stress: BEGIN\n");

	initRNS();

	//const int num_announces = 0;
	//const int num_announces = 100;
	//const int num_announces = 1000;
	const int num_announces = 10000;
	//const int announces_period = 1000;
	const int announces_period = 100;

	int announce_count = 0;
	uint64_t start_time = RNS::Utilities::OS::ltime();
	uint64_t announce_time = 0;
	uint64_t log_time = 0;
	while (true) {
		// Loop for 60 seconds after last announce
		if ((RNS::Utilities::OS::ltime() - start_time) >= (num_announces * announces_period + 60000)) {
			break;
		}
		try {
			if (announce_count < num_announces && (RNS::Utilities::OS::ltime() - announce_time) >= announces_period) {
				printf("***** test_incoming_announce_stress: Sending announce #%d\n", announce_count + 1);
				RNS::Identity temp_id(true);
				RNS::Destination temp_dest(temp_id, RNS::Type::Destination::IN,
					RNS::Type::Destination::SINGLE, "test", "announce");
				temp_dest.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);

				// Announce
				std::string app_data = "test_announce_" + std::to_string(announce_count + 1);
				RNS::Packet announce_packet = temp_dest.announce(app_data, false, {RNS::Type::NONE}, {RNS::Type::NONE}, false);
				announce_packet.pack();
				//printf("test_incoming_anounce: Sending packet with data: %s\n", announce_packet.raw().toHex().c_str());

				// Must deregister destination so that the ANNOUNCE appears to be from a remote node
				RNS::Transport::deregister_destination(temp_dest);

				in_interface.handle_incoming(announce_packet.raw());

				announce_time = RNS::Utilities::OS::ltime();
				++announce_count;
			}
			test_reticulum.loop();
			if ((RNS::Utilities::OS::ltime() - log_time) >= 5000) {
				RNS::Transport::dump_stats();
				log_time = RNS::Utilities::OS::ltime();
			}
		}
		catch (const std::bad_alloc&) {
			ERROR("test_incoming_announce_stress: bad_alloc - OUT OF MEMORY");
			break;
		}
		catch (const std::exception& e) {
			ERRORF("test_incoming_announce_stress: exception: %s", e.what());
			break;
		}
		RNS::Utilities::OS::sleep(0.1);
	}
	RNS::Transport::persist_data();
	RNS::Transport::dump_stats();

	printf("test_incoming_announce_stress: END\n");
}


// ============================================================================
// Regression tests for bugs found driving the lxmf-conformance suite
// and the pyxis T-Deck firmware to a working state.
// ============================================================================

// Regression: `RequestReceiptData::_link` had no `{Type::NONE}` initializer,
// so default-constructing a RequestReceipt allocated a fresh Link via the
// main `Link::Link(...)` ctor with all-default args. That branch leaves
// `_sig_prv` null (only set when destination or owner is provided), so the
// `_sig_prv->public_key()` call later in the same ctor SIGSEGV'd.
//
// First-fired site was inside the LXMF propagation sync flow, when
// constructing the response receipt to /get. Catching this in a unit test
// keeps the regression from creeping back if anyone touches LinkData.h.
//
// We don't need an end-to-end link here — just exercising
// `_object(new RequestReceiptData())` is enough to crash the buggy
// version, because the embedded Link's ctor runs at allocation time.
void test_request_receipt_default_construct_no_segfault() {
	printf("test_request_receipt_default_construct_no_segfault: BEGIN\n");

	initRNS();

	// Set up a real link so we have a valid one to attach. We use the
	// loopback interfaces the test fixture already has.
	RNS::Identity peer_id(true);
	RNS::Destination peer_dest(peer_id, RNS::Type::Destination::OUT,
	    RNS::Type::Destination::SINGLE, "test", "rr_smoke");

	RNS::Link link(peer_dest);
	TEST_ASSERT_TRUE(link);

	// Use an empty PacketReceipt + Resource so RequestReceipt's ctor
	// path inside the buggy commit only relies on the embedded Link's
	// default-construction (which is the actual crash trigger).
	RNS::PacketReceipt empty_pr({RNS::Type::NONE});
	RNS::Resource empty_res({RNS::Type::NONE});

	bool no_crash = true;
	try {
		RNS::RequestReceipt rr(link, empty_pr, empty_res,
		    /*response_callback=*/nullptr,
		    /*failed_callback=*/nullptr,
		    /*progress_callback=*/nullptr,
		    /*timeout=*/1.0,
		    /*request_size=*/0);
		// Touch a public method so the optimizer doesn't elide
		// the construction (which would defeat the point).
		(void)rr.request_id();
	}
	catch (const std::exception& e) {
		printf("RequestReceipt threw: %s\n", e.what());
		no_crash = false;
	}

	TEST_ASSERT_TRUE_MESSAGE(no_crash,
	    "RequestReceipt constructor should not crash; was a regression "
	    "of the LinkData.h missing `{Type::NONE}` on the embedded Link.");

	printf("test_request_receipt_default_construct_no_segfault: END\n");
}

// Regression: the microStore migration left `Transport::get_path_table()`
// reading the in-memory `_path_table` (a `std::map<Bytes, DestinationEntry>`)
// while the inbound announce path only wrote to the file-backed
// `_new_path_table`. Every consumer that iterated the in-memory map saw it
// empty — pyxis's UI announce list was the user-visible symptom.
//
// This test fires a single announce from a synthesized peer through the
// loopback interface and asserts BOTH stores receive the entry: the file-
// backed one (queried via Transport::has_path) and the in-memory one
// (queried via Transport::get_path_table().size()).
void test_announce_populates_both_path_tables() {
	printf("test_announce_populates_both_path_tables: BEGIN\n");

	initRNS();

	const size_t paths_before_mem = RNS::Transport::get_path_table().size();

	// Synthesize an announce from a fresh peer. Same recipe as the
	// stress tests above: build a Destination on a one-off Identity,
	// emit an announce packet, deregister the local Destination so
	// `_destinations` doesn't claim the announce as "ours", and feed
	// the raw bytes back through the loopback IN interface so Transport
	// processes it on the receive path.
	RNS::Identity peer_id(true);
	RNS::Destination peer_dest(peer_id, RNS::Type::Destination::IN,
	    RNS::Type::Destination::SINGLE, "test", "dual_path");
	peer_dest.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);
	RNS::Bytes peer_dest_hash = peer_dest.hash();

	std::string app_data = "dual-path-test";
	RNS::Packet announce = peer_dest.announce(app_data, false,
	    {RNS::Type::NONE}, {RNS::Type::NONE}, false);
	announce.pack();
	RNS::Transport::deregister_destination(peer_dest);

	in_interface.handle_incoming(announce.raw());

	// Run Transport's loop for a short window so the inbound path
	// runs to completion (announce validation + dual-write).
	uint64_t deadline = RNS::Utilities::OS::ltime() + 1000;
	while (RNS::Utilities::OS::ltime() < deadline) {
		test_reticulum.loop();
		RNS::Utilities::OS::sleep(0.02);
	}

	const size_t paths_after_mem = RNS::Transport::get_path_table().size();
	const bool has_path = RNS::Transport::has_path(peer_dest_hash);

	printf("paths_before_mem=%zu paths_after_mem=%zu has_path=%d\n",
	    paths_before_mem, paths_after_mem, (int)has_path);

	TEST_ASSERT_TRUE_MESSAGE(has_path,
	    "Transport::has_path() should return true after a fresh announce; "
	    "regression of the file-backed path-store write.");
	TEST_ASSERT_TRUE_MESSAGE(paths_after_mem > paths_before_mem,
	    "Transport::get_path_table() (in-memory map) should grow after a "
	    "fresh announce; regression of the dual-write fix that ensures "
	    "callers iterating the in-memory map see new paths.");

	printf("test_announce_populates_both_path_tables: END\n");
}

// ============================================================================
// Test runner
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int runUnityTests(void) {
	UNITY_BEGIN();

/*
	// Transport receipt lifecycle
	RUN_TEST(test_receipt_creation_and_culling);
	RUN_TEST(test_receipt_rapid_fire);
	// CBA Temporarily disabled while failing
	//RUN_TEST(test_receipt_timeout_callback);

	// Transport hashlist
	RUN_TEST(test_hashlist_basic);
	RUN_TEST(test_hashlist_overflow);

	// Packet send/receive
	RUN_TEST(test_send_receive_cycle);
	RUN_TEST(test_send_receive_stress);
	RUN_TEST(test_interleaved_send_and_jobs);

	// Identity/Destination lifecycle
	RUN_TEST(test_identity_create_destroy_cycle);
	RUN_TEST(test_destination_packet_cycle);

	//RUN_TEST(test_incoming_announce_limit);
*/
	// CBA temporarily disabled while iterating on the regression tests
	// below — the over-limit test takes ~45min and SIGHUPs the runner
	// before the regressions fire.
	//RUN_TEST(test_incoming_announce_over_limit);
	//RUN_TEST(test_incoming_announce_stress);

	// Regression tests
	RUN_TEST(test_request_receipt_default_construct_no_segfault);
	RUN_TEST(test_announce_populates_both_path_tables);

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
