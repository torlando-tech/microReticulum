#pragma once

#include "LXMFTypes.h"
#include "LXMessage.h"
#include "../Identity.h"
#include "../Destination.h"
#include "../Link.h"
#include "../Bytes.h"
#include "../Type.h"

#include <memory>
#include <map>
#include <list>
#include <string>
#include <functional>

namespace RNS { namespace LXMF {

/**
 * LXMRouter handles LXMF message routing, delivery, and reception.
 *
 * This class manages:
 * - Registration of delivery destinations
 * - Outbound message queuing and delivery
 * - Incoming message reception and callbacks
 * - Direct delivery over Links
 *
 * Usage:
 *   LXMRouter router;
 *   router.register_delivery_identity(my_identity, "My Display Name");
 *   router.register_delivery_callback(on_message_received);
 *   router.announce();
 *
 *   // In main loop:
 *   router.loop();
 */
class LXMRouter {

public:
	//=========================================================================
	// Configuration Constants (matching Python)
	//=========================================================================

	static constexpr uint8_t MAX_DELIVERY_ATTEMPTS = 5;
	static constexpr double DELIVERY_RETRY_WAIT = 10.0;    // seconds
	static constexpr double PATH_REQUEST_WAIT = 7.0;       // seconds
	static constexpr uint8_t MAX_PATHLESS_TRIES = 1;
	static constexpr double LINK_MAX_INACTIVITY = 600.0;   // seconds
	static constexpr size_t MAX_OUTBOUND_QUEUE = 100;

	//=========================================================================
	// Callbacks
	//=========================================================================

	class Callbacks {
	public:
		/**
		 * Called when a message is received and validated.
		 * @param message The received LXMessage
		 */
		using delivery = void(*)(const LXMessage& message);

		/**
		 * Called when outbound message state changes.
		 * @param message The LXMessage whose state changed
		 */
		using outbound_state = void(*)(const LXMessage& message);
	};

	//=========================================================================
	// Constructors
	//=========================================================================

	LXMRouter();
	virtual ~LXMRouter();

	// Non-copyable (singleton-like behavior)
	LXMRouter(const LXMRouter&) = delete;
	LXMRouter& operator=(const LXMRouter&) = delete;

	//=========================================================================
	// Delivery Destination Management
	//=========================================================================

	/**
	 * Register an identity for receiving LXMF messages.
	 * Creates a delivery destination with app_name="lxmf", aspects="delivery".
	 *
	 * @param identity The identity to use for the delivery destination
	 * @param display_name Optional display name for announcements
	 * @param stamp_cost Optional stamp cost (0-255, 0 = no stamp required)
	 * @return The created delivery destination
	 */
	Destination register_delivery_identity(
		const Identity& identity,
		const std::string& display_name = "",
		uint8_t stamp_cost = 0
	);

	/**
	 * Register a callback to be invoked when messages are delivered.
	 * @param callback Function to call with received messages
	 */
	void register_delivery_callback(Callbacks::delivery callback);

	/**
	 * Get the registered delivery destination.
	 * @return The delivery destination, or invalid destination if not registered
	 */
	const Destination& delivery_destination() const;

	/**
	 * Get the delivery identity.
	 * @return The identity used for delivery, or invalid if not registered
	 */
	const Identity& delivery_identity() const;

	//=========================================================================
	// Announcing
	//=========================================================================

	/**
	 * Announce the delivery destination on the network.
	 * Includes display_name and stamp_cost in the announcement app_data.
	 */
	void announce();

	/**
	 * Set the display name used in announcements.
	 * @param name The display name (UTF-8)
	 */
	void set_display_name(const std::string& name);

	/**
	 * Set the stamp cost for incoming messages.
	 * @param cost Stamp cost (0-255, 0 = no stamp required)
	 */
	void set_stamp_cost(uint8_t cost);

	//=========================================================================
	// Message Sending
	//=========================================================================

	/**
	 * Queue a message for outbound delivery.
	 * The message will be packed if not already packed.
	 *
	 * @param message The message to send
	 * @return true if message was queued successfully
	 */
	bool handle_outbound(LXMessage& message);

	/**
	 * Cancel a pending outbound message.
	 * @param message The message to cancel
	 * @return true if message was found and cancelled
	 */
	bool cancel_outbound(LXMessage& message);

	/**
	 * Get the number of pending outbound messages.
	 */
	size_t pending_outbound_count() const;

	//=========================================================================
	// Processing Loop
	//=========================================================================

	/**
	 * Process pending outbound messages and maintenance tasks.
	 * Should be called regularly from the main loop.
	 *
	 * @param max_messages Maximum messages to process per call (0 = all)
	 */
	void loop(size_t max_messages = 5);

	//=========================================================================
	// Statistics
	//=========================================================================

	/**
	 * Get the number of messages sent since startup.
	 */
	uint32_t messages_sent() const;

	/**
	 * Get the number of messages received since startup.
	 */
	uint32_t messages_received() const;

private:
	//=========================================================================
	// Internal State
	//=========================================================================

	struct State {
		// Delivery destination
		Identity _delivery_identity = {Type::NONE};
		Destination _delivery_destination = {Type::NONE};
		std::string _display_name;
		uint8_t _stamp_cost = 0;

		// Callbacks
		Callbacks::delivery _delivery_callback = nullptr;

		// Outbound queue
		std::list<LXMessage> _pending_outbound;

		// Direct links (destination_hash -> Link)
		std::map<Bytes, Link> _direct_links;

		// Statistics
		uint32_t _messages_sent = 0;
		uint32_t _messages_received = 0;

		// Duplicate cache (message_hash -> receive_time)
		std::map<Bytes, double> _duplicate_cache;
	};

	std::unique_ptr<State> _state;

	//=========================================================================
	// Internal Methods
	//=========================================================================

	/**
	 * Get announce app_data for the delivery destination.
	 * Format: msgpack([display_name, stamp_cost])
	 */
	Bytes get_announce_app_data() const;

	/**
	 * Process a single outbound message.
	 * @return true if message was processed (sent or failed)
	 */
	bool process_outbound_message(LXMessage& message);

	/**
	 * Send a message over an established link.
	 * @param message The message to send
	 * @param link The link to send over
	 * @return true if send was initiated
	 */
	bool send_over_link(LXMessage& message, Link& link);

	/**
	 * Handle incoming packet on delivery destination.
	 */
	static void delivery_packet_callback(const Bytes& data, const Packet& packet);

	/**
	 * Handle link established on delivery destination.
	 */
	static void delivery_link_established_callback(Link& link);

	/**
	 * Process received LXMF data.
	 * @param lxmf_data The raw LXMF bytes
	 * @param method The delivery method used
	 */
	void lxmf_delivery(const Bytes& lxmf_data, DeliveryMethod method);

	/**
	 * Check if a message hash is in the duplicate cache.
	 */
	bool is_duplicate(const Bytes& message_hash) const;

	/**
	 * Add a message hash to the duplicate cache.
	 */
	void add_to_duplicate_cache(const Bytes& message_hash);

	/**
	 * Clean expired entries from the duplicate cache.
	 */
	void clean_duplicate_cache();

	// Static instance for callbacks (since RNS uses C-style function pointers)
	static LXMRouter* _instance;
};

}} // namespace RNS::LXMF
