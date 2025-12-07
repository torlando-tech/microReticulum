#include "LXMRouter.h"
#include "../Packet.h"
#include "../Resource.h"
#include "../Transport.h"
#include "../Log.h"
#include "../Utilities/OS.h"

#include <MsgPack.h>

namespace RNS { namespace LXMF {

// Static instance for C-style callbacks
/*static*/ LXMRouter* LXMRouter::_instance = nullptr;

//=============================================================================
// Constructor / Destructor
//=============================================================================

LXMRouter::LXMRouter() : _state(std::make_unique<State>()) {
	DEBUG("LXMRouter created");
	_instance = this;
}

LXMRouter::~LXMRouter() {
	DEBUG("LXMRouter destroyed");
	if (_instance == this) {
		_instance = nullptr;
	}
}

//=============================================================================
// Delivery Destination Management
//=============================================================================

Destination LXMRouter::register_delivery_identity(
	const Identity& identity,
	const std::string& display_name,
	uint8_t stamp_cost
) {
	if (!identity) {
		ERROR("LXMRouter::register_delivery_identity: Invalid identity");
		return {Type::NONE};
	}

	_state->_delivery_identity = identity;
	_state->_display_name = display_name;
	_state->_stamp_cost = stamp_cost;

	// Create delivery destination (IN, SINGLE, "lxmf", "delivery")
	_state->_delivery_destination = Destination(
		identity,
		Type::Destination::IN,
		Type::Destination::SINGLE,
		APP_NAME,
		ASPECT_DELIVERY
	);

	if (!_state->_delivery_destination) {
		ERROR("LXMRouter::register_delivery_identity: Failed to create destination");
		return {Type::NONE};
	}

	// Enable accepting incoming links (required for DIRECT delivery)
	_state->_delivery_destination.accepts_links(true);
	_state->_delivery_destination.set_proof_strategy(Type::Destination::PROVE_ALL);

	// Set up callbacks
	_state->_delivery_destination.set_packet_callback(delivery_packet_callback);
	_state->_delivery_destination.set_link_established_callback(delivery_link_established_callback);

	INFOF("LXMRouter: Registered delivery destination %s",
		  _state->_delivery_destination.hash().toHex().c_str());

	return _state->_delivery_destination;
}

void LXMRouter::register_delivery_callback(Callbacks::delivery callback) {
	_state->_delivery_callback = callback;
}

const Destination& LXMRouter::delivery_destination() const {
	return _state->_delivery_destination;
}

const Identity& LXMRouter::delivery_identity() const {
	return _state->_delivery_identity;
}

//=============================================================================
// Announcing
//=============================================================================

void LXMRouter::announce() {
	if (!_state->_delivery_destination) {
		ERROR("LXMRouter::announce: No delivery destination registered");
		return;
	}

	Bytes app_data = get_announce_app_data();
	_state->_delivery_destination.announce(app_data);

	INFOF("LXMRouter: Announced delivery destination %s",
		  _state->_delivery_destination.hash().toHex().c_str());
}

void LXMRouter::set_display_name(const std::string& name) {
	_state->_display_name = name;
}

void LXMRouter::set_stamp_cost(uint8_t cost) {
	_state->_stamp_cost = cost;
}

Bytes LXMRouter::get_announce_app_data() const {
	// Format: msgpack([display_name, stamp_cost])
	// display_name: UTF-8 string or nil
	// stamp_cost: uint8 or nil (0 means no stamp required)

	MsgPack::Packer packer;
	packer.pack(MsgPack::arr_size_t(2));

	// Display name
	if (_state->_display_name.empty()) {
		packer.pack(MsgPack::object::nil_t{true});
	} else {
		packer.serialize(_state->_display_name);
	}

	// Stamp cost
	if (_state->_stamp_cost == 0) {
		packer.pack(MsgPack::object::nil_t{true});
	} else {
		packer.serialize(_state->_stamp_cost);
	}

	return Bytes(packer.data(), packer.size());
}

//=============================================================================
// Message Sending
//=============================================================================

bool LXMRouter::handle_outbound(LXMessage& message) {
	if (!message) {
		ERROR("LXMRouter::handle_outbound: Invalid message");
		return false;
	}

	// Check queue capacity
	if (_state->_pending_outbound.size() >= MAX_OUTBOUND_QUEUE) {
		ERROR("LXMRouter::handle_outbound: Outbound queue full");
		return false;
	}

	// Pack message if not already packed
	if (message.packed_size() == 0) {
		if (!message.pack()) {
			ERROR("LXMRouter::handle_outbound: Failed to pack message");
			return false;
		}
	}

	// Set state to outbound
	message.set_state(MessageState::OUTBOUND);

	// Add to pending queue
	_state->_pending_outbound.push_back(message);

	DEBUGF("LXMRouter: Queued message %s for outbound delivery",
		   message.hash().toHex().c_str());

	return true;
}

bool LXMRouter::cancel_outbound(LXMessage& message) {
	for (auto it = _state->_pending_outbound.begin();
		 it != _state->_pending_outbound.end(); ++it) {
		if (*it == message) {
			message.set_state(MessageState::CANCELLED);
			_state->_pending_outbound.erase(it);
			return true;
		}
	}
	return false;
}

size_t LXMRouter::pending_outbound_count() const {
	return _state->_pending_outbound.size();
}

//=============================================================================
// Processing Loop
//=============================================================================

void LXMRouter::loop(size_t max_messages) {
	// Clean duplicate cache periodically
	clean_duplicate_cache();

	// Process pending outbound messages
	size_t processed = 0;
	auto it = _state->_pending_outbound.begin();

	while (it != _state->_pending_outbound.end() &&
		   (max_messages == 0 || processed < max_messages)) {

		LXMessage& message = *it;

		if (process_outbound_message(message)) {
			// Message was sent or failed, remove from queue
			it = _state->_pending_outbound.erase(it);
			processed++;
		} else {
			// Message still pending
			++it;
		}
	}
}

bool LXMRouter::process_outbound_message(LXMessage& message) {
	// Check if we've exceeded max attempts
	if (message.delivery_attempts() >= MAX_DELIVERY_ATTEMPTS) {
		WARNINGF("LXMRouter: Message %s exceeded max delivery attempts",
				 message.hash().toHex().c_str());
		message.set_state(MessageState::FAILED);
		return true;  // Remove from queue
	}

	// Get destination hash
	Bytes dest_hash = message.destination_hash();

	// Check if we have an active link to this destination
	auto link_it = _state->_direct_links.find(dest_hash);
	if (link_it != _state->_direct_links.end()) {
		Link& link = link_it->second;

		if (link.status() == Type::Link::ACTIVE) {
			// Send over existing link
			if (send_over_link(message, link)) {
				return true;  // Message sent
			}
		} else if (link.status() == Type::Link::CLOSED ||
				   link.status() == Type::Link::STALE) {
			// Remove stale link
			_state->_direct_links.erase(link_it);
		}
	}

	// For DIRECT delivery, we need to establish a link
	if (message.desired_method() == DeliveryMethod::DIRECT) {
		// Check if path is known
		if (!Transport::has_path(dest_hash)) {
			// Request path if not already requested
			Transport::request_path(dest_hash);
			message.set_delivery_attempts(message.delivery_attempts() + 1);
			return false;  // Keep in queue
		}

		// Create destination from hash
		// Note: We need the identity to create a proper destination
		// For now, create a destination with just the hash
		Identity recalled = Identity::recall(dest_hash);
		if (!recalled) {
			DEBUGF("LXMRouter: Cannot recall identity for %s",
				   dest_hash.toHex().c_str());
			message.set_delivery_attempts(message.delivery_attempts() + 1);
			return false;
		}

		Destination dest(recalled, Type::Destination::OUT, Type::Destination::SINGLE,
						 APP_NAME, ASPECT_DELIVERY);

		// Establish new link
		Link link(dest);
		if (!link) {
			WARNINGF("LXMRouter: Failed to create link to %s",
					 dest_hash.toHex().c_str());
			message.set_delivery_attempts(message.delivery_attempts() + 1);
			return false;
		}

		// Store link for future use
		_state->_direct_links[dest_hash] = link;

		// Link is being established, will send when ready
		message.set_delivery_attempts(message.delivery_attempts() + 1);
		return false;  // Keep in queue
	}

	// For OPPORTUNISTIC delivery (single packet, no link needed)
	if (message.desired_method() == DeliveryMethod::OPPORTUNISTIC) {
		// Check message size for opportunistic delivery
		if (message.packed_size() > Wire::ENCRYPTED_PACKET_MAX_CONTENT + Wire::LXMF_OVERHEAD) {
			// Too large for opportunistic, try direct
			DEBUG("LXMRouter: Message too large for opportunistic, switching to direct");
			// Note: In a full implementation, we'd update the method
			message.set_delivery_attempts(message.delivery_attempts() + 1);
			return false;
		}

		// Get destination
		Identity recalled = Identity::recall(dest_hash);
		if (!recalled) {
			message.set_delivery_attempts(message.delivery_attempts() + 1);
			return false;
		}

		Destination dest(recalled, Type::Destination::OUT, Type::Destination::SINGLE,
						 APP_NAME, ASPECT_DELIVERY);

		// For opportunistic, we send a single packet
		// The packet contains the packed message minus destination hash (inferred from packet)
		Bytes payload = message.packed().mid(Wire::DESTINATION_LENGTH);

		message.set_state(MessageState::SENDING);
		Packet packet(dest, payload, Type::Packet::DATA, Type::Packet::CONTEXT_NONE, Type::Transport::BROADCAST, Type::Packet::HEADER_1);

		if (packet.send()) {
			message.set_state(MessageState::SENT);
			_state->_messages_sent++;
			DEBUGF("LXMRouter: Sent opportunistic message %s",
				   message.hash().toHex().c_str());
			return true;
		} else {
			message.set_state(MessageState::OUTBOUND);
			message.set_delivery_attempts(message.delivery_attempts() + 1);
			return false;
		}
	}

	return false;
}

bool LXMRouter::send_over_link(LXMessage& message, Link& link) {
	if (!link || link.status() != Type::Link::ACTIVE) {
		return false;
	}

	message.set_state(MessageState::SENDING);

	// Determine representation (PACKET vs RESOURCE)
	if (message.packed_size() <= Wire::LINK_PACKET_MAX_CONTENT + Wire::LXMF_OVERHEAD) {
		// Small message - send as single packet over link
		Packet packet(link, message.packed());

		if (packet.send()) {
			message.set_state(MessageState::SENT);
			_state->_messages_sent++;
			DEBUGF("LXMRouter: Sent message %s as packet over link",
				   message.hash().toHex().c_str());
			return true;
		}
	} else {
		// Large message - send as resource
		Resource resource(message.packed(), link);

		if (resource) {
			message.set_state(MessageState::SENT);
			_state->_messages_sent++;
			DEBUGF("LXMRouter: Sent message %s as resource over link",
				   message.hash().toHex().c_str());
			return true;
		}
	}

	message.set_state(MessageState::OUTBOUND);
	return false;
}

//=============================================================================
// Incoming Message Handling (Static Callbacks)
//=============================================================================

/*static*/ void LXMRouter::delivery_packet_callback(const Bytes& data, const Packet& packet) {
	if (!_instance) return;

	DEBUG("LXMRouter: Received delivery packet");

	// Determine method and reconstruct LXMF data
	Bytes lxmf_data;
	DeliveryMethod method;

	// Check if destination is valid - for link-delivered packets, destination may be null
	// or a link destination. If so, treat as DIRECT (data is complete LXMF message).
	if (!packet.destination() || packet.destination().type() == Type::Destination::LINK) {
		// Direct delivery over link - data is complete LXMF message
		lxmf_data = data;
		method = DeliveryMethod::DIRECT;
	} else {
		// Opportunistic delivery - prepend destination hash
		lxmf_data = packet.destination().hash() + data;
		method = DeliveryMethod::OPPORTUNISTIC;
	}

	_instance->lxmf_delivery(lxmf_data, method);
}

/*static*/ void LXMRouter::delivery_link_established_callback(Link& link) {
	if (!_instance) return;

	DEBUGF("LXMRouter: Delivery link established from %s",
		   link.destination().hash().toHex().c_str());

	// Set up link callbacks
	link.set_packet_callback([](const Bytes& data, const Packet& packet) {
		delivery_packet_callback(data, packet);
	});

	// Accept resources (for large messages)
	// link.set_resource_strategy(RNS.Link.ACCEPT_APP);
	// Note: Resource callbacks would be set up here for full implementation
}

void LXMRouter::lxmf_delivery(const Bytes& lxmf_data, DeliveryMethod method) {
	// Unpack the message
	LXMessage message = LXMessage::unpack_from_bytes(lxmf_data, method);

	if (!message) {
		WARNING("LXMRouter: Failed to unpack received message");
		return;
	}

	// Check for duplicates
	if (is_duplicate(message.hash())) {
		DEBUGF("LXMRouter: Ignoring duplicate message %s",
			   message.hash().toHex().c_str());
		return;
	}

	// Add to duplicate cache
	add_to_duplicate_cache(message.hash());

	// Validate signature
	if (!message.signature_validated()) {
		WARNINGF("LXMRouter: Signature validation failed for message %s",
				 message.hash().toHex().c_str());
		// Continue anyway if stamp is valid (deferred for v1)
	}

	_state->_messages_received++;

	INFOF("LXMRouter: Received message %s from %s",
		  message.hash().toHex().c_str(),
		  message.source_hash().toHex().c_str());

	// Invoke delivery callback
	if (_state->_delivery_callback) {
		_state->_delivery_callback(message);
	}
}

//=============================================================================
// Duplicate Cache Management
//=============================================================================

bool LXMRouter::is_duplicate(const Bytes& message_hash) const {
	return _state->_duplicate_cache.find(message_hash) != _state->_duplicate_cache.end();
}

void LXMRouter::add_to_duplicate_cache(const Bytes& message_hash) {
	_state->_duplicate_cache[message_hash] = Utilities::OS::time();
}

void LXMRouter::clean_duplicate_cache() {
	// Remove entries older than 1 hour
	static constexpr double CACHE_EXPIRY = 3600.0;
	double now = Utilities::OS::time();

	auto it = _state->_duplicate_cache.begin();
	while (it != _state->_duplicate_cache.end()) {
		if (now - it->second > CACHE_EXPIRY) {
			it = _state->_duplicate_cache.erase(it);
		} else {
			++it;
		}
	}
}

//=============================================================================
// Statistics
//=============================================================================

uint32_t LXMRouter::messages_sent() const {
	return _state->_messages_sent;
}

uint32_t LXMRouter::messages_received() const {
	return _state->_messages_received;
}

}} // namespace RNS::LXMF
