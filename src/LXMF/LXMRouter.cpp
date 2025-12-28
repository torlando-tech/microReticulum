#include "LXMRouter.h"
#include "../Log.h"
#include "../Utilities/OS.h"
#include "../Packet.h"
#include "../Transport.h"

#include <map>

using namespace LXMF;
using namespace RNS;

// Static router registry for callback dispatch
static std::map<Bytes, LXMRouter*> _router_registry;

// Static packet callback for destination
static void static_packet_callback(const Bytes& data, const Packet& packet) {
	// Look up router by destination hash
	auto it = _router_registry.find(packet.destination_hash());
	if (it != _router_registry.end()) {
		it->second->on_packet(data, packet);
	}
}

// Static link callbacks
static void static_link_established_callback(Link& link) {
	// Find router that owns this link destination
	auto it = _router_registry.find(link.destination().hash());
	if (it != _router_registry.end()) {
		it->second->on_link_established(link);
	}
}

static void static_link_closed_callback(Link& link) {
	// Find router that owns this link destination
	auto it = _router_registry.find(link.destination().hash());
	if (it != _router_registry.end()) {
		it->second->on_link_closed(link);
	}
}

// Constructor
LXMRouter::LXMRouter(
	const Identity& identity,
	const std::string& storage_path,
	bool announce_at_start
) :
	_identity(identity),
	_delivery_destination(RNS::Type::NONE),
	_storage_path(storage_path),
	_announce_at_start(announce_at_start)
{
	INFO("Initializing LXMF Router");

	// Create delivery destination: <identity>/lxmf/delivery
	_delivery_destination = Destination(
		_identity,
		RNS::Type::Destination::IN,
		RNS::Type::Destination::SINGLE,
		"lxmf",
		"delivery"
	);

	// Register this router in global registry for callback dispatch
	_router_registry[_delivery_destination.hash()] = this;

	// Register packet callback for receiving LXMF messages
	_delivery_destination.set_packet_callback(static_packet_callback);

	INFO("  Delivery destination: " + _delivery_destination.hash().toHex());
	INFO("  Destination type: " + std::to_string((uint8_t)_delivery_destination.type()));
	INFO("  Destination direction: " + std::to_string((uint8_t)_delivery_destination.direction()));

	// Announce at start if enabled
	if (_announce_at_start) {
		INFO("  Auto-announce enabled");
		announce();
	}

	_initialized = true;
	INFO("LXMF Router initialized");
}

LXMRouter::~LXMRouter() {
	// Unregister from global registry
	_router_registry.erase(_delivery_destination.hash());
	TRACE("LXMRouter destroyed");
}

// Register callbacks
void LXMRouter::register_delivery_callback(DeliveryCallback callback) {
	_delivery_callback = callback;
	DEBUG("Delivery callback registered");
}

void LXMRouter::register_sent_callback(SentCallback callback) {
	_sent_callback = callback;
	DEBUG("Sent callback registered");
}

void LXMRouter::register_delivered_callback(DeliveredCallback callback) {
	_delivered_callback = callback;
	DEBUG("Delivered callback registered");
}

void LXMRouter::register_failed_callback(FailedCallback callback) {
	_failed_callback = callback;
	DEBUG("Failed callback registered");
}

// Queue outbound message
void LXMRouter::handle_outbound(LXMessage& message) {
	INFO("Handling outbound LXMF message");
	DEBUG("  Destination: " + message.destination_hash().toHex());
	DEBUG("  Content size: " + std::to_string(message.content().size()) + " bytes");

	// Pack the message
	message.pack();

	// Check if message fits in a single packet - use OPPORTUNISTIC if so
	// OPPORTUNISTIC is simpler (no link needed) and works when identity is known
	if (message.packed_size() <= Type::Constants::ENCRYPTED_PACKET_MDU) {
		INFO("  Message fits in single packet, will use OPPORTUNISTIC delivery");
	} else {
		INFO("  Message too large for single packet, will use DIRECT (link) delivery");
	}

	// Set state to outbound
	message.state(Type::Message::OUTBOUND);

	// Add to pending queue
	_pending_outbound.push_back(message);

	INFO("Message queued for delivery (" + std::to_string(_pending_outbound.size()) + " pending)");
}

// Process outbound queue
void LXMRouter::process_outbound() {
	if (_pending_outbound.empty()) {
		return;
	}

	// Check backoff timer - don't process if we're in retry delay
	double now = Utilities::OS::time();
	if (now < _next_outbound_process_time) {
		return;  // Wait until retry delay expires
	}

	// Process one message per call to avoid blocking
	LXMessage& message = _pending_outbound.front();

	DEBUG("Processing outbound message to " + message.destination_hash().toHex());

	try {
		// Determine delivery method based on message size
		bool use_opportunistic = (message.packed_size() <= Type::Constants::ENCRYPTED_PACKET_MDU);

		if (use_opportunistic) {
			// OPPORTUNISTIC delivery - send as single encrypted packet
			DEBUG("  Using OPPORTUNISTIC delivery (single packet)");

			// Check if we have a path to the destination
			if (!Transport::has_path(message.destination_hash())) {
				// Request path from network
				INFO("  No path to destination, requesting...");
				Transport::request_path(message.destination_hash());
				_next_outbound_process_time = now + PATH_REQUEST_WAIT;
				return;
			}

			// Try to recall the destination identity
			Identity dest_identity = Identity::recall(message.destination_hash());
			if (!dest_identity) {
				// Path exists but identity not cached yet - wait for announce
				INFO("  Path exists but identity not known, waiting for announce...");
				_next_outbound_process_time = now + OUTBOUND_RETRY_DELAY;
				return;
			}

			// Create destination and send packet
			if (send_opportunistic(message, dest_identity)) {
				INFO("Message sent via OPPORTUNISTIC delivery");

				// Call sent callback if registered
				if (_sent_callback) {
					_sent_callback(message);
				}

				// Remove from pending queue
				_pending_outbound.erase(_pending_outbound.begin());
			} else {
				ERROR("Failed to send OPPORTUNISTIC message");
				message.state(Type::Message::FAILED);

				if (_failed_callback) {
					_failed_callback(message);
				}

				_failed_outbound.push_back(message);
				_pending_outbound.erase(_pending_outbound.begin());
			}
		} else {
			// DIRECT delivery - need a link for large messages
			DEBUG("  Using DIRECT delivery (via link)");

			// Get or establish link
			Link link = get_link_for_destination(message.destination_hash());

			if (!link) {
				WARNING("Failed to establish link for message delivery");
				// Set backoff timer to avoid tight loop
				_next_outbound_process_time = now + OUTBOUND_RETRY_DELAY;
				INFO("  Will retry in " + std::to_string((int)OUTBOUND_RETRY_DELAY) + " seconds");
				return;
			}

			// Check link status
			if (link.status() != RNS::Type::Link::ACTIVE) {
				DEBUG("Link not yet active, waiting...");
				// Set shorter backoff for pending links
				_next_outbound_process_time = now + 1.0;  // Check again in 1 second
				return;
			}

			// Send via link
			if (send_via_link(message, link)) {
				INFO("Message sent successfully via link");

				// Call sent callback if registered
				if (_sent_callback) {
					_sent_callback(message);
				}

				// Remove from pending queue
				_pending_outbound.erase(_pending_outbound.begin());
			} else {
				ERROR("Failed to send message via link");
				message.state(Type::Message::FAILED);

				// Call failed callback
				if (_failed_callback) {
					_failed_callback(message);
				}

				// Move to failed queue
				_failed_outbound.push_back(message);
				_pending_outbound.erase(_pending_outbound.begin());
			}
		}

	} catch (const std::exception& e) {
		ERROR("Exception processing outbound message: " + std::string(e.what()));
		message.state(Type::Message::FAILED);

		// Call failed callback
		if (_failed_callback) {
			_failed_callback(message);
		}

		// Move to failed queue
		_failed_outbound.push_back(message);
		_pending_outbound.erase(_pending_outbound.begin());
	}
}

// Process inbound queue
void LXMRouter::process_inbound() {
	if (_pending_inbound.empty()) {
		return;
	}

	// Process one message per call
	LXMessage& message = _pending_inbound.front();

	DEBUG("Processing inbound message from " + message.source_hash().toHex());

	try {
		// Message is already unpacked and validated in on_packet()
		// Just invoke the delivery callback
		if (_delivery_callback) {
			_delivery_callback(message);
		}

		// Remove from pending queue
		_pending_inbound.erase(_pending_inbound.begin());

		INFO("Inbound message processed (" + std::to_string(_pending_inbound.size()) + " remaining)");

	} catch (const std::exception& e) {
		ERROR("Exception processing inbound message: " + std::string(e.what()));
		// Discard message on error
		_pending_inbound.erase(_pending_inbound.begin());
	}
}

// Announce delivery destination
void LXMRouter::announce(const Bytes& app_data, bool path_response) {
	INFO("Announcing LXMF delivery destination: " + _delivery_destination.hash().toHex());

	try {
		DEBUG("  Calling _delivery_destination.announce()...");
		_delivery_destination.announce(app_data, path_response);
		_last_announce_time = Utilities::OS::time();
		INFO("Announce sent successfully");

	} catch (const std::exception& e) {
		ERROR("Failed to announce: " + std::string(e.what()));
	}
}

// Set announce interval
void LXMRouter::set_announce_interval(uint32_t interval) {
	_announce_interval = interval;
	if (interval > 0) {
		INFO("Auto-announce interval set to " + std::to_string(interval) + " seconds");
	} else {
		INFO("Auto-announce disabled");
	}
}

// Set announce at start
void LXMRouter::set_announce_at_start(bool enabled) {
	_announce_at_start = enabled;
	DEBUG("Announce at start: " + std::string(enabled ? "enabled" : "disabled"));
}

// Clear failed outbound
void LXMRouter::clear_failed_outbound() {
	size_t count = _failed_outbound.size();
	_failed_outbound.clear();
	INFO("Cleared " + std::to_string(count) + " failed outbound messages");
}

// Retry failed outbound
void LXMRouter::retry_failed_outbound() {
	if (_failed_outbound.empty()) {
		return;
	}

	INFO("Retrying " + std::to_string(_failed_outbound.size()) + " failed messages");

	// Move all failed messages back to pending
	for (auto& message : _failed_outbound) {
		message.state(Type::Message::OUTBOUND);
		_pending_outbound.push_back(message);
	}

	_failed_outbound.clear();
}

// Packet callback - receive LXMF messages
void LXMRouter::on_packet(const Bytes& data, const Packet& packet) {
	INFO("Received LXMF message packet (" + std::to_string(data.size()) + " bytes)");
	DEBUG("  From: " + packet.destination_hash().toHex());
	DEBUG("  Destination type: " + std::to_string((uint8_t)packet.destination_type()));

	try {
		// Build LXMF data based on delivery method (matches Python LXMF exactly)
		Bytes lxmf_data;
		Type::Message::Method method;

		if (packet.destination_type() != RNS::Type::Destination::LINK) {
			// OPPORTUNISTIC delivery: destination hash is NOT in the encrypted data
			// We need to prepend it from the packet destination
			method = Type::Message::OPPORTUNISTIC;
			INFO("  Delivery method: OPPORTUNISTIC (prepending destination hash)");
			lxmf_data = _delivery_destination.hash() + data;
		} else {
			// DIRECT delivery via Link: data already contains everything
			method = Type::Message::DIRECT;
			INFO("  Delivery method: DIRECT (data complete)");
			lxmf_data = data;
		}

		DEBUG("  LXMF data size after processing: " + std::to_string(lxmf_data.size()) + " bytes");

		// Unpack LXMF message from packet data
		LXMessage message = LXMessage::unpack_from_bytes(lxmf_data, method);

		DEBUG("  Message hash: " + message.hash().toHex());
		DEBUG("  Source: " + message.source_hash().toHex());
		DEBUG("  Content size: " + std::to_string(message.content().size()) + " bytes");

		// Verify destination matches our delivery destination
		if (message.destination_hash() != _delivery_destination.hash()) {
			WARNING("Message destination mismatch - ignoring");
			return;
		}

		// Signature validation
		if (!message.signature_validated()) {
			WARNING("Message signature not validated");
			DEBUG("  Unverified reason: " + std::to_string((uint8_t)message.unverified_reason()));

			// For Phase 1 MVP, we'll still accept messages with unknown source
			// (signature will be validated later if source identity is learned)
			if (message.unverified_reason() != Type::Message::SOURCE_UNKNOWN) {
				WARNING("  Rejecting message with invalid signature");
				return;
			}
		}

		// Add to inbound queue
		_pending_inbound.push_back(message);

		INFO("Message queued for processing (" + std::to_string(_pending_inbound.size()) + " pending)");

	} catch (const std::exception& e) {
		ERROR("Failed to unpack LXMF message: " + std::string(e.what()));
	}
}

// Get or establish link to destination
Link LXMRouter::get_link_for_destination(const Bytes& destination_hash) {
	DEBUG("Getting link for destination " + destination_hash.toHex());

	// Check if we already have an active link
	auto it = _direct_links.find(destination_hash);
	if (it != _direct_links.end()) {
		Link& existing_link = it->second;

		// Check if link is still valid
		if (existing_link &&
		    (existing_link.status() == RNS::Type::Link::ACTIVE ||
		     existing_link.status() == RNS::Type::Link::PENDING)) {
			DEBUG("  Using existing link (status: " + std::to_string((uint8_t)existing_link.status()) + ")");
			return existing_link;
		} else {
			DEBUG("  Existing link is not active, removing");
			_direct_links.erase(it);
		}
	}

	// Need to establish new link
	INFO("  Establishing new link to " + destination_hash.toHex());

	try {
		// Create destination object for link
		// We need to recall the identity if we have it
		Identity dest_identity = Identity::recall(destination_hash);

		if (!dest_identity) {
			// We need the identity to establish link
			WARNING("  Don't have identity for destination - cannot establish link");
			WARNING("  Destination must announce first");
			return Link(RNS::Type::NONE);
		}

		// Create destination for link
		Destination link_destination(
			dest_identity,
			RNS::Type::Destination::OUT,
			RNS::Type::Destination::SINGLE,
			"lxmf",
			"delivery"
		);

		// Create link
		Link link(link_destination);

		// Register in global registry for link callbacks
		_router_registry[link_destination.hash()] = this;

		// Register link callbacks (using static functions)
		link.set_link_established_callback(static_link_established_callback);
		link.set_link_closed_callback(static_link_closed_callback);

		// Store link (use insert to avoid default-constructing Link which crashes)
		_direct_links.insert({destination_hash, link});

		INFO("  Link establishment initiated");
		return link;

	} catch (const std::exception& e) {
		ERROR("  Failed to establish link: " + std::string(e.what()));
		return Link(RNS::Type::NONE);
	}
}

// Send message via link
bool LXMRouter::send_via_link(LXMessage& message, Link& link) {
	INFO("Sending LXMF message via link");
	DEBUG("  Message size: " + std::to_string(message.packed_size()) + " bytes");
	DEBUG("  Representation: " + std::to_string((uint8_t)message.representation()));

	try {
		// Use LXMessage's send_via_link method
		bool success = message.send_via_link(link);

		if (success) {
			message.state(Type::Message::SENT);
		}

		return success;

	} catch (const std::exception& e) {
		ERROR("Failed to send message: " + std::string(e.what()));
		return false;
	}
}

// Send message via OPPORTUNISTIC delivery (single encrypted packet)
bool LXMRouter::send_opportunistic(LXMessage& message, const Identity& dest_identity) {
	INFO("Sending LXMF message via OPPORTUNISTIC delivery");
	DEBUG("  Message size: " + std::to_string(message.packed_size()) + " bytes");

	try {
		// Create destination object for the remote peer's LXMF delivery
		Destination destination(
			dest_identity,
			RNS::Type::Destination::OUT,
			RNS::Type::Destination::SINGLE,
			"lxmf",
			"delivery"
		);

		// Verify destination hash matches
		if (destination.hash() != message.destination_hash()) {
			ERROR("Destination hash mismatch!");
			DEBUG("  Expected: " + message.destination_hash().toHex());
			DEBUG("  Got: " + destination.hash().toHex());
			return false;
		}

		// For OPPORTUNISTIC, we strip the destination hash from the packed data
		// since it's already in the packet header
		Bytes packet_data = message.packed().mid(Type::Constants::DESTINATION_LENGTH);
		DEBUG("  Packet data size: " + std::to_string(packet_data.size()) + " bytes");

		// Create and send packet
		Packet packet(destination, packet_data, RNS::Type::Packet::DATA);
		packet.send();

		message.state(Type::Message::SENT);
		INFO("  OPPORTUNISTIC packet sent");

		return true;

	} catch (const std::exception& e) {
		ERROR("Failed to send OPPORTUNISTIC message: " + std::string(e.what()));
		return false;
	}
}

// Link established callback
void LXMRouter::on_link_established(const Link& link) {
	INFO("Link established to " + link.destination().hash().toHex());

	// Link is now active, process_outbound() will pick it up
}

// Link closed callback
void LXMRouter::on_link_closed(const Link& link) {
	INFO("Link closed to " + link.destination().hash().toHex());

	// Remove from active links
	auto it = _direct_links.find(link.destination().hash());
	if (it != _direct_links.end()) {
		_direct_links.erase(it);
		DEBUG("  Removed link from cache");
	}
}
