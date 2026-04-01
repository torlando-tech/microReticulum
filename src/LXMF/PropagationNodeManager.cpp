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
	(void)announced_identity;
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
	Bytes effective_before = get_effective_node();
	PropagationNodeInfo effective_before_info = get_node(effective_before);

	// Get hop count from Transport
	info.hops = Transport::hops_to(destination_hash);

	// Check if this is an update to an existing node
	PropagationNodeSlot* existing_slot = find_node_slot(destination_hash);
	bool is_update = (existing_slot != nullptr);

	// Store/update node
	PropagationNodeSlot* slot = existing_slot;
	if (!slot) {
		slot = find_empty_node_slot();
		if (!slot) {
			PropagationNodeSlot* worst_slot = find_worst_node_slot();
			if (!worst_slot || !is_better_candidate(info, worst_slot->info)) {
				WARNING("PropagationNodeManager: Pool full, dropping lower-priority node " + hash_str);
				return;
			}

			INFO("PropagationNodeManager: Evicting node " +
			     worst_slot->node_hash.toHex().substr(0, 16) + "... for better candidate " + hash_str);
			slot = worst_slot;
		}
	}

	slot->in_use = true;
	slot->node_hash = destination_hash;
	slot->info = info;

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

	update_effective_node(false);

	// Notify listeners
	Bytes effective_after = get_effective_node();
	PropagationNodeInfo effective_after_info = get_node(effective_after);
	bool effective_changed = (effective_after != effective_before);
	bool effective_info_changed =
		(effective_after == effective_before) &&
		(
			effective_after_info.stamp_cost != effective_before_info.stamp_cost ||
			effective_after_info.enabled != effective_before_info.enabled ||
			effective_after_info.hops != effective_before_info.hops
		);

	if (_update_callback && (effective_changed || effective_info_changed)) {
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
				// Python packs name as bytes (bin type via str.encode("utf-8"))
				MsgPack::bin_t<uint8_t> name_bin;
				unpacker.deserialize(name_bin);
				if (!name_bin.empty()) {
					info.name = std::string(name_bin.begin(), name_bin.end());
				}
				// On type mismatch (e.g. str type from non-standard node),
				// type_error() silently advances curr_index past the element.
				// Name stays empty and will use default fallback below.
			} else {
				// Skip unknown metadata value: deserialize as int64_t.
				// On type mismatch, type_error() increments curr_index by 1,
				// which correctly skips to the next element regardless of type.
				int64_t skip_val;
				unpacker.deserialize(skip_val);
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
	result.reserve(nodes_count());

	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (_nodes_pool[i].in_use) {
			result.push_back(_nodes_pool[i].info);
		}
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
	const PropagationNodeSlot* slot = find_node_slot(hash);
	if (slot) {
		return slot->info;
	}
	return {};
}

bool PropagationNodeManager::has_node(const Bytes& hash) const {
	return find_node_slot(hash) != nullptr;
}

void PropagationNodeManager::set_selected_node(const Bytes& hash) {
	Bytes effective_before = get_effective_node();

	if (hash.size() == 0) {
		_manual_selected_node = {};
		update_effective_node(true);
		INFO("PropagationNodeManager: Cleared manual node selection");
		if (_update_callback && get_effective_node() != effective_before) {
			_update_callback();
		}
		return;
	}

	if (!has_node(hash)) {
		WARNING("PropagationNodeManager: Cannot select unknown node " + hash.toHex().substr(0, 16));
		return;
	}

	_manual_selected_node = hash;
	update_effective_node(true);
	PropagationNodeInfo node = get_node(hash);
	INFO("PropagationNodeManager: Selected node '" + node.name + "' (" +
	     hash.toHex().substr(0, 16) + "...)");
	if (_update_callback && get_effective_node() != effective_before) {
		_update_callback();
	}
}

Bytes PropagationNodeManager::get_best_node() const {
	PropagationNodeInfo best;
	best.hops = 0xFF;
	best.last_seen = 0;

	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (!_nodes_pool[i].in_use) {
			continue;
		}
		const PropagationNodeInfo& node = _nodes_pool[i].info;

		if (!is_reachable_candidate(node)) {
			continue;
		}

		// Check if this node is better
		bool is_better = false;
		if (node.hops < best.hops) {
			is_better = true;
		} else if (node.hops == best.hops && node.last_seen > best.last_seen) {
			is_better = true;
		} else if (node.hops == best.hops && node.last_seen == best.last_seen && node.stamp_cost < best.stamp_cost) {
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
	if (_manual_selected_node.size() > 0) {
		const PropagationNodeSlot* slot = find_node_slot(_manual_selected_node);
		if (slot) {
			return _manual_selected_node;
		}
	}

	if (_effective_node.size() > 0) {
		const PropagationNodeSlot* slot = find_node_slot(_effective_node);
		if (slot) {
			return _effective_node;
		}
	}

	return get_best_node();
}

void PropagationNodeManager::clean_stale_nodes() {
	double now = Utilities::OS::time();
	bool removed_any = false;
	Bytes effective_before = get_effective_node();

	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (_nodes_pool[i].in_use &&
		    now - _nodes_pool[i].info.last_seen > NODE_STALE_TIMEOUT) {
			INFO("PropagationNodeManager: Removing stale node " +
			     _nodes_pool[i].node_hash.toHex().substr(0, 16) + "...");
			_nodes_pool[i].clear();
			removed_any = true;
		}
	}

	if (removed_any) {
		update_effective_node(true);
	}

	if (removed_any && _update_callback && get_effective_node() != effective_before) {
		_update_callback();
	}
}

PropagationNodeSlot* PropagationNodeManager::find_node_slot(const Bytes& hash) {
	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (_nodes_pool[i].in_use && _nodes_pool[i].node_hash == hash) {
			return &_nodes_pool[i];
		}
	}
	return nullptr;
}

const PropagationNodeSlot* PropagationNodeManager::find_node_slot(const Bytes& hash) const {
	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (_nodes_pool[i].in_use && _nodes_pool[i].node_hash == hash) {
			return &_nodes_pool[i];
		}
	}
	return nullptr;
}

PropagationNodeSlot* PropagationNodeManager::find_empty_node_slot() {
	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (!_nodes_pool[i].in_use) {
			return &_nodes_pool[i];
		}
	}
	return nullptr;
}

PropagationNodeSlot* PropagationNodeManager::find_worst_node_slot() {
	PropagationNodeSlot* worst = nullptr;

	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (!_nodes_pool[i].in_use) {
			continue;
		}
		if (!worst) {
			worst = &_nodes_pool[i];
			continue;
		}

		if (is_better_candidate(worst->info, _nodes_pool[i].info)) {
			worst = &_nodes_pool[i];
		}
	}

	return worst;
}

size_t PropagationNodeManager::nodes_count() const {
	size_t count = 0;
	for (size_t i = 0; i < MAX_PROPAGATION_NODES; ++i) {
		if (_nodes_pool[i].in_use) {
			++count;
		}
	}
	return count;
}

bool PropagationNodeManager::is_better_candidate(const PropagationNodeInfo& candidate, const PropagationNodeInfo& current) const {
	if (!candidate.enabled && current.enabled) {
		return false;
	}
	if (candidate.enabled && !current.enabled) {
		return true;
	}
	if (candidate.hops != current.hops) {
		return candidate.hops < current.hops;
	}
	if (candidate.last_seen != current.last_seen) {
		return candidate.last_seen > current.last_seen;
	}
	return candidate.stamp_cost < current.stamp_cost;
}

bool PropagationNodeManager::is_reachable_candidate(const PropagationNodeInfo& candidate) const {
	if (!candidate || !candidate.enabled) {
		return false;
	}

	if (!Transport::has_path(candidate.node_hash)) {
		return false;
	}

	Identity identity = Identity::recall(candidate.node_hash);
	return static_cast<bool>(identity);
}

bool PropagationNodeManager::should_switch_effective_node(
	const PropagationNodeInfo& current,
	const PropagationNodeInfo& candidate,
	bool force_reselect
) const {
	if (!candidate || !is_reachable_candidate(candidate)) {
		return false;
	}
	if (!current) {
		return true;
	}
	if (!is_reachable_candidate(current)) {
		return true;
	}
	if (force_reselect) {
		return is_better_candidate(candidate, current);
	}

	double now = Utilities::OS::time();
	double current_age = now - current.last_seen;
	if (current_age > EFFECTIVE_NODE_STICKY_WINDOW) {
		return is_better_candidate(candidate, current);
	}

	if (candidate.hops + EFFECTIVE_NODE_SWITCH_HOP_MARGIN <= current.hops) {
		return true;
	}

	return false;
}

void PropagationNodeManager::update_effective_node(bool force_reselect) {
	if (_manual_selected_node.size() > 0) {
		const PropagationNodeSlot* manual_slot = find_node_slot(_manual_selected_node);
		if (manual_slot) {
			_effective_node = _manual_selected_node;
			return;
		}
	}

	if (_effective_node.size() > 0) {
		const PropagationNodeSlot* current_slot = find_node_slot(_effective_node);
		if (current_slot) {
			Bytes best_hash = get_best_node();
			if (best_hash.size() == 0) {
				return;
			}

			const PropagationNodeSlot* best_slot = find_node_slot(best_hash);
			if (!best_slot) {
				return;
			}

			if (!should_switch_effective_node(current_slot->info, best_slot->info, force_reselect)) {
				return;
			}
		}
	}

	Bytes best_hash = get_best_node();
	if (best_hash.size() > 0) {
		_effective_node = best_hash;
	}
}
