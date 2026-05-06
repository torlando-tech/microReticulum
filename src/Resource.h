/*
 * Copyright (c) 2023 Chad Attermann
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 */

#pragma once

#include "Bytes.h"
#include "Destination.h"
#include "Type.h"

#include <memory>
#include <cassert>

namespace RNS {

	class ResourceData;
	class Packet;
	class Destination;
	class Link;
	class Resource;

	// Resource transfer protocol — port of python RNS.Resource. Handles
	// sequencing + chunking of arbitrary payloads larger than a single
	// link MDU. Receiver-side: accept(adv_packet) → request_next →
	// receive_part repeatedly → assemble → prove. Sender-side:
	// constructor(data, link) → advertise → request → send_parts →
	// validate_proof.
	class Resource {

	public:
		// Constants — match python Resource. Static constants here so
		// they're usable in type signatures (e.g. MAPHASH_LEN as buffer
		// size).
		static constexpr uint8_t  MAPHASH_LEN = 4;
		static constexpr uint8_t  RANDOM_HASH_SIZE = 4;
		static constexpr uint8_t  HASHMAP_IS_NOT_EXHAUSTED = 0x00;
		static constexpr uint8_t  HASHMAP_IS_EXHAUSTED = 0xFF;
		// Conservative default windows (matches python Resource.WINDOW).
		static constexpr uint8_t  WINDOW = 4;
		static constexpr uint8_t  WINDOW_MIN = 2;
		static constexpr uint8_t  WINDOW_MAX = 75;

		class Callbacks {
		public:
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
		// Sender-side: construct with data + link, packs into parts, sends advertisement.
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
		}

	public:
		// Static factory: receiver constructs an inbound Resource from a
		// RESOURCE_ADV packet (with already-decrypted plaintext on
		// `packet.plaintext()`). Initializes RX state and immediately
		// dispatches the first request_next().
		static Resource accept(const Packet& advertisement_packet,
		                       Callbacks::concluded callback = nullptr,
		                       Callbacks::progress progress_callback = nullptr,
		                       const Bytes& request_id = {Type::NONE});

		// Sender-side: emit RESOURCE_ADV packet on the link.
		void advertise();

		// Receiver-side: send RESOURCE_REQ asking for the next batch of parts.
		void request_next();

		// Receiver-side: handle an inbound RESOURCE packet.
		void receive_part(const Packet& packet);

		// Sender-side: handle an inbound RESOURCE_REQ packet, sending requested parts.
		void request(const Bytes& request_data);

		// Receiver-side: assemble parts → emit RESOURCE_PRF.
		void assemble();

		// Receiver-side: send a proof packet for the assembled resource.
		void prove();

		// Sender-side: handle an inbound RESOURCE_PRF packet.
		void validate_proof(const Bytes& proof_data);

		void cancel();

		// Map-hash function: SHA256(data || random_hash) truncated to
		// MAPHASH_LEN bytes. Used to identify parts in the hashmap.
		const Bytes get_map_hash(const Bytes& data) const;

		void set_concluded_callback(Callbacks::concluded callback);
		void set_progress_callback(Callbacks::progress callback);

		std::string toString() const;
		float get_progress() const;

		// getters
		const Bytes& hash() const;
		const Bytes& request_id() const;
		const Bytes& data() const;
		const Type::Resource::status status() const;
		const size_t size() const;
		const size_t total_size() const;
		const Link& link() const;

	protected:
		std::shared_ptr<ResourceData> _object;
	};


	// ResourceAdvertisement — wire-format helper for the RESOURCE_ADV
	// packet payload. Single-segment, single-window-of-hashmap payloads.
	// Wire shape (msgpack dict[str, any]) matches python's
	// ResourceAdvertisement.pack/unpack.
	class ResourceAdvertisement {
	public:
		ResourceAdvertisement() {}

		// Sender-side: build advertisement state from a Resource instance.
		ResourceAdvertisement(const Resource& resource);

		// Encode to msgpack bytes ready to put on a Packet.
		Bytes pack() const;

		// Decode from msgpack bytes (the plaintext of a RESOURCE_ADV packet).
		static ResourceAdvertisement unpack(const Bytes& packed);

		// Field accessors (single-letter names match python).
		uint32_t t = 0;        // Transfer size (bytes)
		uint32_t d = 0;        // Total uncompressed data size
		uint32_t n = 0;        // Number of parts
		Bytes h;               // Resource hash (32 bytes)
		Bytes r;               // Random hash (4 bytes)
		Bytes o;               // First-segment hash
		Bytes m;               // Hashmap bytes (n × MAPHASH_LEN)
		uint8_t f = 0;         // Flags byte
		uint16_t i = 1;        // Segment index
		uint16_t l = 1;        // Total segments
		Bytes q;               // Request ID (empty = nil)
	};

}
