#include "PropagationNodeManager.h"
#include "../Log.h"
#include "../Utilities/OS.h"

#include <MsgPack.h>
#include <algorithm>

using namespace LXMF;
using namespace RNS;

PropagationNodeManager::PropagationNodeManager()
	: AnnounceHandler("lxmf.propagation")
{
	INFO("PropagationNodeManager: initialized with aspect filter 'lxmf.propagation'");
}

void PropagationNodeManager::received_announce(
	const Bytes& destination_hash,
	const Identity& announced_identity,
	const Bytes& app_data
) {
	std::string hash_str = destination_hash.toHex().substr(0, 16);
	TRACE("PropagationNodeManager::received_announce from " + hash_str + "...");

	if (!app_data || app_data.size() == 0) {
		WARNING("PropagationNodeManager: Received announce with empty app_data");
		return;
	}

	PropagationNodeInfo info = parse_announce_data(app_data);
	if (info.node_hash.size() == 0 && info.name.empty()) {
		WARNING("PropagationNodeManager: Failed to parse announce app_data");
		return;
	}

	info.node_hash = destination_hash;
	info.last_seen = Utilities::OS::time();

	// Get hop count from Transport
	info.hops = Transport::hops_to(destination_hash);

	// Check if this is an update to an existing node
	bool is_update = _nodes.find(destination_hash) != _nodes.end();

	// Store/update node
	_nodes[destination_hash] = info;

	std::string action = is_update ? "Updated" : "Discovered";
	if (info.enabled) {
		INFO("PropagationNodeManager: " + action +
		     " propagation node '" + info.name + "' at " +
		     destination_hash.toHex().substr(0, 16) + "... (" +
		     std::to_string(info.hops) + " hops)");
	} else {
		INFO("PropagationNodeManager: Node " + destination_hash.toHex().substr(0, 16) +
		     "... reports propagation disabled");
	}

	// Notify listeners
	if (_update_callback) {
		_update_callback();
	}
}

PropagationNodeInfo PropagationNodeManager::parse_announce_data(const Bytes& app_data) {
	PropagationNodeInfo info;

	try {
		MsgPack::Unpacker unpacker;
		unpacker.feed(app_data.data(), app_data.size());

		// Expect array with 7 elements
		MsgPack::arr_size_t arr_size;
		unpacker.deserialize(arr_size);
		if (arr_size.size() < 7) {
			WARNING("PropagationNodeManager: Invalid app_data array size: " + std::to_string(arr_size.size()));
			return {};
		}

		// [0] Legacy flag (bool) - skip
		bool legacy_flag;
		unpacker.deserialize(legacy_flag);

		// [1] Node timebase (int64)
		int64_t timebase;
		unpacker.deserialize(timebase);
		info.timebase = static_cast<double>(timebase);

		// [2] Propagation enabled (bool)
		unpacker.deserialize(info.enabled);

		// [3] Per-transfer limit (int)
		int64_t transfer_limit;
		unpacker.deserialize(transfer_limit);
		info.transfer_limit = static_cast<uint32_t>(transfer_limit);

		// [4] Per-sync limit (int)
		int64_t sync_limit;
		unpacker.deserialize(sync_limit);
		info.sync_limit = static_cast<uint32_t>(sync_limit);

		// [5] Stamp costs array: [cost, flexibility, peering_cost]
		MsgPack::arr_size_t costs_size;
		unpacker.deserialize(costs_size);
		if (costs_size.size() >= 3) {
			int64_t cost, flexibility, peering;
			unpacker.deserialize(cost);
			unpacker.deserialize(flexibility);
			unpacker.deserialize(peering);
			info.stamp_cost = static_cast<uint8_t>(cost);
			info.stamp_flexibility = static_cast<uint8_t>(flexibility);
			info.peering_cost = static_cast<uint8_t>(peering);
		}

		// [6] Metadata dict
		MsgPack::map_size_t map_size;
		unpacker.deserialize(map_size);
		for (size_t i = 0; i < map_size.size(); i++) {
			int64_t key;
			unpacker.deserialize(key);

			if (key == PN_META_NAME) {
				// Name is a binary/string
				MsgPack::bin_t<uint8_t> name_bin;
				unpacker.deserialize(name_bin);
				info.name = std::string(name_bin.begin(), name_bin.end());
			} else {
				// Skip other metadata fields by reading and discarding
				// Try to read as binary first (most common), fall back to int
				try {
					MsgPack::bin_t<uint8_t> skip_bin;
					unpacker.deserialize(skip_bin);
				} catch (...) {
					try {
						int64_t skip_int;
						unpacker.deserialize(skip_int);
					} catch (...) {
						// Give up - might be complex type
					}
				}
			}
		}

		// Default name if none provided
		if (info.name.empty()) {
			info.name = "Propagation Node";
		}

		// Mark as valid by setting a dummy value that will be overwritten
		info.hops = 0;
		return info;

	} catch (const std::exception& e) {
		WARNING("PropagationNodeManager: Exception parsing app_data: " + std::string(e.what()));
		return {};
	}
}

std::vector<PropagationNodeInfo> PropagationNodeManager::get_nodes() const {
	std::vector<PropagationNodeInfo> result;
	result.reserve(_nodes.size());

	for (const auto& pair : _nodes) {
		result.push_back(pair.second);
	}

	// Sort by hops (closest first), then by last_seen (most recent first)
	std::sort(result.begin(), result.end(), [](const PropagationNodeInfo& a, const PropagationNodeInfo& b) {
		if (a.hops != b.hops) {
			return a.hops < b.hops;
		}
		return a.last_seen > b.last_seen;
	});

	return result;
}

PropagationNodeInfo PropagationNodeManager::get_node(const Bytes& hash) const {
	auto it = _nodes.find(hash);
	if (it != _nodes.end()) {
		return it->second;
	}
	return {};
}

bool PropagationNodeManager::has_node(const Bytes& hash) const {
	return _nodes.find(hash) != _nodes.end();
}

void PropagationNodeManager::set_selected_node(const Bytes& hash) {
	if (hash.size() == 0) {
		_selected_node = {};
		INFO("PropagationNodeManager: Cleared manual node selection");
		return;
	}

	if (!has_node(hash)) {
		WARNING("PropagationNodeManager: Cannot select unknown node " + hash.toHex().substr(0, 16));
		return;
	}

	_selected_node = hash;
	PropagationNodeInfo node = get_node(hash);
	INFO("PropagationNodeManager: Selected node '" + node.name + "' (" +
	     hash.toHex().substr(0, 16) + "...)");
}

Bytes PropagationNodeManager::get_best_node() const {
	PropagationNodeInfo best;
	best.hops = 0xFF;
	best.last_seen = 0;

	for (const auto& pair : _nodes) {
		const PropagationNodeInfo& node = pair.second;

		// Skip disabled nodes
		if (!node.enabled) {
			continue;
		}

		// Check if this node is better
		bool is_better = false;
		if (node.hops < best.hops) {
			is_better = true;
		} else if (node.hops == best.hops && node.last_seen > best.last_seen) {
			is_better = true;
		}

		if (is_better) {
			best = node;
		}
	}

	if (best.node_hash.size() > 0) {
		TRACE("PropagationNodeManager: Best node is '" + best.name + "' (" +
		      std::to_string(best.hops) + " hops)");
	}

	return best.node_hash;
}

Bytes PropagationNodeManager::get_effective_node() const {
	if (_selected_node.size() > 0) {
		// Verify selected node is still valid
		auto it = _nodes.find(_selected_node);
		if (it != _nodes.end() && it->second.enabled) {
			return _selected_node;
		}
	}

	// Fall back to auto-selection
	return get_best_node();
}

void PropagationNodeManager::clean_stale_nodes() {
	double now = Utilities::OS::time();
	std::vector<Bytes> to_remove;

	for (const auto& pair : _nodes) {
		if (now - pair.second.last_seen > NODE_STALE_TIMEOUT) {
			to_remove.push_back(pair.first);
		}
	}

	for (const Bytes& hash : to_remove) {
		INFO("PropagationNodeManager: Removing stale node " + hash.toHex().substr(0, 16) + "...");
		_nodes.erase(hash);
	}

	if (!to_remove.empty() && _update_callback) {
		_update_callback();
	}
}
