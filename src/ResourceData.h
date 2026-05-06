/*
 * Copyright (c) 2023 Chad Attermann
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#include "Resource.h"

#include "Interface.h"
#include "Packet.h"
#include "Destination.h"
#include "Bytes.h"
#include "Type.h"
#include "Cryptography/Fernet.h"

#include <vector>

namespace RNS {

	class ResourceData {
	public:
		ResourceData(const Link& link) : _link(link) {}
		virtual ~ResourceData() {}
	public:
		// Public so the Resource implementation file (and supporting
		// helpers like init_sender_state, ResourceAdvertisement
		// constructor) can access state without needing dozens of
		// friend declarations. ResourceData is itself a private impl
		// detail of Resource — it's only ever held by shared_ptr inside
		// the Resource handle.
		Link _link;
		Bytes _hash;
		Bytes _truncated_hash;
		Bytes _original_hash;
		Bytes _random_hash;
		Bytes _expected_proof;
		Bytes _request_id;
		Bytes _data;                   // assembled data (receiver)
		Bytes _hashmap;                // concatenated 4-byte map_hashes
		Type::Resource::status _status = Type::Resource::NONE;
		size_t _size = 0;              // transfer size (encrypted)
		size_t _total_size = 0;        // uncompressed size
		size_t _sdu = 0;               // segment data unit
		size_t _total_parts = 0;
		size_t _received_count = 0;
		size_t _outstanding_parts = 0;
		size_t _sent_parts = 0;
		bool _initiator = false;
		bool _encrypted = true;
		bool _compressed = false;
		bool _split = false;
		bool _has_metadata = false;
		bool _is_response = false;
		uint8_t _flags = 0;
		uint16_t _segment_index = 1;
		uint16_t _total_segments = 1;
		// Sender-side parts (already-encrypted bytes payload per part)
		std::vector<Bytes> _parts_sender;
		// Receiver-side parts (raw bytes, indexed by part number)
		std::vector<Bytes> _parts_recv;
		// Window state (receiver)
		size_t _window = 4;
		int64_t _consecutive_completed_height = -1;
		// Sender-side: parts already sent (indexed by part_index → bool)
		std::vector<bool> _sent;
		// Sender-side: receiver_min_consecutive_height for collision-guard window
		size_t _receiver_min_consecutive_height = 0;
		Resource::Callbacks _callbacks;

	friend class Resource;
	};

}
