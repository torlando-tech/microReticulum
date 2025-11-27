#include "SegmentAccumulator.h"
#include "Resource.h"

using namespace RNS;
using namespace RNS::Utilities;

SegmentAccumulator::SegmentAccumulator(AccumulatedCallback callback)
	: _accumulated_callback(callback)
{
}

void SegmentAccumulator::set_accumulated_callback(AccumulatedCallback callback) {
	_accumulated_callback = callback;
}

void SegmentAccumulator::set_segment_callback(SegmentCallback callback) {
	_segment_callback = callback;
}

bool SegmentAccumulator::segment_completed(const Resource& resource) {
	// Check if this is a multi-segment resource
	if (!resource.is_segmented()) {
		// Single-segment resource - caller should handle normally
		return false;
	}

	int segment_index = resource.segment_index();
	int total_segments = resource.total_segments();
	Bytes original_hash = resource.original_hash();

	// Use resource hash as fallback if original_hash not set
	if (!original_hash) {
		original_hash = resource.hash();
		DEBUG("SegmentAccumulator: No original_hash, using resource hash as key");
	}

	DEBUGF("SegmentAccumulator: Received segment %d/%d for %s (%zu bytes)",
		segment_index, total_segments,
		original_hash.toHex().substr(0, 16).c_str(),
		resource.data().size());

	double now = OS::time();

	// Create or get pending transfer
	auto it = _pending.find(original_hash);
	if (it == _pending.end()) {
		// New transfer - initialize
		PendingTransfer transfer;
		transfer.original_hash = original_hash;
		transfer.total_segments = total_segments;
		transfer.received_count = 0;
		transfer.segments.resize(total_segments);
		transfer.started_at = now;
		transfer.last_activity = now;

		// Initialize segment slots
		for (int i = 0; i < total_segments; i++) {
			transfer.segments[i].segment_index = i + 1;
			transfer.segments[i].received = false;
		}

		_pending[original_hash] = std::move(transfer);
		it = _pending.find(original_hash);

		INFOF("SegmentAccumulator: Started tracking %d-segment transfer for %s",
			total_segments, original_hash.toHex().substr(0, 16).c_str());
	}

	PendingTransfer& transfer = it->second;
	transfer.last_activity = now;

	// Validate segment index
	if (segment_index < 1 || segment_index > transfer.total_segments) {
		WARNINGF("SegmentAccumulator: Invalid segment_index %d (expected 1-%d)",
			segment_index, transfer.total_segments);
		return true;  // We handled it (by rejecting)
	}

	// Store segment data (segment_index is 1-based)
	int idx = segment_index - 1;
	if (!transfer.segments[idx].received) {
		transfer.segments[idx].data = resource.data();
		transfer.segments[idx].data_size = resource.data().size();
		transfer.segments[idx].received = true;
		transfer.received_count++;

		DEBUGF("SegmentAccumulator: Stored segment %d, %d/%d received",
			segment_index, transfer.received_count, transfer.total_segments);

		// Fire per-segment callback if set
		if (_segment_callback) {
			_segment_callback(segment_index, total_segments, original_hash);
		}
	} else {
		DEBUGF("SegmentAccumulator: Duplicate segment %d, ignoring", segment_index);
	}

	// Check if all segments received
	if (transfer.received_count == transfer.total_segments) {
		INFOF("SegmentAccumulator: All %d segments received for %s, assembling...",
			transfer.total_segments, original_hash.toHex().substr(0, 16).c_str());

		// Assemble complete data
		Bytes complete_data = assemble_segments(transfer);

		INFOF("SegmentAccumulator: Assembled %zu bytes from %d segments",
			complete_data.size(), transfer.total_segments);

		// Fire accumulated callback
		if (_accumulated_callback) {
			_accumulated_callback(complete_data, original_hash);
		}

		// Cleanup
		_pending.erase(it);
	}

	return true;  // We handled this multi-segment resource
}

Bytes SegmentAccumulator::assemble_segments(const PendingTransfer& transfer) {
	// Calculate total size
	size_t total_size = 0;
	for (const auto& seg : transfer.segments) {
		total_size += seg.data_size;
	}

	// Concatenate in order
	Bytes result;
	result.reserve(total_size);

	for (int i = 0; i < transfer.total_segments; i++) {
		const SegmentInfo& seg = transfer.segments[i];
		if (!seg.received) {
			ERRORF("SegmentAccumulator: Missing segment %d during assembly!", i + 1);
			continue;
		}
		result += seg.data;
	}

	return result;
}

void SegmentAccumulator::check_timeouts(double timeout_seconds) {
	double now = OS::time();
	std::vector<Bytes> to_remove;

	for (const auto& pair : _pending) {
		const PendingTransfer& transfer = pair.second;
		double inactive_time = now - transfer.last_activity;

		if (inactive_time > timeout_seconds) {
			WARNINGF("SegmentAccumulator: Transfer %s timed out (%.1fs inactive, %d/%d segments)",
				transfer.original_hash.toHex().substr(0, 16).c_str(),
				inactive_time, transfer.received_count, transfer.total_segments);
			to_remove.push_back(pair.first);
		}
	}

	for (const Bytes& hash : to_remove) {
		_pending.erase(hash);
	}
}

void SegmentAccumulator::cleanup(const Bytes& original_hash) {
	auto it = _pending.find(original_hash);
	if (it != _pending.end()) {
		DEBUGF("SegmentAccumulator: Cleaning up transfer %s (%d/%d segments received)",
			original_hash.toHex().substr(0, 16).c_str(),
			it->second.received_count, it->second.total_segments);
		_pending.erase(it);
	}
}

bool SegmentAccumulator::has_pending(const Bytes& original_hash) const {
	return _pending.find(original_hash) != _pending.end();
}

size_t SegmentAccumulator::pending_count() const {
	return _pending.size();
}
