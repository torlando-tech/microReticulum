#include "Interface.h"

#include "Identity.h"
#include "Transport.h"
#include "Utilities/OS.h"

#include <algorithm>

using namespace RNS;
using namespace RNS::Type::Interface;

/*static*/ uint8_t Interface::DISCOVER_PATHS_FOR = MODE_ACCESS_POINT | MODE_GATEWAY;

void InterfaceImpl::handle_outgoing(const Bytes& data) {
	//TRACE("InterfaceImpl.handle_outgoing: data: " + data.toHex());
	TRACE("InterfaceImpl.handle_outgoing");
	_txb += data.size();
}

void InterfaceImpl::handle_incoming(const Bytes& data) {
	//TRACE("InterfaceImpl.handle_incoming: data: " + data.toHex());
	TRACE("InterfaceImpl.handle_incoming");
	_rxb += data.size();
	// Create temporary Interface encapsulating our own shared impl
	std::shared_ptr<InterfaceImpl> self = shared_from_this();
	Interface interface(self);
	// Pass data on to transport for handling
	Transport::inbound(data, interface);
}

void Interface::handle_incoming(const Bytes& data) {
	//TRACE("Interface.handle_incoming: data: " + data.toHex());
	TRACE("Interface.handle_incoming");
	assert(_impl);
/*
	_impl->_rxb += data.size();
	// Pass data on to transport for handling
	Transport::inbound(data, *this);
*/
	_impl->handle_incoming(data);
}

void Interface::process_announce_queue() {
	if (!_impl || _impl->_announce_queue.empty()) {
		return;
	}

	double now = Utilities::OS::time();

	// For MCU, use shorter lifetime (60 seconds vs 24 hours)
#ifdef ARDUINO
	static constexpr double MCU_ANNOUNCE_LIFE = 60.0;
#else
	static constexpr double MCU_ANNOUNCE_LIFE = Type::Reticulum::QUEUED_ANNOUNCE_LIFE;
#endif

	// Remove stale entries
	_impl->_announce_queue.remove_if([now](const AnnounceEntry& entry) {
		return now > (entry._time + MCU_ANNOUNCE_LIFE);
	});

	// Limit queue size for MCU (remove oldest if over limit)
#ifdef ARDUINO
	static constexpr size_t MCU_MAX_QUEUED_ANNOUNCES = 16;
	while (_impl->_announce_queue.size() > MCU_MAX_QUEUED_ANNOUNCES) {
		_impl->_announce_queue.pop_front();
	}
#endif

	// Check if we're allowed to send
	if (_impl->_announce_queue.empty() || now < _impl->_announce_allowed_at) {
		return;
	}

	// Find entry with minimum hops
	auto min_it = std::min_element(
		_impl->_announce_queue.begin(),
		_impl->_announce_queue.end(),
		[](const AnnounceEntry& a, const AnnounceEntry& b) {
			if (a._hops != b._hops) return a._hops < b._hops;
			return a._time < b._time;  // Oldest first for same hop count
		}
	);

	if (min_it != _impl->_announce_queue.end()) {
		// Calculate wait time for next announce
		uint32_t wait_time = 0;
		if (_impl->_bitrate > 0 && _impl->_announce_cap > 0) {
			uint32_t tx_time = (min_it->_raw.size() * 8) / _impl->_bitrate;
			wait_time = (tx_time / _impl->_announce_cap);
		}
		_impl->_announce_allowed_at = now + wait_time;

		// Transmit the announce
		send_outgoing(min_it->_raw);

		// Remove from queue
		_impl->_announce_queue.erase(min_it);
	}
}

/*
void ArduinoJson::convertFromJson(JsonVariantConst src, RNS::Interface& dst) {
	TRACE(">>> Deserializing Interface");
TRACE(">>> Interface pre: " + dst.debugString());
	if (!src.isNull()) {
		RNS::Bytes hash;
		hash.assignHex(src.as<const char*>());
		TRACE(">>> Querying Transport for Interface hash " + hash.toHex());
		// Query transport for matching interface
		dst = Transport::find_interface_from_hash(hash);
TRACE(">>> Interface post: " + dst.debugString());
	}
	else {
		dst = {RNS::Type::NONE};
TRACE(">>> Interface post: " + dst.debugString());
	}
}
*/
