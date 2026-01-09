#include "LXMRouter.h"
#include "PropagationNodeManager.h"
#include "../Log.h"
#include "../Utilities/OS.h"
#include "../Packet.h"
#include "../Transport.h"
#include "../Resource.h"

#include <map>
#include <set>
#include <MsgPack.h>

using namespace LXMF;
using namespace RNS;

// Static router registry for callback dispatch
static std::map<Bytes, LXMRouter*> _router_registry;

// Static pending proofs map (packet_hash -> message_hash)
std::map<Bytes, Bytes> LXMRouter::_pending_proofs;

// Static pending outbound resources map (resource_hash -> message_hash)
// Used to track DIRECT delivery completion
static std::map<Bytes, Bytes> _pending_outbound_resources;

// Static pending propagation resources map (resource_hash -> message_hash)
// Used to track PROPAGATED delivery completion
std::map<Bytes, Bytes> LXMRouter::_pending_propagation_resources;

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

// Static callback for incoming link established on delivery destination
static void static_delivery_link_established_callback(Link& link) {
	// Find router that owns this destination
	auto it = _router_registry.find(link.destination().hash());
	if (it != _router_registry.end()) {
		it->second->on_incoming_link_established(link);
	}
}

// Static callback for resource concluded on delivery links (receiving)
static void static_resource_concluded_callback(const Resource& resource) {
	Link link = resource.link();
	if (!link) {
		ERROR("static_resource_concluded_callback: Resource has no link");
		return;
	}

	// Find router that owns this link's destination
	auto it = _router_registry.find(link.destination().hash());
	if (it != _router_registry.end()) {
		it->second->on_resource_concluded(resource);
	}
}

// Static callback for outbound resource concluded (sending)
// Called when our resource transfer completes (receiver sent RESOURCE_PRF)
static void static_outbound_resource_concluded(const Resource& resource) {
	Bytes resource_hash = resource.hash();
	char buf[128];
	snprintf(buf, sizeof(buf), "Outbound resource concluded: %.16s...", resource_hash.toHex().c_str());
	DEBUG(buf);
	snprintf(buf, sizeof(buf), "  Status: %d", (int)resource.status());
	DEBUG(buf);

	// Check if this resource is one we're tracking
	auto it = _pending_outbound_resources.find(resource_hash);
	if (it == _pending_outbound_resources.end()) {
		DEBUG("  Resource not in pending outbound map");
		return;
	}

	Bytes message_hash = it->second;

	// Check if resource completed successfully
	if (resource.status() == RNS::Type::Resource::COMPLETE) {
		snprintf(buf, sizeof(buf), "DIRECT delivery confirmed for message %.16s...", message_hash.toHex().c_str());
		INFO(buf);

		// Use the public static method to trigger delivered callback
		LXMRouter::handle_direct_proof(message_hash);
	} else {
		snprintf(buf, sizeof(buf), "DIRECT resource transfer failed with status %d", (int)resource.status());
		WARNING(buf);
	}

	// Remove from pending map
	_pending_outbound_resources.erase(it);
}

// Static proof callback - called when delivery proof is received
void LXMRouter::static_proof_callback(const PacketReceipt& receipt) {
	// Get packet hash from receipt
	Bytes packet_hash = receipt.hash();
	char buf[128];

	// Look up message hash for this packet
	auto it = _pending_proofs.find(packet_hash);
	if (it != _pending_proofs.end()) {
		Bytes message_hash = it->second;
		snprintf(buf, sizeof(buf), "Delivery proof received for message %.16s...", message_hash.toHex().c_str());
		INFO(buf);

		// Use set to avoid calling callback multiple times for same router
		std::set<LXMRouter*> notified_routers;

		// Find the router that sent this message and call its delivered callback
		for (auto& router_entry : _router_registry) {
			LXMRouter* router = router_entry.second;
			if (router && router->_delivered_callback && notified_routers.find(router) == notified_routers.end()) {
				notified_routers.insert(router);
				// Create a minimal message with just the hash for the callback
				// The callback can look up full message from storage if needed
				Bytes empty_hash;
				LXMessage msg(empty_hash, empty_hash);
				msg.hash(message_hash);
				msg.state(Type::Message::DELIVERED);
				router->_delivered_callback(msg);
			}
		}

		// Remove from pending proofs
		_pending_proofs.erase(it);
	} else {
		snprintf(buf, sizeof(buf), "Received proof for unknown packet: %.16s...", packet_hash.toHex().c_str());
		DEBUG(buf);
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

	// Register packet callback for receiving LXMF messages (OPPORTUNISTIC)
	_delivery_destination.set_packet_callback(static_packet_callback);

	// Register link established callback for incoming links (DIRECT delivery)
	_delivery_destination.set_link_established_callback(static_delivery_link_established_callback);

	char buf[128];
	snprintf(buf, sizeof(buf), "  Delivery destination: %s", _delivery_destination.hash().toHex().c_str());
	INFO(buf);
	snprintf(buf, sizeof(buf), "  Destination type: %u", (uint8_t)_delivery_destination.type());
	INFO(buf);
	snprintf(buf, sizeof(buf), "  Destination direction: %u", (uint8_t)_delivery_destination.direction());
	INFO(buf);

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
	char buf[128];
	snprintf(buf, sizeof(buf), "  Destination: %s", message.destination_hash().toHex().c_str());
	DEBUG(buf);
	snprintf(buf, sizeof(buf), "  Content size: %zu bytes", message.content().size());
	DEBUG(buf);

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

	snprintf(buf, sizeof(buf), "Message queued for delivery (%zu pending)", _pending_outbound.size());
	INFO(buf);
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
	char buf[128];

	snprintf(buf, sizeof(buf), "Processing outbound message to %s", message.destination_hash().toHex().c_str());
	DEBUG(buf);

	try {
		// If propagation-only mode is enabled, send via propagation node
		if (_propagation_only) {
			DEBUG("  Using PROPAGATED delivery (propagation-only mode)");
			message.set_method(Type::Message::PROPAGATED);
			if (send_propagated(message)) {
				INFO("Message sent via PROPAGATED delivery");
				if (_sent_callback) {
					_sent_callback(message);
				}
				_pending_outbound.pop_front();
			} else {
				// Propagation not ready yet - wait and retry
				DEBUG("  Propagation delivery not ready, will retry...");
				_next_outbound_process_time = now + OUTBOUND_RETRY_DELAY;
			}
			return;
		}

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
				_pending_outbound.pop_front();
			} else {
				ERROR("Failed to send OPPORTUNISTIC message");
				message.state(Type::Message::FAILED);

				if (_failed_callback) {
					_failed_callback(message);
				}

				_failed_outbound.push_back(message);
				_pending_outbound.pop_front();
			}
		} else {
			// DIRECT delivery - need a link for large messages
			DEBUG("  Using DIRECT delivery (via link)");

			// Check if we have a path to the destination
			if (!Transport::has_path(message.destination_hash())) {
				// Request path from network
				INFO("  No path to destination, requesting...");
				Transport::request_path(message.destination_hash());
				_next_outbound_process_time = now + PATH_REQUEST_WAIT;
				return;
			}

			// Get or establish link
			Link link = get_link_for_destination(message.destination_hash());

			if (!link) {
				WARNING("Failed to establish link for message delivery");
				// Set backoff timer to avoid tight loop
				_next_outbound_process_time = now + OUTBOUND_RETRY_DELAY;
				snprintf(buf, sizeof(buf), "  Will retry in %d seconds", (int)OUTBOUND_RETRY_DELAY);
				INFO(buf);
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
				_pending_outbound.pop_front();
			} else {
				ERROR("Failed to send message via link");
				message.state(Type::Message::FAILED);

				// Call failed callback
				if (_failed_callback) {
					_failed_callback(message);
				}

				// Move to failed queue
				_failed_outbound.push_back(message);
				_pending_outbound.pop_front();
			}
		}

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "Exception processing outbound message: %s", e.what());
		ERROR(buf);
		message.state(Type::Message::FAILED);

		// Call failed callback
		if (_failed_callback) {
			_failed_callback(message);
		}

		// Move to failed queue
		_failed_outbound.push_back(message);
		_pending_outbound.pop_front();
	}
}

// Process inbound queue
void LXMRouter::process_inbound() {
	if (_pending_inbound.empty()) {
		return;
	}

	// Process one message per call
	LXMessage& message = _pending_inbound.front();
	char buf[128];

	snprintf(buf, sizeof(buf), "Processing inbound message from %s", message.source_hash().toHex().c_str());
	DEBUG(buf);

	try {
		// Message is already unpacked and validated in on_packet()
		// Just invoke the delivery callback
		if (_delivery_callback) {
			_delivery_callback(message);
		}

		// Remove from pending queue
		_pending_inbound.pop_front();

		snprintf(buf, sizeof(buf), "Inbound message processed (%zu remaining)", _pending_inbound.size());
		INFO(buf);

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "Exception processing inbound message: %s", e.what());
		ERROR(buf);
		// Discard message on error
		_pending_inbound.pop_front();
	}
}

// Announce delivery destination
void LXMRouter::announce(const Bytes& app_data, bool path_response) {
	char buf[128];
	snprintf(buf, sizeof(buf), "Announcing LXMF delivery destination: %s", _delivery_destination.hash().toHex().c_str());
	INFO(buf);

	try {
		Bytes announce_data;

		// If app_data provided, use it directly
		if (app_data && app_data.size() > 0) {
			announce_data = app_data;
		}
		// Otherwise build LXMF-format app_data: [display_name, stamp_cost]
		else if (!_display_name.empty()) {
			MsgPack::Packer packer;
			// LXMF 0.5.0+ format: [display_name_bytes, stamp_cost]
			packer.pack(MsgPack::arr_size_t(2));
			// Pack display name as raw bytes
			std::vector<uint8_t> name_bytes(_display_name.begin(), _display_name.end());
			MsgPack::bin_t<uint8_t> name_bin;
			name_bin = name_bytes;
			packer.pack(name_bin);
			// Pack stamp_cost as nil (not used)
			packer.packNil();

			announce_data = Bytes(packer.data(), packer.size());
			snprintf(buf, sizeof(buf), "  Built LXMF app_data for display_name: %s", _display_name.c_str());
			DEBUG(buf);
		}

		snprintf(buf, sizeof(buf), "  Name hash: %s", RNS::Destination::name_hash("lxmf", "delivery").toHex().c_str());
		DEBUG(buf);
		snprintf(buf, sizeof(buf), "  App_data (%zu bytes): %s", announce_data.size(),
		         (announce_data.size() > 0 ? announce_data.toHex().c_str() : "(empty)"));
		DEBUG(buf);
		DEBUG("  Calling _delivery_destination.announce()...");
		_delivery_destination.announce(announce_data, path_response);
		_last_announce_time = Utilities::OS::time();
		INFO("Announce sent successfully");

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "Failed to announce: %s", e.what());
		ERROR(buf);
	}
}

// Set announce interval
void LXMRouter::set_announce_interval(uint32_t interval) {
	_announce_interval = interval;
	if (interval > 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Auto-announce interval set to %u seconds", interval);
		INFO(buf);
	} else {
		INFO("Auto-announce disabled");
	}
}

// Set announce at start
void LXMRouter::set_announce_at_start(bool enabled) {
	_announce_at_start = enabled;
	DEBUG(enabled ? "Announce at start: enabled" : "Announce at start: disabled");
}

// Set display name for announces
void LXMRouter::set_display_name(const std::string& name) {
	_display_name = name;
	if (!name.empty()) {
		char buf[128];
		snprintf(buf, sizeof(buf), "Display name set to: %s", name.c_str());
		INFO(buf);
	}
}

// Clear failed outbound
void LXMRouter::clear_failed_outbound() {
	size_t count = _failed_outbound.size();
	_failed_outbound.clear();
	char buf[64];
	snprintf(buf, sizeof(buf), "Cleared %zu failed outbound messages", count);
	INFO(buf);
}

// Retry failed outbound
void LXMRouter::retry_failed_outbound() {
	if (_failed_outbound.empty()) {
		return;
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "Retrying %zu failed messages", _failed_outbound.size());
	INFO(buf);

	// Move all failed messages back to pending
	for (auto& message : _failed_outbound) {
		message.state(Type::Message::OUTBOUND);
		_pending_outbound.push_back(message);
	}

	_failed_outbound.clear();
}

// Packet callback - receive LXMF messages
void LXMRouter::on_packet(const Bytes& data, const Packet& packet) {
	char buf[128];
	snprintf(buf, sizeof(buf), "Received LXMF message packet (%zu bytes)", data.size());
	INFO(buf);
	snprintf(buf, sizeof(buf), "  From: %s", packet.destination_hash().toHex().c_str());
	DEBUG(buf);
	snprintf(buf, sizeof(buf), "  Destination type: %u", (uint8_t)packet.destination_type());
	DEBUG(buf);

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

		snprintf(buf, sizeof(buf), "  LXMF data size after processing: %zu bytes", lxmf_data.size());
		DEBUG(buf);

		// Unpack LXMF message from packet data
		LXMessage message = LXMessage::unpack_from_bytes(lxmf_data, method);

		snprintf(buf, sizeof(buf), "  Message hash: %s", message.hash().toHex().c_str());
		DEBUG(buf);
		snprintf(buf, sizeof(buf), "  Source: %s", message.source_hash().toHex().c_str());
		DEBUG(buf);
		snprintf(buf, sizeof(buf), "  Content size: %zu bytes", message.content().size());
		DEBUG(buf);

		// Verify destination matches our delivery destination
		if (message.destination_hash() != _delivery_destination.hash()) {
			WARNING("Message destination mismatch - ignoring");
			return;
		}

		// Signature validation
		if (!message.signature_validated()) {
			WARNING("Message signature not validated");
			snprintf(buf, sizeof(buf), "  Unverified reason: %u", (uint8_t)message.unverified_reason());
			DEBUG(buf);

			// For Phase 1 MVP, we'll still accept messages with unknown source
			// (signature will be validated later if source identity is learned)
			if (message.unverified_reason() != Type::Message::SOURCE_UNKNOWN) {
				WARNING("  Rejecting message with invalid signature");
				return;
			}
		}

		// Stamp enforcement (if enabled)
		if (_stamp_cost > 0 && _enforce_stamps) {
			if (!message.validate_stamp(_stamp_cost)) {
				snprintf(buf, sizeof(buf), "  Rejecting message with invalid or missing stamp (required cost=%u)", _stamp_cost);
				WARNING(buf);
				return;
			}
			INFO("  Stamp validated");
		}

		// Send delivery proof back to sender (matches Python LXMF packet.prove())
		// Make a non-const copy to call prove() since Packet uses shared_ptr internally
		Packet proof_packet = packet;
		INFO("  Sending delivery proof");
		proof_packet.prove();

		// Add to inbound queue
		_pending_inbound.push_back(message);

		snprintf(buf, sizeof(buf), "Message queued for processing (%zu pending)", _pending_inbound.size());
		INFO(buf);

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "Failed to unpack LXMF message: %s", e.what());
		ERROR(buf);
	}
}

// Get or establish link to destination
Link LXMRouter::get_link_for_destination(const Bytes& destination_hash) {
	char buf[128];
	snprintf(buf, sizeof(buf), "Getting link for destination %s", destination_hash.toHex().c_str());
	DEBUG(buf);

	// Check if we already have an active link
	auto it = _direct_links.find(destination_hash);
	if (it != _direct_links.end()) {
		Link& existing_link = it->second;

		// Check if link is still valid
		if (existing_link && existing_link.status() == RNS::Type::Link::ACTIVE) {
			DEBUG("  Using existing active link");
			return existing_link;
		} else if (existing_link && existing_link.status() == RNS::Type::Link::PENDING) {
			// Check if pending link has timed out
			auto time_it = _link_creation_times.find(destination_hash);
			if (time_it != _link_creation_times.end()) {
				double age = Utilities::OS::time() - time_it->second;
				if (age > LINK_ESTABLISHMENT_TIMEOUT) {
					snprintf(buf, sizeof(buf), "  Pending link timed out after %ds, removing", (int)age);
					WARNING(buf);
					_direct_links.erase(it);
					_link_creation_times.erase(time_it);
					// Fall through to create new link
				} else {
					snprintf(buf, sizeof(buf), "  Using existing pending link (age: %ds)", (int)age);
					DEBUG(buf);
					return existing_link;
				}
			} else {
				DEBUG("  Using existing pending link");
				return existing_link;
			}
		} else {
			DEBUG("  Existing link is not active, removing");
			_direct_links.erase(it);
			_link_creation_times.erase(destination_hash);
		}
	}

	// Need to establish new link
	snprintf(buf, sizeof(buf), "  Establishing new link to %s", destination_hash.toHex().c_str());
	INFO(buf);

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

		// Store link and creation time
		_direct_links.insert({destination_hash, link});
		_link_creation_times[destination_hash] = Utilities::OS::time();

		INFO("  Link establishment initiated");
		return link;

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "  Failed to establish link: %s", e.what());
		ERROR(buf);
		return Link(RNS::Type::NONE);
	}
}

// Send message via link
bool LXMRouter::send_via_link(LXMessage& message, Link& link) {
	INFO("Sending LXMF message via link");
	char buf[128];
	snprintf(buf, sizeof(buf), "  Message size: %zu bytes", message.packed_size());
	DEBUG(buf);
	snprintf(buf, sizeof(buf), "  Representation: %u", (uint8_t)message.representation());
	DEBUG(buf);

	try {
		// Ensure message is packed
		if (!message.packed_size()) {
			message.pack();
		}

		// Check that link is active
		if (!link || link.status() != RNS::Type::Link::ACTIVE) {
			ERROR("Cannot send message - link is not active");
			return false;
		}

		message.state(Type::Message::SENDING);

		if (message.representation() == Type::Message::PACKET) {
			// Send as single packet over link
			snprintf(buf, sizeof(buf), "  Sending as single packet (%zu bytes)", message.packed_size());
			INFO(buf);

			Packet packet(link, message.packed());
			packet.send();

			message.state(Type::Message::SENT);
			INFO("Message sent successfully as packet");
			return true;

		} else if (message.representation() == Type::Message::RESOURCE) {
			// Send as resource over link with concluded callback
			snprintf(buf, sizeof(buf), "  Sending as resource (%zu bytes)", message.packed_size());
			INFO(buf);

			// Create resource with our callback to track completion
			Resource resource(message.packed(), link, true, true, static_outbound_resource_concluded);

			// Track this resource so we can match the callback to the message
			if (resource.hash()) {
				_pending_outbound_resources[resource.hash()] = message.hash();
				snprintf(buf, sizeof(buf), "  Tracking resource %.16s for message %.16s",
				         resource.hash().toHex().c_str(), message.hash().toHex().c_str());
				DEBUG(buf);
			}

			message.state(Type::Message::SENT);
			INFO("Message resource transfer initiated");
			return true;

		} else {
			ERROR("Unknown message representation");
			message.state(Type::Message::FAILED);
			return false;
		}

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "Failed to send message: %s", e.what());
		ERROR(buf);
		return false;
	}
}

// Send message via OPPORTUNISTIC delivery (single encrypted packet)
bool LXMRouter::send_opportunistic(LXMessage& message, const Identity& dest_identity) {
	INFO("Sending LXMF message via OPPORTUNISTIC delivery");
	char buf[128];
	snprintf(buf, sizeof(buf), "  Message size: %zu bytes", message.packed_size());
	DEBUG(buf);

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
			snprintf(buf, sizeof(buf), "  Expected: %s", message.destination_hash().toHex().c_str());
			DEBUG(buf);
			snprintf(buf, sizeof(buf), "  Got: %s", destination.hash().toHex().c_str());
			DEBUG(buf);
			return false;
		}

		// For OPPORTUNISTIC, we strip the destination hash from the packed data
		// since it's already in the packet header
		Bytes packet_data = message.packed().mid(Type::Constants::DESTINATION_LENGTH);
		snprintf(buf, sizeof(buf), "  Packet data size: %zu bytes", packet_data.size());
		DEBUG(buf);

		// Create and send packet
		Packet packet(destination, packet_data, RNS::Type::Packet::DATA);
		PacketReceipt receipt = packet.send();

		// Register proof callback to track delivery confirmation
		if (receipt) {
			receipt.set_delivery_callback(static_proof_callback);
			_pending_proofs[receipt.hash()] = message.hash();
			snprintf(buf, sizeof(buf), "  Registered proof callback for packet %.16s...", receipt.hash().toHex().c_str());
			DEBUG(buf);
		}

		message.state(Type::Message::SENT);
		INFO("  OPPORTUNISTIC packet sent");

		return true;

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "Failed to send OPPORTUNISTIC message: %s", e.what());
		ERROR(buf);
		return false;
	}
}

// Handle delivery proof for DIRECT messages
void LXMRouter::handle_direct_proof(const Bytes& message_hash) {
	char buf[128];
	snprintf(buf, sizeof(buf), "Processing DIRECT delivery proof for message %.16s...", message_hash.toHex().c_str());
	INFO(buf);

	// Use set to avoid calling callback multiple times for same router
	// (router may be registered under multiple keys in registry)
	std::set<LXMRouter*> notified_routers;

	// Call delivered callback for all unique routers
	for (auto& router_entry : _router_registry) {
		LXMRouter* router = router_entry.second;
		if (router && router->_delivered_callback && notified_routers.find(router) == notified_routers.end()) {
			notified_routers.insert(router);
			Bytes empty_hash;
			LXMessage msg(empty_hash, empty_hash);
			msg.hash(message_hash);
			msg.state(Type::Message::DELIVERED);
			router->_delivered_callback(msg);
		}
	}
}

// Link established callback
void LXMRouter::on_link_established(const Link& link) {
	char buf[128];
	snprintf(buf, sizeof(buf), "Link established to %s", link.destination().hash().toHex().c_str());
	INFO(buf);
	// Link is now active, process_outbound() will pick it up
	// Note: Delivery confirmation comes from Resource completed callback
}

// Link closed callback
void LXMRouter::on_link_closed(const Link& link) {
	char buf[128];
	snprintf(buf, sizeof(buf), "Link closed to %s", link.destination().hash().toHex().c_str());
	INFO(buf);

	// Remove from active links
	auto it = _direct_links.find(link.destination().hash());
	if (it != _direct_links.end()) {
		_direct_links.erase(it);
		DEBUG("  Removed link from cache");
	}
}

// Incoming link established callback (for DIRECT delivery to our destination)
void LXMRouter::on_incoming_link_established(Link& link) {
	INFO("Incoming link established from remote peer");
	char buf[128];
	snprintf(buf, sizeof(buf), "  Link ID: %s", link.link_id().toHex().c_str());
	DEBUG(buf);

	// Set up resource concluded callback to receive LXMF messages over this link
	link.set_resource_concluded_callback(static_resource_concluded_callback);
	DEBUG("  Resource callback registered for incoming LXMF messages");
}

// Resource concluded callback (LXMF message received via DIRECT delivery)
void LXMRouter::on_resource_concluded(const RNS::Resource& resource) {
	char buf[128];
	snprintf(buf, sizeof(buf), "Resource concluded, status=%d", (int)resource.status());
	DEBUG(buf);

	if (resource.status() != RNS::Type::Resource::COMPLETE) {
		snprintf(buf, sizeof(buf), "Resource transfer failed with status %d", (int)resource.status());
		WARNING(buf);
		return;
	}

	// Get the resource data - this is the LXMF message
	Bytes data = resource.data();
	snprintf(buf, sizeof(buf), "Received LXMF message via DIRECT delivery (%zu bytes)", data.size());
	INFO(buf);

	try {
		// DIRECT delivery via resource: data is the full packed LXMF message
		// Format: destination_hash + source_hash + signature + payload
		// (same as OPPORTUNISTIC, just delivered via link resource instead of single packet)

		// Unpack the LXMF message
		LXMessage message = LXMessage::unpack_from_bytes(data, Type::Message::DIRECT);

		snprintf(buf, sizeof(buf), "  Message hash: %s", message.hash().toHex().c_str());
		DEBUG(buf);
		snprintf(buf, sizeof(buf), "  Source: %s", message.source_hash().toHex().c_str());
		DEBUG(buf);
		snprintf(buf, sizeof(buf), "  Content size: %zu bytes", message.content().size());
		DEBUG(buf);

		// Verify destination matches
		if (message.destination_hash() != _delivery_destination.hash()) {
			WARNING("Message destination mismatch - ignoring");
			return;
		}

		// Signature validation (same as on_packet)
		if (!message.signature_validated()) {
			WARNING("Message signature not validated");
			snprintf(buf, sizeof(buf), "  Unverified reason: %u", (uint8_t)message.unverified_reason());
			DEBUG(buf);

			// Accept messages with unknown source (signature validated later)
			if (message.unverified_reason() != Type::Message::SOURCE_UNKNOWN) {
				WARNING("  Rejecting message with invalid signature");
				return;
			}
		}

		// Stamp enforcement (if enabled)
		if (_stamp_cost > 0 && _enforce_stamps) {
			if (!message.validate_stamp(_stamp_cost)) {
				snprintf(buf, sizeof(buf), "  Rejecting message with invalid or missing stamp (required cost=%u)", _stamp_cost);
				WARNING(buf);
				return;
			}
			INFO("  Stamp validated");
		}

		// Note: We don't need to send a custom delivery proof here.
		// The sender gets delivery confirmation when the Resource completes
		// (via RESOURCE_PRF from RNS layer), which triggers their callback.

		// Add to inbound queue for processing
		_pending_inbound.push_back(message);
		INFO("  Message queued for delivery");

	} catch (const std::exception& e) {
		snprintf(buf, sizeof(buf), "Failed to process DIRECT message: %s", e.what());
		ERROR(buf);
	}
}

// ============== Propagation Node Support ==============

void LXMRouter::set_propagation_node_manager(PropagationNodeManager* manager) {
	_propagation_manager = manager;
	INFO("Propagation node manager set");
}

void LXMRouter::set_outbound_propagation_node(const Bytes& node_hash) {
	if (node_hash.size() == 0) {
		_outbound_propagation_node = {};
		_outbound_propagation_link = Link(RNS::Type::NONE);
		INFO("Cleared outbound propagation node");
		return;
	}

	// Check if changing to a different node
	if (_outbound_propagation_node != node_hash) {
		// Tear down existing link if any
		if (_outbound_propagation_link && _outbound_propagation_link.status() != RNS::Type::Link::CLOSED) {
			_outbound_propagation_link.teardown();
		}
		_outbound_propagation_link = Link(RNS::Type::NONE);
	}

	_outbound_propagation_node = node_hash;
	char buf[64];
	snprintf(buf, sizeof(buf), "Set outbound propagation node to %.16s...", node_hash.toHex().c_str());
	INFO(buf);
}

void LXMRouter::register_sync_complete_callback(SyncCompleteCallback callback) {
	_sync_complete_callback = callback;
	DEBUG("Sync complete callback registered");
}

// Static callback for outbound propagation resource
void LXMRouter::static_propagation_resource_concluded(const Resource& resource) {
	Bytes resource_hash = resource.hash();
	char buf[128];
	snprintf(buf, sizeof(buf), "Propagation resource concluded: %.16s...", resource_hash.toHex().c_str());
	DEBUG(buf);
	snprintf(buf, sizeof(buf), "  Status: %d", (int)resource.status());
	DEBUG(buf);

	auto it = _pending_propagation_resources.find(resource_hash);
	if (it == _pending_propagation_resources.end()) {
		DEBUG("  Resource not in pending propagation map");
		return;
	}

	Bytes message_hash = it->second;

	if (resource.status() == RNS::Type::Resource::COMPLETE) {
		snprintf(buf, sizeof(buf), "PROPAGATED delivery to node confirmed for message %.16s...", message_hash.toHex().c_str());
		INFO(buf);

		// For PROPAGATED, "delivered" means delivered to propagation node, not final recipient
		// We mark it as SENT (not DELIVERED) to indicate it's on the propagation network
		std::set<LXMRouter*> notified_routers;
		for (auto& router_entry : _router_registry) {
			LXMRouter* router = router_entry.second;
			if (router && router->_sent_callback && notified_routers.find(router) == notified_routers.end()) {
				notified_routers.insert(router);
				Bytes empty_hash;
				LXMessage msg(empty_hash, empty_hash);
				msg.hash(message_hash);
				msg.state(Type::Message::SENT);
				router->_sent_callback(msg);
			}
		}
	} else {
		snprintf(buf, sizeof(buf), "PROPAGATED resource transfer failed with status %d", (int)resource.status());
		WARNING(buf);
	}

	_pending_propagation_resources.erase(it);
}

bool LXMRouter::send_propagated(LXMessage& message) {
	INFO("Sending LXMF message via PROPAGATED delivery");
	char buf[128];

	// Get propagation node
	Bytes prop_node = _outbound_propagation_node;
	if (prop_node.size() == 0 && _propagation_manager) {
		DEBUG("  Looking for propagation node via manager...");
		auto nodes = _propagation_manager->get_nodes();
		snprintf(buf, sizeof(buf), "  Manager has %zu nodes", nodes.size());
		DEBUG(buf);
		prop_node = _propagation_manager->get_effective_node();
	}

	if (prop_node.size() == 0) {
		WARNING("No propagation node available for PROPAGATED delivery");
		return false;
	}

	snprintf(buf, sizeof(buf), "  Using propagation node: %.16s...", prop_node.toHex().c_str());
	DEBUG(buf);

	// Check/establish link to propagation node
	if (!_outbound_propagation_link ||
	    _outbound_propagation_link.status() == RNS::Type::Link::CLOSED) {

		// Check if we have a path
		if (!Transport::has_path(prop_node)) {
			INFO("  No path to propagation node, requesting...");
			Transport::request_path(prop_node);
			return false;  // Will retry next cycle
		}

		// Recall identity for propagation node
		Identity node_identity = Identity::recall(prop_node);
		if (!node_identity) {
			INFO("  Propagation node identity not known, waiting for announce...");
			return false;
		}

		// Create destination for propagation node
		Destination prop_dest(
			node_identity,
			RNS::Type::Destination::OUT,
			RNS::Type::Destination::SINGLE,
			"lxmf",
			"propagation"
		);

		// Create link with established callback
		_outbound_propagation_link = Link(prop_dest);
		INFO("  Establishing link to propagation node...");
		return false;  // Will retry when link established
	}

	// Check if link is active
	if (_outbound_propagation_link.status() != RNS::Type::Link::ACTIVE) {
		DEBUG("  Propagation link not yet active, waiting...");
		return false;  // Will retry
	}

	// Generate propagation stamp if required by node
	if (_propagation_manager) {
		auto node_info = _propagation_manager->get_node(prop_node);
		if (node_info && node_info.stamp_cost > 0) {
			snprintf(buf, sizeof(buf), "  Generating propagation stamp (cost=%u)...", node_info.stamp_cost);
			DEBUG(buf);
			Bytes stamp = message.generate_propagation_stamp(node_info.stamp_cost);
			if (stamp.size() == 0) {
				WARNING("  Failed to generate propagation stamp, sending anyway");
			}
		}
	}

	// Pack message for propagation
	Bytes prop_packed = message.pack_propagated();
	if (!prop_packed || prop_packed.size() == 0) {
		ERROR("  Failed to pack message for propagation");
		return false;
	}

	snprintf(buf, sizeof(buf), "  Propagated message size: %zu bytes", prop_packed.size());
	DEBUG(buf);

	// Send via resource with callback
	Resource resource(prop_packed, _outbound_propagation_link, true, true, static_propagation_resource_concluded);

	// Track this resource
	if (resource.hash()) {
		_pending_propagation_resources[resource.hash()] = message.hash();
		snprintf(buf, sizeof(buf), "  Tracking propagation resource %.16s", resource.hash().toHex().c_str());
		DEBUG(buf);
	}

	message.state(Type::Message::SENDING);
	INFO("  PROPAGATED resource transfer initiated");
	return true;
}

void LXMRouter::request_messages_from_propagation_node() {
	if (_sync_state != PR_IDLE && _sync_state != PR_COMPLETE && _sync_state != PR_FAILED) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Sync already in progress (state=%d)", _sync_state);
		WARNING(buf);
		return;
	}

	// Get propagation node
	Bytes prop_node = _outbound_propagation_node;
	if (!prop_node && _propagation_manager) {
		prop_node = _propagation_manager->get_effective_node();
	}

	if (!prop_node) {
		WARNING("No propagation node available for sync");
		_sync_state = PR_FAILED;
		return;
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "Requesting messages from propagation node %.16s...", prop_node.toHex().c_str());
	INFO(buf);
	_sync_progress = 0.0f;

	// Check if link exists and is active
	if (_outbound_propagation_link && _outbound_propagation_link.status() == RNS::Type::Link::ACTIVE) {
		_sync_state = PR_LINK_ESTABLISHED;

		// TODO: Implement link.identify() and link.request() for full sync protocol
		// For now, we log that sync would happen here
		INFO("  Link active - sync protocol not yet implemented");
		INFO("  (Requires Link.identify() and Link.request() support)");

		_sync_state = PR_COMPLETE;
		_sync_progress = 1.0f;
		if (_sync_complete_callback) {
			_sync_complete_callback(0);
		}
	} else {
		// Need to establish link first
		if (!Transport::has_path(prop_node)) {
			INFO("  No path to propagation node, requesting...");
			Transport::request_path(prop_node);
			_sync_state = PR_PATH_REQUESTED;
		} else {
			Identity node_identity = Identity::recall(prop_node);
			if (!node_identity) {
				INFO("  Propagation node identity not known");
				_sync_state = PR_FAILED;
				return;
			}

			Destination prop_dest(
				node_identity,
				RNS::Type::Destination::OUT,
				RNS::Type::Destination::SINGLE,
				"lxmf",
				"propagation"
			);

			_outbound_propagation_link = Link(prop_dest);
			_sync_state = PR_LINK_ESTABLISHING;
			INFO("  Establishing link for sync...");
		}
	}
}

void LXMRouter::on_message_list_response(const Bytes& response) {
	// TODO: Implement when Link.request() is available
	DEBUG("on_message_list_response: Not yet implemented");
}

void LXMRouter::on_message_get_response(const Bytes& response) {
	// TODO: Implement when Link.request() is available
	DEBUG("on_message_get_response: Not yet implemented");
}

void LXMRouter::process_propagated_lxmf(const Bytes& lxmf_data) {
	// lxmf_data format: dest_hash (16 bytes) + encrypted_content
	if (lxmf_data.size() < Type::Constants::DESTINATION_LENGTH) {
		WARNING("Propagated LXMF data too short");
		return;
	}

	Bytes dest_hash = lxmf_data.left(Type::Constants::DESTINATION_LENGTH);

	// Verify this is for us
	if (dest_hash != _delivery_destination.hash()) {
		DEBUG("Received propagated message not addressed to us");
		return;
	}

	// Decrypt the content
	Bytes encrypted = lxmf_data.mid(Type::Constants::DESTINATION_LENGTH);
	Bytes decrypted = _identity.decrypt(encrypted);

	if (!decrypted || decrypted.size() == 0) {
		WARNING("Failed to decrypt propagated message");
		return;
	}

	// Reconstruct full LXMF data: dest_hash + decrypted
	Bytes full_data;
	full_data << dest_hash << decrypted;

	try {
		LXMessage message = LXMessage::unpack_from_bytes(full_data, Type::Message::PROPAGATED);

		// Check if message was unpacked successfully (has valid hash)
		if (message.hash().size() > 0) {
			// Track transient ID to avoid re-downloading
			Bytes transient_id = Identity::full_hash(lxmf_data);
			_locally_delivered_transient_ids.insert(transient_id);

			// Queue for delivery
			_pending_inbound.push_back(message);
			INFO("Propagated message queued for delivery");
		}
	} catch (const std::exception& e) {
		char buf[128];
		snprintf(buf, sizeof(buf), "Failed to unpack propagated message: %s", e.what());
		ERROR(buf);
	}
}
