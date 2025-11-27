#pragma once

#include "Bytes.h"
#include "Log.h"
#include "Utilities/OS.h"

#include <map>
#include <vector>
#include <functional>

namespace RNS {

	// Forward declaration to avoid circular includes
	class Resource;

	/**
	 * SegmentAccumulator collects multi-segment resources and fires a single callback
	 * when all segments have been received.
	 *
	 * Python RNS splits large resources (>MAX_EFFICIENT_SIZE, ~1MB) into multiple segments.
	 * Each segment is transferred as a separate Resource with its own hash/proof.
	 * Segments share the same original_hash and have segment_index 1..total_segments.
	 *
	 * This class:
	 * - Tracks incoming segments by original_hash
	 * - Stores segment data until all segments arrive
	 * - Fires the accumulated callback once with the complete concatenated data
	 * - Handles timeout cleanup for stale transfers
	 */
	class SegmentAccumulator {

	public:
		// Callback fired when all segments are received
		// Parameters: complete_data, original_hash
		using AccumulatedCallback = std::function<void(const Bytes& data, const Bytes& original_hash)>;

		// Callback for individual segment completion (optional, for progress tracking)
		using SegmentCallback = std::function<void(int segment_index, int total_segments, const Bytes& original_hash)>;

	public:
		SegmentAccumulator() = default;
		explicit SegmentAccumulator(AccumulatedCallback callback);

		/**
		 * Set the callback for completed multi-segment resources.
		 */
		void set_accumulated_callback(AccumulatedCallback callback);

		/**
		 * Set optional per-segment progress callback.
		 */
		void set_segment_callback(SegmentCallback callback);

		/**
		 * Called when a Resource segment completes.
		 *
		 * @param resource The completed resource (may be single or multi-segment)
		 * @return true if this was a multi-segment resource that was handled,
		 *         false if it was a single-segment resource (caller should invoke normal callback)
		 */
		bool segment_completed(const Resource& resource);

		/**
		 * Check for timed-out transfers and clean them up.
		 * Should be called periodically (e.g., from watchdog).
		 *
		 * @param timeout_seconds Maximum time since last activity before cleanup
		 */
		void check_timeouts(double timeout_seconds = 600.0);

		/**
		 * Manually cleanup a specific transfer.
		 */
		void cleanup(const Bytes& original_hash);

		/**
		 * Check if a transfer is in progress for the given original_hash.
		 */
		bool has_pending(const Bytes& original_hash) const;

		/**
		 * Get the number of pending (incomplete) transfers.
		 */
		size_t pending_count() const;

	private:
		struct SegmentInfo {
			int segment_index = 0;
			size_t data_size = 0;
			Bytes data;
			bool received = false;
		};

		struct PendingTransfer {
			Bytes original_hash;
			int total_segments = 0;
			int received_count = 0;
			std::vector<SegmentInfo> segments;  // Indexed by segment_index - 1
			double started_at = 0.0;
			double last_activity = 0.0;
		};

		std::map<Bytes, PendingTransfer> _pending;
		AccumulatedCallback _accumulated_callback = nullptr;
		SegmentCallback _segment_callback = nullptr;

		/**
		 * Concatenate all segments in order and return complete data.
		 */
		Bytes assemble_segments(const PendingTransfer& transfer);
	};

}
