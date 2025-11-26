#pragma once

#include "Destination.h"
#include "Packet.h"
#include "Link.h"
#include "Type.h"

#include <memory>
#include <cassert>

namespace RNS {

	class ResourceData;
	class Packet;
	class Destination;
	class Link;
	class Resource;

	class Resource {

	public:
		class Callbacks {
		public:
			// CBA std::function apparently not implemented in NRF52 framework
			//typedef std::function<void(const Resource& resource)> concluded;
			using concluded = void(*)(const Resource& resource);
			using progress = void(*)(const Resource& resource);
		public:
			concluded _concluded = nullptr;
			progress _progress = nullptr;
		friend class Resource;
		};

	public:
		Resource(Type::NoneConstructor none) {
			MEM("Resource NONE object created");
		}
		Resource(const Resource& resource) : _object(resource._object) {
			MEM("Resource object copy created");
		}
		//Resource(const Link& link = {Type::NONE});
		Resource(const Bytes& data, const Link& link, const Bytes& request_id, bool is_response, double timeout);
		Resource(const Bytes& data, const Link& link, bool advertise = true, bool auto_compress = true, Callbacks::concluded callback = nullptr, Callbacks::progress progress_callback = nullptr, double timeout = 0.0, int segment_index = 1, const Bytes& original_hash = {Type::NONE}, const Bytes& request_id = {Type::NONE}, bool is_response = false);
		virtual ~Resource(){
			MEM("Resource object destroyed");
		}

		Resource& operator = (const Resource& resource) {
			_object = resource._object;
			return *this;
		}
		operator bool() const {
			return _object.get() != nullptr;
		}
		bool operator < (const Resource& resource) const {
			return _object.get() < resource._object.get();
			//return _object->_hash < resource._object->_hash;
		}

	public:
		// Accept an incoming resource advertisement
		static Resource accept(const Packet& advertisement_packet,
			Callbacks::concluded callback = nullptr,
			Callbacks::progress progress_callback = nullptr,
			const Bytes& request_id = {Type::NONE});

	public:
		// Hashmap management
		void hashmap_update_packet(const Bytes& plaintext);
		void hashmap_update(int segment, const Bytes& hashmap_data);
		static Bytes get_map_hash(const Bytes& data, const Bytes& random_hash);

		// Transfer control
		void request_next();
		void receive_part(const Packet& packet);
		void assemble();
		void prove();
		void validate_proof(const Bytes& proof_data);
		void cancel();
//p def set_callback(self, callback):
//p def progress_callback(self, callback):
		float get_progress() const;
//p def get_transfer_size(self):
//p def get_data_size(self):
//p def get_parts(self):
//p def get_segments(self):
//p def get_hash(self):
//p def is_compressed(self):
		void set_concluded_callback(Callbacks::concluded callback);
		void set_progress_callback(Callbacks::progress callback);

		std::string toString() const;

		// getters
		const Bytes& hash() const;
		const Bytes& request_id() const;
		const Bytes& data() const;
		const Type::Resource::status status() const;
		const size_t size() const;
		const size_t total_size() const;

		// setters

	protected:
		std::shared_ptr<ResourceData> _object;

	};


	class ResourceAdvertisement {
	public:
		// Parsed advertisement data
		size_t transfer_size = 0;      // "t" - encrypted transfer size
		size_t total_size = 0;         // "d" - original data size
		size_t total_parts = 0;        // "n" - number of parts
		Bytes resource_hash;           // "h" - resource hash (32 bytes)
		Bytes random_hash;             // "r" - random hash (4 bytes)
		Bytes original_hash;           // "o" - original hash for multi-segment (optional)
		int segment_index = 1;         // "i" - segment index
		int total_segments = 1;        // "l" - total segments
		Bytes request_id;              // "q" - request ID (optional)
		uint8_t flags = 0;             // "f" - flags byte
		Bytes hashmap;                 // "m" - hashmap data

		// Parsed flags
		bool is_encrypted = true;
		bool is_compressed = false;
		bool is_split = false;
		bool is_request = false;
		bool is_response = false;
		bool has_metadata = false;

		// Flag bit positions (from Python RNS)
		static constexpr uint8_t FLAG_ENCRYPTED    = 0x01;  // bit 0
		static constexpr uint8_t FLAG_COMPRESSED   = 0x02;  // bit 1
		static constexpr uint8_t FLAG_SPLIT        = 0x04;  // bit 2
		static constexpr uint8_t FLAG_IS_REQUEST   = 0x08;  // bit 3
		static constexpr uint8_t FLAG_IS_RESPONSE  = 0x10;  // bit 4
		static constexpr uint8_t FLAG_HAS_METADATA = 0x20;  // bit 5

	public:
		ResourceAdvertisement() = default;

		// Parse an advertisement from msgpack data
		static bool unpack(const Bytes& data, ResourceAdvertisement& adv);

		// Create a packed advertisement
		static Bytes pack(const ResourceAdvertisement& adv);

		// Parse flags byte
		void parse_flags() {
			is_encrypted = (flags & FLAG_ENCRYPTED) != 0;
			is_compressed = (flags & FLAG_COMPRESSED) != 0;
			is_split = (flags & FLAG_SPLIT) != 0;
			is_request = (flags & FLAG_IS_REQUEST) != 0;
			is_response = (flags & FLAG_IS_RESPONSE) != 0;
			has_metadata = (flags & FLAG_HAS_METADATA) != 0;
		}

		// Build flags byte from individual flags
		void build_flags() {
			flags = 0;
			if (is_encrypted) flags |= FLAG_ENCRYPTED;
			if (is_compressed) flags |= FLAG_COMPRESSED;
			if (is_split) flags |= FLAG_SPLIT;
			if (is_request) flags |= FLAG_IS_REQUEST;
			if (is_response) flags |= FLAG_IS_RESPONSE;
			if (has_metadata) flags |= FLAG_HAS_METADATA;
		}
	};

}
