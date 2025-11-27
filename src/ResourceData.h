#pragma once

#include "Resource.h"

#include "Interface.h"
#include "Packet.h"
#include "Destination.h"
#include "Link.h"
#include "Bytes.h"
#include "Type.h"
#include "Cryptography/Fernet.h"

#include <vector>
#include <set>

namespace RNS {

	class ResourceData {
	public:
		ResourceData(const Link& link) : _link(link) {}
		virtual ~ResourceData() {}

	private:
		// Core link reference
		Link _link;

		// Resource identification
		Bytes _hash;              // Resource hash (SHA256 of encrypted data + random_hash)
		Bytes _original_hash;     // Original hash for multi-segment tracking
		Bytes _random_hash;       // 4-byte random salt
		Bytes _request_id;        // Optional request ID for request/response

		// Data storage
		Bytes _data;              // The resource data (assembled)
		Bytes _original_data;     // Full original data (for segmented sends)
		Bytes _metadata;          // Optional metadata
		size_t _metadata_size = 0;
		bool _has_metadata = false;

		// Size tracking
		size_t _size = 0;              // Transfer size (encrypted payload)
		size_t _total_size = 0;        // Original data size
		size_t _uncompressed_size = 0; // Size before compression

		// Status and flags
		Type::Resource::status _status = Type::Resource::NONE;
		uint8_t _flags = 0;
		bool _encrypted = true;
		bool _compressed = false;
		bool _initiator = false;       // True if we're sending, false if receiving
		bool _is_response = false;

		// Segmentation
		size_t _sdu = 0;              // Service Data Unit size
		size_t _total_parts = 0;
		size_t _received_count = 0;
		size_t _sent_parts = 0;
		size_t _outstanding_parts = 0;
		std::vector<Bytes> _parts;     // Individual parts for assembly

		// Multi-segment resources
		int _segment_index = 1;
		int _total_segments = 1;
		bool _split = false;

		// Hashmap management
		Bytes _hashmap_raw;           // Raw hashmap from advertisement
		std::vector<Bytes> _hashmap;  // Per-part map hashes (4 bytes each)
		size_t _hashmap_height = 0;   // How many hashes we know
		size_t _initial_hashmap_count = 0; // Hashes in initial segment (segment 0)
		bool _waiting_for_hmu = false;
		bool _receiving_part = false;
		int _consecutive_completed_height = -1;
		std::set<Bytes> _req_hashlist; // Track request packet hashes

		// Window management (flow control)
		size_t _window = Type::Resource::WINDOW;
		size_t _window_max = Type::Resource::WINDOW_MAX_SLOW;
		size_t _window_min = Type::Resource::WINDOW_MIN;
		size_t _window_flexibility = Type::Resource::WINDOW_FLEXIBILITY;
		size_t _fast_rate_rounds = 0;
		size_t _very_slow_rate_rounds = 0;

		// Rate tracking
		double _rtt = 0.0;
		double _eifr = 0.0;            // Expected In-Flight Rate
		double _previous_eifr = 0.0;

		// Timing
		double _last_activity = 0.0;
		double _started_transferring = 0.0;
		double _adv_sent = 0.0;
		double _req_sent = 0.0;
		double _req_resp_rtt_rate = 0.0;

		// Retry management
		size_t _max_retries = Type::Resource::MAX_RETRIES;
		size_t _max_adv_retries = Type::Resource::MAX_ADV_RETRIES;
		size_t _retries_left = Type::Resource::MAX_RETRIES;
		size_t _adv_retries = 0;
		double _timeout_factor = 1.0;
		double _part_timeout_factor = Type::Resource::PART_TIMEOUT_FACTOR;
		double _sender_grace_time = Type::Resource::SENDER_GRACE_TIME;
		bool _hmu_retry_ok = false;
		bool _watchdog_lock = false;
		uint32_t _watchdog_job_id = 0;

		// Assembly state
		bool _assembly_lock = false;
		bool _preparing_next_segment = false;

		// Storage paths (for large resources)
		std::string _storagepath;
		std::string _meta_storagepath;

		// Progress tracking
		size_t _processed_parts = 0;
		size_t _grand_total_parts = 0;
		double _progress_total_parts = 0.0;

		// Callbacks
		Resource::Callbacks _callbacks;

	friend class Resource;
	};

}
