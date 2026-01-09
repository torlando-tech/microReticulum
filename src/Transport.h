#pragma once

#include "Packet.h"
#include "Bytes.h"
#include "Type.h"

#include <map>
#include <vector>
#include <list>
#include <set>
#include <array>
#include <memory>
#include <functional>
#include <stdint.h>

//#define INTERFACES_SET
//#define INTERFACES_LIST
//#define INTERFACES_MAP
#define INTERFACES_POOL  // Use fixed pool instead of STL container

//#define DESTINATIONS_SET
//#define DESTINATIONS_MAP
#define DESTINATIONS_POOL  // Use fixed pool instead of STL container

namespace RNS {

	class Reticulum;
	class Identity;
	class Destination;
	class Interface;
	class Link;
	class Packet;
	class PacketReceipt;

	class AnnounceHandler {
	public:
		// The initialisation method takes the optional
		// aspect_filter argument. If aspect_filter is set to
		// None, all announces will be passed to the instance.
		// If only some announces are wanted, it can be set to
		// an aspect string.
		AnnounceHandler(const char* aspect_filter = nullptr) { if (aspect_filter != nullptr) _aspect_filter = aspect_filter; }
		// This method will be called by Reticulums Transport
		// system when an announce arrives that matches the
		// configured aspect filter. Filters must be specific,
		// and cannot use wildcards.
		virtual void received_announce(const Bytes& destination_hash, const Identity& announced_identity, const Bytes& app_data) = 0;
		std::string& aspect_filter() { return _aspect_filter; }
	private:
		std::string _aspect_filter;
	};
	using HAnnounceHandler = std::shared_ptr<AnnounceHandler>;

    /*
    Through static methods of this class you can interact with the
    Transport system of Reticulum.
    */
	class Transport {

	public:
		class Callbacks {
		public:
			using receive_packet = void(*)(const Bytes& raw, const Interface& interface);
			using transmit_packet = void(*)(const Bytes& raw, const Interface& interface);
			using filter_packet = bool(*)(const Packet& packet);
		public:
			receive_packet _receive_packet = nullptr;
			transmit_packet _transmit_packet = nullptr;
			filter_packet _filter_packet = nullptr;
		friend class Transport;
		};

	public:

		class PacketEntry {
		public:
			PacketEntry() {}
			PacketEntry(const Bytes& raw, double sent_at, const Bytes& destination_hash) :
				_raw(raw),
				_sent_at(sent_at),
				_destination_hash(destination_hash)
			{
			}
			PacketEntry(const Packet& packet) :
				_raw(packet.raw()),
				_sent_at(packet.sent_at()),
				_destination_hash(packet.destination_hash())
			{
			}
		public:
			Bytes _raw;
			double _sent_at = 0;
			Bytes _destination_hash;
			bool _cached = false;
#ifndef NDEBUG
			inline std::string debugString() const {
				std::string dump;
				dump = "PacketEntry: destination_hash=" + _destination_hash.toHex() +
					" sent_at=" + std::to_string(_sent_at);
				return dump;
			}
#endif
		};

		// CBA TODO Analyze safety of using Inrerface references here
		// CBA TODO Analyze safety of using Packet references here
		class DestinationEntry {
		public:
			DestinationEntry() {}
			DestinationEntry(double timestamp, const Bytes& received_from, uint8_t announce_hops, double expires, const std::set<Bytes>& random_blobs, const Bytes& receiving_interface, const Bytes& packet) :
				_timestamp(timestamp),
				_received_from(received_from),
				_hops(announce_hops),
				_expires(expires),
				_random_blobs(random_blobs),
				_receiving_interface(receiving_interface),
				_announce_packet(packet)
			{
			}
		public:
			inline Interface receiving_interface() const { return find_interface_from_hash(_receiving_interface); }
			inline Packet announce_packet() const { return get_cached_packet(_announce_packet); }
		public:
			double _timestamp = 0;
			Bytes _received_from;
			uint8_t _hops = 0;
			double _expires = 0;
			std::set<Bytes> _random_blobs;
			//Interface _receiving_interface = {Type::NONE};
			Bytes _receiving_interface;
			//const Packet& _announce_packet;
			//Packet _announce_packet = {Type::NONE};
			Bytes _announce_packet;
			inline bool operator < (const DestinationEntry& entry) const {
				// sort by ascending timestamp (oldest entries at the top)
				return _timestamp < entry._timestamp;
			}
#ifndef NDEBUG
			inline std::string debugString() const {
				std::string dump;
				dump = "DestinationEntry: timestamp=" + std::to_string(_timestamp) +
					" received_from=" + _received_from.toHex() +
					" hops=" + std::to_string(_hops) +
					" expires=" + std::to_string(_expires) +
					//" random_blobs=" + _random_blobs +
					" receiving_interface=" + _receiving_interface.toHex() +
					" announce_packet=" + _announce_packet.toHex();
				dump += " random_blobs=(";
				for (auto& blob : _random_blobs) {
					dump += blob.toHex() + ",";
				}
				dump += ")";
				return dump;
			}
#endif
		};

		// CBA TODO Analyze safety of using Inrerface references here
		// CBA TODO Analyze safety of using Packet references here
		class AnnounceEntry {
		public:
			AnnounceEntry() {}
			AnnounceEntry(double timestamp, double retransmit_timeout, uint8_t retries, const Bytes& received_from, uint8_t hops, const Packet& packet, uint8_t local_rebroadcasts, bool block_rebroadcasts, const Interface& attached_interface) :
				_timestamp(timestamp),
				_retransmit_timeout(retransmit_timeout),
				_retries(retries),
				_received_from(received_from),
				_hops(hops),
				_packet(packet),
				_local_rebroadcasts(local_rebroadcasts),
				_block_rebroadcasts(block_rebroadcasts),
				_attached_interface(attached_interface)
			{
			}
		public:
			double _timestamp = 0;
			double _retransmit_timeout = 0;
			uint8_t _retries = 0;
			Bytes _received_from;
			uint8_t _hops = 0;
			// CBA Storing packet reference causes memory issues, presumably because orignal packet is being destroyed
			//  MUST use instance instad of reference!!!
			//const Packet& _packet;
			Packet _packet = {Type::NONE};
			uint8_t _local_rebroadcasts = 0;
			bool _block_rebroadcasts = false;
			Interface _attached_interface = {Type::NONE};
		};

		// CBA TODO Analyze safety of using Inrerface references here
		class LinkEntry {
		public:
			LinkEntry() {}
			LinkEntry(double timestamp, const Bytes& next_hop, const Interface& outbound_interface, uint8_t remaining_hops, const Interface& receiving_interface, uint8_t hops, const Bytes& destination_hash, bool validated, double proof_timeout) :
				_timestamp(timestamp),
				_next_hop(next_hop),
				_outbound_interface(outbound_interface),
				_remaining_hops(remaining_hops),
				_receiving_interface(receiving_interface),
				_hops(hops),
				_destination_hash(destination_hash),
				_validated(validated),
				_proof_timeout(proof_timeout)
			{
			}
		public:
			double _timestamp = 0;
			Bytes _next_hop;
			Interface _outbound_interface = {Type::NONE};
			uint8_t _remaining_hops = 0;
			Interface _receiving_interface = {Type::NONE};
			uint8_t _hops = 0;
			Bytes _destination_hash;
			bool _validated = false;
			double _proof_timeout = 0;
		};

		// CBA TODO Analyze safety of using Inrerface references here
		class ReverseEntry {
		public:
			ReverseEntry() {}
			ReverseEntry(const Interface& receiving_interface, const Interface& outbound_interface, double timestamp) :
				_receiving_interface(receiving_interface),
				_outbound_interface(outbound_interface),
				_timestamp(timestamp)
			{
			}
		public:
			Interface _receiving_interface = {Type::NONE};
			Interface _outbound_interface = {Type::NONE};
			double _timestamp = 0;
		};

		// CBA TODO Analyze safety of using Inrerface references here
		class PathRequestEntry {
		public:
			PathRequestEntry() {}
			PathRequestEntry(const Bytes& destination_hash, double timeout, const Interface& requesting_interface) :
				_destination_hash(destination_hash),
				_timeout(timeout),
				_requesting_interface(requesting_interface)
			{
			}
		public:
			Bytes _destination_hash;
			double _timeout = 0;
			const Interface _requesting_interface = {Type::NONE};
		};

/*
		// CBA TODO Analyze safety of using Inrerface references here
		class SerialisedEntry {
		public:
			SerialisedEntry(const Bytes& destination_hash, double timestamp, const Bytes& received_from, uint8_t announce_hops, double expires, const std::set<Bytes>& random_blobs, Interface& receiving_interface, const Packet& packet) :
				_destination_hash(destination_hash),
				_timestamp(timestamp),
				_hops(announce_hops),
				_expires(expires),
				_random_blobs(random_blobs),
				_receiving_interface(receiving_interface),
				_announce_packet(packet)
			{
			}
		public:
			Bytes _destination_hash;
			double _timestamp = 0;
			Bytes _received_from;
			uint8_t _hops = 0;
			double _expires = 0;
			std::set<Bytes> _random_blobs;
			Interface _receiving_interface = {Type::NONE};
			Packet _announce_packet = {Type::NONE};
		};
*/

		// CBA TODO Analyze safety of using Inrerface references here
		class TunnelEntry {
		public:
			TunnelEntry() {}
			TunnelEntry(const Bytes& tunnel_id, const Bytes& interface_hash, double expires) :
				_tunnel_id(tunnel_id),
				_interface_hash(interface_hash),
				_expires(expires)
			{
			}
			void clear() {
				_tunnel_id.clear();
				_interface_hash.clear();
				_serialised_paths.clear();
				_expires = 0;
			}
		public:
			Bytes _tunnel_id;
			Bytes _interface_hash;
			std::map<Bytes, DestinationEntry> _serialised_paths;
			double _expires = 0;
		};

		class RateEntry {
		public:
			RateEntry() {}
			RateEntry(double now) :
				_last(now)
			{
				_timestamps.push_back(now);
			}
		public:
			double _last = 0.0;
			double _rate_violations = 0.0;
			double _blocked_until = 0.0;
			std::vector<double> _timestamps;
		};

		// Fixed-size pool structures for zero heap fragmentation
		static constexpr size_t ANNOUNCE_TABLE_SIZE = 8;  // Reduced for testing
		struct AnnounceTableSlot {
			bool in_use = false;
			Bytes destination_hash;
			AnnounceEntry entry;
			void clear() { in_use = false; destination_hash.clear(); entry = AnnounceEntry(); }
		};
		static AnnounceTableSlot _announce_table_pool[ANNOUNCE_TABLE_SIZE];
		static AnnounceTableSlot* find_announce_table_slot(const Bytes& hash);
		static AnnounceTableSlot* find_empty_announce_table_slot();
		static size_t announce_table_count();

		static constexpr size_t DESTINATION_TABLE_SIZE = 16;  // Reduced for testing
		struct DestinationTableSlot {
			bool in_use = false;
			Bytes destination_hash;
			DestinationEntry entry;
			void clear() { in_use = false; destination_hash.clear(); entry = DestinationEntry(); }
		};
		static DestinationTableSlot _destination_table_pool[DESTINATION_TABLE_SIZE];
		static DestinationTableSlot* find_destination_table_slot(const Bytes& hash);
		static DestinationTableSlot* find_empty_destination_table_slot();
		static size_t destination_table_count();

		static constexpr size_t REVERSE_TABLE_SIZE = 8;  // Reduced for testing
		struct ReverseTableSlot {
			bool in_use = false;
			Bytes packet_hash;
			ReverseEntry entry;
			void clear() { in_use = false; packet_hash.clear(); entry = ReverseEntry(); }
		};
		static ReverseTableSlot _reverse_table_pool[REVERSE_TABLE_SIZE];
		static ReverseTableSlot* find_reverse_table_slot(const Bytes& hash);
		static ReverseTableSlot* find_empty_reverse_table_slot();
		static size_t reverse_table_count();

		static constexpr size_t LINK_TABLE_SIZE = 8;  // Reduced for testing
		struct LinkTableSlot {
			bool in_use = false;
			Bytes link_id;
			LinkEntry entry;
			void clear() { in_use = false; link_id.clear(); entry = LinkEntry(); }
		};
		static LinkTableSlot _link_table_pool[LINK_TABLE_SIZE];
		static LinkTableSlot* find_link_table_slot(const Bytes& id);
		static LinkTableSlot* find_empty_link_table_slot();
		static size_t link_table_count();

		static constexpr size_t HELD_ANNOUNCES_SIZE = 8;  // Reduced for testing
		struct HeldAnnounceSlot {
			bool in_use = false;
			Bytes destination_hash;
			AnnounceEntry entry;
			void clear() { in_use = false; destination_hash.clear(); entry = AnnounceEntry(); }
		};
		static HeldAnnounceSlot _held_announces_pool[HELD_ANNOUNCES_SIZE];
		static HeldAnnounceSlot* find_held_announce_slot(const Bytes& hash);
		static HeldAnnounceSlot* find_empty_held_announce_slot();
		static size_t held_announces_count();

		static constexpr size_t TUNNELS_SIZE = 16;
		struct TunnelSlot {
			bool in_use = false;
			Bytes tunnel_id;
			TunnelEntry entry;
			void clear() { in_use = false; tunnel_id.clear(); entry.clear(); }
		};
		static TunnelSlot _tunnels_pool[TUNNELS_SIZE];
		static TunnelSlot* find_tunnel_slot(const Bytes& id);
		static TunnelSlot* find_empty_tunnel_slot();
		static size_t tunnels_count();

		static constexpr size_t ANNOUNCE_RATE_TABLE_SIZE = 8;  // Reduced for testing
		struct RateTableSlot {
			bool in_use = false;
			Bytes destination_hash;
			RateEntry entry;
			void clear() { in_use = false; destination_hash.clear(); entry = RateEntry(); }
		};
		static RateTableSlot _announce_rate_table_pool[ANNOUNCE_RATE_TABLE_SIZE];
		static RateTableSlot* find_rate_table_slot(const Bytes& hash);
		static RateTableSlot* find_empty_rate_table_slot();
		static size_t announce_rate_table_count();

		static constexpr size_t PATH_REQUESTS_SIZE = 8;  // Reduced for testing
		struct PathRequestSlot {
			bool in_use = false;
			Bytes destination_hash;
			double timestamp = 0;
			void clear() { in_use = false; destination_hash.clear(); timestamp = 0; }
		};
		static PathRequestSlot _path_requests_pool[PATH_REQUESTS_SIZE];
		static PathRequestSlot* find_path_request_slot(const Bytes& hash);
		static PathRequestSlot* find_empty_path_request_slot();
		static size_t path_requests_count();

		// Receipts fixed array
		static constexpr size_t RECEIPTS_SIZE = 8;  // Reduced for testing
		static PacketReceipt _receipts_pool[RECEIPTS_SIZE];
		static size_t _receipts_count;
		static bool receipts_add(const PacketReceipt& receipt);
		static bool receipts_remove(const PacketReceipt& receipt);
		static inline size_t receipts_count() { return _receipts_count; }

		// Packet hashlist circular buffer (replaces std::set<Bytes>)
		static constexpr size_t PACKET_HASHLIST_SIZE = 64;  // Reduced for testing
		static Bytes _packet_hashlist_buffer[PACKET_HASHLIST_SIZE];
		static size_t _packet_hashlist_head;
		static size_t _packet_hashlist_count;
		static bool packet_hashlist_contains(const Bytes& hash);
		static void packet_hashlist_add(const Bytes& hash);
		static void packet_hashlist_clear();
		static inline size_t packet_hashlist_count() { return _packet_hashlist_count; }

		// Discovery PR tags circular buffer (replaces std::set<Bytes>)
		static constexpr size_t DISCOVERY_PR_TAGS_SIZE = 32;
		static Bytes _discovery_pr_tags_buffer[DISCOVERY_PR_TAGS_SIZE];
		static size_t _discovery_pr_tags_head;
		static size_t _discovery_pr_tags_count;
		static bool discovery_pr_tags_contains(const Bytes& tag);
		static void discovery_pr_tags_add(const Bytes& tag);

		// Pending links fixed array (replaces std::set<Link>)
		static constexpr size_t PENDING_LINKS_SIZE = 4;  // Reduced for testing
		static Link _pending_links_pool[PENDING_LINKS_SIZE];
		static size_t _pending_links_count;
		static bool pending_links_contains(const Link& link);
		static bool pending_links_add(const Link& link);
		static bool pending_links_remove(const Link& link);
		static inline size_t pending_links_count() { return _pending_links_count; }

		// Active links fixed array (replaces std::set<Link>)
		static constexpr size_t ACTIVE_LINKS_SIZE = 4;  // Reduced for testing
		static Link _active_links_pool[ACTIVE_LINKS_SIZE];
		static size_t _active_links_count;
		static bool active_links_contains(const Link& link);
		static bool active_links_add(const Link& link);
		static bool active_links_remove(const Link& link);
		static inline size_t active_links_count() { return _active_links_count; }

		// Control hashes fixed array (replaces std::set<Bytes>)
		static constexpr size_t CONTROL_HASHES_SIZE = 8;
		static Bytes _control_hashes_pool[CONTROL_HASHES_SIZE];
		static size_t _control_hashes_count;
		static bool control_hashes_contains(const Bytes& hash);
		static bool control_hashes_add(const Bytes& hash);
		static size_t control_hashes_size();

		// Control destinations fixed array (replaces std::set<Destination>)
		static constexpr size_t CONTROL_DESTINATIONS_SIZE = 8;
		static Destination _control_destinations_pool[CONTROL_DESTINATIONS_SIZE];
		static size_t _control_destinations_count;
		static bool control_destinations_add(const Destination& dest);
		static size_t control_destinations_size();

		// Announce handlers fixed array (replaces std::set<HAnnounceHandler>)
		static constexpr size_t ANNOUNCE_HANDLERS_SIZE = 8;
		static HAnnounceHandler _announce_handlers_pool[ANNOUNCE_HANDLERS_SIZE];
		static size_t _announce_handlers_count;
		static bool announce_handlers_add(HAnnounceHandler handler);
		static bool announce_handlers_remove(HAnnounceHandler handler);
		static size_t announce_handlers_size();

		// Local client interfaces fixed array (replaces std::set<reference_wrapper<Interface>>)
		static constexpr size_t LOCAL_CLIENT_INTERFACES_SIZE = 8;
		static const Interface* _local_client_interfaces_pool[LOCAL_CLIENT_INTERFACES_SIZE];
		static size_t _local_client_interfaces_count;
		static bool local_client_interfaces_contains(const Interface& iface);
		static bool local_client_interfaces_add(const Interface& iface);
		static bool local_client_interfaces_remove(const Interface& iface);
		static size_t local_client_interfaces_size();

		// Interfaces fixed array (replaces std::map<Bytes, Interface&>)
		struct InterfaceSlot {
			bool in_use = false;
			Bytes hash;
			Interface* interface_ptr = nullptr;
			void clear() { in_use = false; hash.clear(); interface_ptr = nullptr; }
		};
		static constexpr size_t INTERFACES_POOL_SIZE = 8;
		static InterfaceSlot _interfaces_pool[INTERFACES_POOL_SIZE];
		static InterfaceSlot* find_interface_slot(const Bytes& hash);
		static InterfaceSlot* find_empty_interface_slot();
		static size_t interfaces_count();
		static bool interfaces_contains(const Bytes& hash);

		// Destinations fixed array (replaces std::map<Bytes, Destination>)
		struct DestinationSlot {
			bool in_use = false;
			Bytes hash;
			Destination destination;
			void clear() { in_use = false; hash.clear(); destination = Destination(); }
		};
		static constexpr size_t DESTINATIONS_POOL_SIZE = 32;
		static DestinationSlot _destinations_pool[DESTINATIONS_POOL_SIZE];
		static DestinationSlot* find_destination_slot(const Bytes& hash);
		static DestinationSlot* find_empty_destination_slot();
		static size_t destinations_count();
		static bool destinations_contains(const Bytes& hash);

		// Discovery path requests fixed array (replaces std::map<Bytes, PathRequestEntry>)
		struct DiscoveryPathRequestSlot {
			bool in_use = false;
			Bytes destination_hash;  // key
			double timeout = 0;
			Interface requesting_interface{Type::NONE};
			void clear() { in_use = false; destination_hash.clear(); timeout = 0; requesting_interface = Interface(Type::NONE); }
		};
		static constexpr size_t DISCOVERY_PATH_REQUESTS_SIZE = 32;
		static DiscoveryPathRequestSlot _discovery_path_requests_pool[DISCOVERY_PATH_REQUESTS_SIZE];
		static DiscoveryPathRequestSlot* find_discovery_path_request_slot(const Bytes& hash);
		static DiscoveryPathRequestSlot* find_empty_discovery_path_request_slot();
		static size_t discovery_path_requests_count();

		// Pending local path requests fixed array (replaces std::map<Bytes, const Interface&>)
		struct PendingLocalPathRequestSlot {
			bool in_use = false;
			Bytes destination_hash;  // key
			Interface attached_interface{Type::NONE};
			void clear() { in_use = false; destination_hash.clear(); attached_interface = Interface(Type::NONE); }
		};
		static constexpr size_t PENDING_LOCAL_PATH_REQUESTS_SIZE = 32;
		static PendingLocalPathRequestSlot _pending_local_path_requests_pool[PENDING_LOCAL_PATH_REQUESTS_SIZE];
		static PendingLocalPathRequestSlot* find_pending_local_path_request_slot(const Bytes& hash);
		static PendingLocalPathRequestSlot* find_empty_pending_local_path_request_slot();
		static size_t pending_local_path_requests_count();

	public:
		static void start(const Reticulum& reticulum_instance);
		static void loop();
		static void jobs();
		static void transmit(Interface& interface, const Bytes& raw);
		static bool outbound(Packet& packet);
		static bool packet_filter(const Packet& packet);
		//static void inbound(const Bytes& raw, const Interface& interface = {Type::NONE});
		static void inbound(const Bytes& raw, const Interface& interface);
		static void inbound(const Bytes& raw);
		static void synthesize_tunnel(const Interface& interface);
		static void tunnel_synthesize_handler(const Bytes& data, const Packet& packet);
		static void handle_tunnel(const Bytes& tunnel_id, const Interface& interface);
		static void register_interface(Interface& interface);
		static void deregister_interface(const Interface& interface);
#if defined(INTERFACES_POOL)
		static std::map<Bytes, Interface*> get_interfaces();
#else
		inline static const std::map<Bytes, Interface&> get_interfaces() { return _interfaces; }
#endif
		static void register_destination(Destination& destination);
		static void deregister_destination(const Destination& destination);
		static void register_link(Link& link);
		static void activate_link(Link& link);
		static void register_announce_handler(HAnnounceHandler handler);
		static void deregister_announce_handler(HAnnounceHandler handler);
		static Interface find_interface_from_hash(const Bytes& interface_hash);
		static bool should_cache_packet(const Packet& packet);
		static bool cache_packet(const Packet& packet, bool force_cache = false);
		static Packet get_cached_packet(const Bytes& packet_hash);
		static bool clear_cached_packet(const Bytes& packet_hash);
		static bool cache_request_packet(const Packet& packet);
		static void cache_request(const Bytes& packet_hash, const Destination& destination);
		static bool remove_path(const Bytes& destination_hash);
		static bool has_path(const Bytes& destination_hash);
		static uint8_t hops_to(const Bytes& destination_hash);
		static Bytes next_hop(const Bytes& destination_hash);
		static Interface next_hop_interface(const Bytes& destination_hash);
		static uint32_t next_hop_interface_bitrate(const Bytes& destination_hash);
		static uint16_t next_hop_interface_hw_mtu(const Bytes& destination_hash);
		static double next_hop_per_bit_latency(const Bytes& destination_hash);
		static double next_hop_per_byte_latency(const Bytes& destination_hash);
		static double first_hop_timeout(const Bytes& destination_hash);
		static double extra_link_proof_timeout(const Interface& interface);
		static bool expire_path(const Bytes& destination_hash);
		//static void request_path(const Bytes& destination_hash, const Interface& on_interface = {Type::NONE}, const Bytes& tag = {}, bool recursive = false);
		static void request_path(const Bytes& destination_hash, const Interface& on_interface, const Bytes& tag = {}, bool recursive = false);
		static void request_path(const Bytes& destination_hash);
		static void path_request_handler(const Bytes& data, const Packet& packet);
		static void path_request(const Bytes& destination_hash, bool is_from_local_client, const Interface& attached_interface, const Bytes& requestor_transport_id = {}, const Bytes& tag = {});
		static bool from_local_client(const Packet& packet);
		static bool is_local_client_interface(const Interface& interface);
		static bool interface_to_shared_instance(const Interface& interface);
		static void detach_interfaces();
		static void shared_connection_disappeared();
		static void shared_connection_reappeared();
		static void drop_announce_queues();
		static uint64_t announce_emitted(const Packet& packet);
		static void write_packet_hashlist();
		static bool read_path_table();
		static bool write_path_table();
		static void read_tunnel_table();
		static void write_tunnel_table();
		static void persist_data();
		static void clean_caches();
		static void dump_stats();
		static void exit_handler();

		static uint16_t remove_reverse_entries(const std::vector<Bytes>& hashes);
		static uint16_t remove_links(const std::vector<Bytes>& hashes);
		static uint16_t remove_paths(const std::vector<Bytes>& hashes);
		static uint16_t remove_discovery_path_requests(const std::vector<Bytes>& hashes);
		static uint16_t remove_tunnels(const std::vector<Bytes>& hashes);

		static Destination find_destination_from_hash(const Bytes& destination_hash);

		// CBA
		static void cull_path_table();

		// getters/setters
		static inline void set_receive_packet_callback(Callbacks::receive_packet callback) { _callbacks._receive_packet = callback; }
		static inline void set_transmit_packet_callback(Callbacks::transmit_packet callback) { _callbacks._transmit_packet = callback; }
		static inline void set_filter_packet_callback(Callbacks::filter_packet callback) { _callbacks._filter_packet = callback; }
		static inline const Reticulum& reticulum() { return _owner; }
		static inline const Identity& identity() { return _identity; }
		inline static uint16_t path_table_maxsize() { return _path_table_maxsize; }
		inline static void path_table_maxsize(uint16_t path_table_maxsize) { _path_table_maxsize = path_table_maxsize; }
		inline static bool probe_destination_enabled() { return _probe_destination_enabled; }
		inline static void probe_destination_enabled(bool enabled) { _probe_destination_enabled = enabled; }
		inline static void path_table_maxpersist(uint16_t path_table_maxpersist) { _path_table_maxpersist = path_table_maxpersist; }
		// CBA TEST
		static inline void identity(Identity& identity) { _identity = identity; }

		// Build maps from pools on demand (returns by value since pools replaced the maps)
		static std::map<Bytes, DestinationEntry> get_destination_table();
		static std::map<Bytes, RateEntry> get_announce_rate_table();
		static std::map<Bytes, LinkEntry> get_link_table();

	private:
		// CBA MUST use references to interfaces here in order for virtul overrides for send/receive to work
#if defined(INTERFACES_SET)
		// set sorted, can use find
		//static std::set<std::reference_wrapper<const Interface>, std::less<const Interface>> _interfaces;           // All active interfaces
		static std::set<std::reference_wrapper<Interface>, std::less<Interface>> _interfaces;           // All active interfaces
#elif defined(INTERFACES_LIST)
		// list is unsorted, can't use find
		static std::list<std::reference_wrapper<Interface>> _interfaces;           // All active interfaces
#elif defined(INTERFACES_MAP)
		// map is sorted, can use find
		static std::map<Bytes, Interface&> _interfaces;           // All active interfaces
#elif defined(INTERFACES_POOL)
		// Fixed pool - declared in private section above as _interfaces_pool
#endif
#if defined(DESTINATIONS_SET)
		static std::set<Destination> _destinations;           // All active destinations
#elif defined(DESTINATIONS_MAP)
		static std::map<Bytes, Destination> _destinations;           // All active destinations
#elif defined(DESTINATIONS_POOL)
		// Fixed pool - declarations below
#endif
		// CBA TODO: Reconsider using std::set for enforcing uniqueness. Maybe consider std::map keyed on hash instead
		static std::set<Link> _pending_links;           // Links that are being established
		static std::set<Link> _active_links;           // Links that are active
		static std::set<Bytes> _packet_hashlist;           // A list of packet hashes for duplicate detection
		static std::list<PacketReceipt> _receipts;           // Receipts of all outgoing packets for proof processing

		// TODO: "destination_table" should really be renamed to "path_table"
		// Notes on memory usage: 1 megabyte of memory can store approximately
		// 55.100 path table entries or approximately 22.300 link table entries.

		static std::map<Bytes, AnnounceEntry> _announce_table;           // A table for storing announces currently waiting to be retransmitted
		static std::map<Bytes, DestinationEntry> _destination_table;           // A lookup table containing the next hop to a given destination
		static std::map<Bytes, ReverseEntry> _reverse_table;           // A lookup table for storing packet hashes used to return proofs and replies
		static std::map<Bytes, LinkEntry> _link_table;           // A lookup table containing hops for links
		static std::map<Bytes, AnnounceEntry> _held_announces;           // A table containing temporarily held announce-table entries
		//static std::set<HAnnounceHandler> _announce_handlers;  // Replaced by fixed array
		static std::map<Bytes, TunnelEntry> _tunnels;           // A table storing tunnels to other transport instances
		static std::map<Bytes, RateEntry> _announce_rate_table;           // A table for keeping track of announce rates
		static std::map<Bytes, double> _path_requests;           // A table for storing path request timestamps

		static std::map<Bytes, PathRequestEntry> _discovery_path_requests;       // A table for keeping track of path requests on behalf of other nodes
		static std::set<Bytes> _discovery_pr_tags;       // A table for keeping track of tagged path requests

		// Transport control destinations are used
		// for control purposes like path requests
		//static std::set<Destination> _control_destinations;  // Replaced by fixed array
		//static std::set<Bytes> _control_hashes;  // Replaced by fixed array

		// Interfaces for communicating with
		// local clients connected to a shared
		// Reticulum instance
		//static std::set<Interface> _local_client_interfaces;
		//static std::set<std::reference_wrapper<const Interface>, std::less<const Interface>> _local_client_interfaces;  // Replaced by fixed array

		static std::map<Bytes, const Interface&> _pending_local_path_requests;

		// CBA
		static std::map<Bytes, PacketEntry> _packet_table;           // A lookup table containing announce packets for known paths

		//z _local_client_rssi_cache    = []
		//z _local_client_snr_cache     = []
		static uint16_t _LOCAL_CLIENT_CACHE_MAXSIZE;

		static double _start_time;
		static bool _jobs_locked;
		static bool _jobs_running;
		static float _job_interval;
		static double _jobs_last_run;
		static double _links_last_checked;
		static float _links_check_interval;
		static double _receipts_last_checked;
		static float _receipts_check_interval;
		static double _announces_last_checked;
		static float _announces_check_interval;
		static double _tables_last_culled;
		static float _tables_cull_interval;
		static bool _saving_path_table;
		static uint16_t _hashlist_maxsize;
		static uint16_t _max_pr_tags;

		// CBA
		static uint16_t _path_table_maxsize;
		static uint16_t _path_table_maxpersist;
		static bool _probe_destination_enabled;
		static double _last_saved;
		static float _save_interval;
		static uint32_t _destination_table_crc;

		static Reticulum _owner;
		static Identity _identity;

		// CBA
		static Callbacks _callbacks;

		// CBA Stats
		static uint32_t _packets_sent;
		static uint32_t _packets_received;
		static uint32_t _destinations_added;
		static size_t _last_memory;
		static size_t _last_flash;
	};

	template <typename M, typename S> 
	void MapToValues(const M& m, S& s) {
		for (typename M::const_iterator it = m.begin(); it != m.end(); ++it) {
			s.insert(it->second);
		}
	}

	template <typename M, typename S> 
	void MapToPairs(const M& m, S& s) {
		for (typename M::const_iterator it = m.begin(); it != m.end(); ++it) {
			s.push_back(*it);
		}
	}
}
