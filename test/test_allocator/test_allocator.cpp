#include <unity.h>

#include <microStore/Adapters/UniversalFileSystem.h>

#include "microReticulum/Bytes.h"
#include "microReticulum/Transport.h"
#include "microReticulum/Persistence/DestinationEntry.h"
#include "microReticulum/Utilities/OS.h"
#include "microReticulum/Utilities/Memory.h"

#include <map>
#include <string>
#include <scoped_allocator>

//#define VECTOR_INTERNAL_CONTENT
//#define VECTOR_EXTERNAL_CONTENT
//#define BYTES_INTERNAL_CONTENT
#define BYTES_EXTERNAL_CONTENT

class TestObject {
public:
	TestObject() {}
	TestObject(const char* str, std::vector<std::string>& vec, RNS::Bytes& buf) : _str(str), _vec(vec), _external_buf(buf) {
#ifdef VECTOR_INTERNAL_CONTENT
		// CBA The following vector entries use psram even though they're created internally (not part of base object)
		_vec.push_back("fee");
		_vec.push_back("fum");
#endif
#ifdef BYTES_INTERNAL_CONTENT
		// CBA The following Bytes content use psram even though they're created internally (not part of base object)
		_internal_buf.assign("This is a Bytes buffer allocated INSIDE of TestObject");
#endif
	}
	inline bool operator < (const TestObject& test) const { return _str < test._str; }

	inline const std::string toString() const { return std::string("TestObject(") + _str + "," + std::to_string(_vec.size()) + "," + _internal_buf.toString() + "," + _external_buf.toString() + ")"; }

	// CBA All of the following empty containers are included in the memory allocated for std::map and use psram
	std::string _str;
	std::vector<std::string> _vec;
	RNS::Bytes _internal_buf;
	RNS::Bytes _external_buf;
};


void test_map_allocator() {

	// CBA TODO Figure out how to selectively use PSRAM for Bytes and Packet objects.

	// Trigger creation of both heap and psram pool before starting
	RNS::Utilities::Memory::pool_init(RNS::Utilities::Memory::heap_pool_info);
	RNS::Utilities::Memory::pool_init(RNS::Utilities::Memory::altheap_pool_info);
	//RNS::Utilities::Memory::dump_heap_stats();
	HEAD("Initial Stats:", RNS::LOG_TRACE);
	RNS::Utilities::Memory::dump_basic_pool_stats();
	//RNS::Utilities::Memory::dump_basic_allocator_stats();

	{
		//using TestMap = std::map<Bytes, TestObject, std::less<Bytes>, RNS::Utilities::Memory::ContainerAllocator<std::pair<const Bytes, TestObject>>>;
		using TestMap = std::map<std::string, TestObject, std::less<std::string>, RNS::Utilities::Memory::ContainerAllocator<std::pair<const std::string, TestObject>>>;
		//using TestAlloc = RNS::Utilities::Memory::ContainerAllocator<std::pair<const std::string, TestObject>>;
		//using TestStringAlloc = RNS::Utilities::Memory::ContainerAllocator<char>;
		//using TestMap = std::map<std::string, TestObject, std::less<std::string>, std::scoped_allocator_adaptor<TestAlloc, TestStringAlloc>>;

		HEAD("Post-map Stats:", RNS::LOG_TRACE);
		RNS::Utilities::Memory::dump_basic_pool_stats();
		//RNS::Utilities::Memory::dump_basic_allocator_stats();

		// CBA The following empty vector object is included in the memory allocated for std::map and uses psram
		std::vector<std::string> vec;
#ifdef VECTOR_EXTERNAL_CONTENT
		// CBA The following vector entries use psram
		vec.push_back("foo");
		vec.push_back("bar");
#endif

		// CBA The following empty Bytes object is included in the memory allocated for std::map and uses psram
		RNS::Bytes buf;
#ifdef BYTES_EXTERNAL_CONTENT
		// CBA The following Bytes content use psram
		buf.assign("This is a Bytes buffer allocated OUTSIDE of TestObject");
#endif

		TestMap map;
		map.insert({"two", {"two", vec, buf}});
		map.insert({"one", {"one", vec, buf}});

		HEAD("Post-insert Stats:", RNS::LOG_TRACE);
		RNS::Utilities::Memory::dump_basic_pool_stats();
		//RNS::Utilities::Memory::dump_basic_allocator_stats();

		TEST_ASSERT_EQUAL(2, map.size());
		for (const auto& [key, entry] : map) {
			TRACEF("key: %s, entry: %s", key.c_str(), entry.toString().c_str());
		}
	}

	HEAD("Post-free Stats:", RNS::LOG_TRACE);
	RNS::Utilities::Memory::dump_basic_pool_stats();
	//RNS::Utilities::Memory::dump_basic_allocator_stats();

}


#ifdef RNS_POOL_CAPACITY_TEST
void test_one_mib_pool_holds_all_bounded_transport_tables_under_fragmentation() {
#if RNS_ALTHEAP_POOL_BUFFER_SIZE != 1048576
#error "This saturation test must exercise an exact 1 MiB container pool"
#endif
	RNS::Utilities::Memory::pool_init(RNS::Utilities::Memory::altheap_pool_info);
	const auto key = [](uint16_t value) {
		uint8_t raw[16] = {0};
		raw[14] = static_cast<uint8_t>(value >> 8);
		raw[15] = static_cast<uint8_t>(value);
		return RNS::Bytes(raw, sizeof(raw));
	};

	microStore::FileSystem filesystem{microStore::Adapters::UniversalFileSystem()};
	TEST_ASSERT_TRUE(filesystem.init());
	RNS::Persistence::PathStore persistent_path_store(4096, 2);
	persistent_path_store.set_max_recs(400);
	TEST_ASSERT_TRUE(persistent_path_store.init(filesystem, "/tmp/microreticulum-pool-capacity-paths", true));
	RNS::Persistence::PathTable enumerable_paths;
	RNS::Transport::AnnounceTable active_announces;
	RNS::Transport::AnnounceTable held_announces;

	uint8_t value[16] = {0};
	for (uint16_t i = 0; i < 400; ++i) {
		const RNS::Bytes path_key = key(i);
		TEST_ASSERT_TRUE(persistent_path_store.put(path_key.data(), path_key.size(), value, sizeof(value)));
	}
	for (uint16_t i = 0; i < 100; ++i) {
		RNS::Persistence::DestinationEntry path;
		path._timestamp = i;
		enumerable_paths.insert({key(500 + i), path});
		RNS::Transport::AnnounceEntry announce(
			i, 0, 0, {}, 0, {RNS::Type::NONE}, 0, false, {RNS::Type::NONE});
		active_announces.insert({key(700 + i), announce});
		held_announces.insert({key(900 + i), announce});
	}
	RNS::Identity::known_destinations_maxsize(100);
	for (uint16_t i = 0; i < 101; ++i) {
		uint8_t packet_hash_raw[32] = {0};
		uint8_t public_key_raw[RNS::Type::Identity::KEYSIZE / 8] = {0};
		packet_hash_raw[30] = public_key_raw[62] = static_cast<uint8_t>(i >> 8);
		packet_hash_raw[31] = public_key_raw[63] = static_cast<uint8_t>(i);
		RNS::Identity::remember(
			RNS::Bytes(packet_hash_raw, sizeof(packet_hash_raw)), key(1100 + i),
			RNS::Bytes(public_key_raw, sizeof(public_key_raw)));
	}

	for (uint16_t i = 0; i < 400; i += 2) {
		const RNS::Bytes path_key = key(i);
		TEST_ASSERT_TRUE(persistent_path_store.remove(path_key.data(), path_key.size()));
	}
	for (uint16_t i = 0; i < 200; ++i) {
		const RNS::Bytes path_key = key(1300 + i);
		TEST_ASSERT_TRUE(persistent_path_store.put(path_key.data(), path_key.size(), value, sizeof(value)));
	}
	const RNS::Bytes overflow_path_key = key(1600);
	TEST_ASSERT_TRUE(persistent_path_store.put(
		overflow_path_key.data(), overflow_path_key.size(), value, sizeof(value)));

	RNS::Persistence::DestinationEntry overflow_path;
	overflow_path._timestamp = 101;
	enumerable_paths.insert({key(600), overflow_path});
	enumerable_paths.erase(enumerable_paths.begin());
	RNS::Transport::AnnounceEntry overflow_announce(
		101, 0, 0, {}, 0, {RNS::Type::NONE}, 0, false, {RNS::Type::NONE});
	active_announces.insert({key(800), overflow_announce});
	active_announces.erase(active_announces.begin());
	held_announces.insert({key(1000), overflow_announce});
	held_announces.erase(held_announces.begin());

	TEST_ASSERT_EQUAL_size_t(400, persistent_path_store.size());
	TEST_ASSERT_EQUAL_size_t(100, enumerable_paths.size());
	TEST_ASSERT_EQUAL_size_t(100, active_announces.size());
	TEST_ASSERT_EQUAL_size_t(100, held_announces.size());
	TEST_ASSERT_EQUAL_size_t(100, RNS::Identity::known_destinations().size());
	TEST_ASSERT_TRUE(RNS::Utilities::Memory::container_allocator_info.alloc_size < 1048576);
	const uint32_t faults_before_probe = RNS::Utilities::Memory::container_allocator_info.alloc_fault;
	RNS::Utilities::Memory::ContainerAllocator<uint8_t> probe_allocator;
	uint8_t* probe = probe_allocator.allocate(65536);
	TEST_ASSERT_NOT_NULL(probe);
	probe_allocator.deallocate(probe, 65536);
	TEST_ASSERT_EQUAL_UINT32(
		faults_before_probe, RNS::Utilities::Memory::container_allocator_info.alloc_fault);
	persistent_path_store.clear();
}
#endif


void setUp(void) {
	// set stuff up here before each test
}

void tearDown(void) {
	// clean stuff up here after each test
}

int runUnityTests(void) {
	UNITY_BEGIN();

	// Suite-level setup

	// Run tests
	RUN_TEST(test_map_allocator);
#ifdef RNS_POOL_CAPACITY_TEST
	RUN_TEST(test_one_mib_pool_holds_all_bounded_transport_tables_under_fragmentation);
#endif

	// Suite-level teardown

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
