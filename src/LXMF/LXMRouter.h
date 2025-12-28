#pragma once

#include "Type.h"
#include "LXMessage.h"
#include "../Bytes.h"
#include "../Identity.h"
#include "../Destination.h"
#include "../Link.h"

#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace LXMF {

	/**
	 * @brief LXMF Router - Message delivery orchestration
	 *
	 * Manages message queues, link establishment, and delivery for LXMF messages.
	 * Supports DIRECT delivery method (via established links) for Phase 1 MVP.
	 *
	 * Usage:
	 *   LXMRouter router(identity, "/path/to/storage");
	 *   router.register_delivery_callback([](LXMessage& msg) {
	 *       // Handle received message
	 *   });
	 *   router.announce();  // Announce delivery destination
	 *
	 *   // Send message
	 *   LXMessage msg(dest, source, content);
	 *   router.handle_outbound(msg);
	 *
	 *   // Process queues periodically
	 *   loop() {
	 *       router.process_outbound();
	 *       router.process_inbound();
	 *   }
	 */
	class LXMRouter {

	public:
		using Ptr = std::shared_ptr<LXMRouter>;

		/**
		 * @brief Callback for message delivery (incoming messages)
		 * @param message The delivered message
		 */
		using DeliveryCallback = std::function<void(LXMessage& message)>;

		/**
		 * @brief Callback for message sent confirmation
		 * @param message The sent message
		 */
		using SentCallback = std::function<void(LXMessage& message)>;

		/**
		 * @brief Callback for message delivery confirmation
		 * @param message The delivered message
		 */
		using DeliveredCallback = std::function<void(LXMessage& message)>;

		/**
		 * @brief Callback for message failure
		 * @param message The failed message
		 */
		using FailedCallback = std::function<void(LXMessage& message)>;

	public:
		/**
		 * @brief Construct LXMF Router
		 *
		 * @param identity Local identity for sending/receiving messages
		 * @param storage_path Path for message persistence (optional)
		 * @param announce_at_start Announce delivery destination on startup (default: false)
		 */
		LXMRouter(
			const RNS::Identity& identity,
			const std::string& storage_path = "",
			bool announce_at_start = false
		);

		~LXMRouter();

	public:
		/**
		 * @brief Register callback for incoming message delivery
		 *
		 * Called when a message is received and validated.
		 *
		 * @param callback Function to call with delivered messages
		 */
		void register_delivery_callback(DeliveryCallback callback);

		/**
		 * @brief Register callback for message sent confirmation
		 *
		 * Called when a message has been sent (packet transmitted or resource started).
		 *
		 * @param callback Function to call when message is sent
		 */
		void register_sent_callback(SentCallback callback);

		/**
		 * @brief Register callback for message delivery confirmation
		 *
		 * Called when remote confirms message delivery.
		 *
		 * @param callback Function to call when message is delivered
		 */
		void register_delivered_callback(DeliveredCallback callback);

		/**
		 * @brief Register callback for message failure
		 *
		 * Called when message sending fails (link timeout, etc).
		 *
		 * @param callback Function to call when message fails
		 */
		void register_failed_callback(FailedCallback callback);

		/**
		 * @brief Queue an outbound message for delivery
		 *
		 * Message will be sent via DIRECT method (link-based delivery).
		 * Link will be established if needed.
		 *
		 * @param message Message to send
		 */
		void handle_outbound(LXMessage& message);

		/**
		 * @brief Process outbound message queue
		 *
		 * Call periodically (e.g., in main loop) to process pending outbound messages.
		 * Establishes links, sends messages, handles retries.
		 */
		void process_outbound();

		/**
		 * @brief Process inbound message queue
		 *
		 * Call periodically to process received messages and invoke delivery callbacks.
		 */
		void process_inbound();

		/**
		 * @brief Announce the delivery destination
		 *
		 * Sends an announce for the LXMF delivery destination so others can
		 * discover this node and send messages to it.
		 *
		 * @param app_data Optional application data to include in announce
		 * @param path_response Optional path response
		 */
		void announce(const RNS::Bytes& app_data = {}, bool path_response = false);

		/**
		 * @brief Set announce interval
		 *
		 * @param interval Seconds between announces (0 = disable auto-announce)
		 */
		void set_announce_interval(uint32_t interval);

		/**
		 * @brief Enable/disable auto-announce on startup
		 *
		 * @param enabled True to announce on startup
		 */
		void set_announce_at_start(bool enabled);

		/**
		 * @brief Get the delivery destination
		 *
		 * @return Destination for receiving LXMF messages
		 */
		inline const RNS::Destination& delivery_destination() const { return _delivery_destination; }

		/**
		 * @brief Get the local identity
		 *
		 * @return Identity used by this router
		 */
		inline const RNS::Identity& identity() const { return _identity; }

		/**
		 * @brief Get pending outbound message count
		 *
		 * @return Number of messages waiting to be sent
		 */
		inline size_t pending_outbound_count() const { return _pending_outbound.size(); }

		/**
		 * @brief Get pending inbound message count
		 *
		 * @return Number of messages waiting to be processed
		 */
		inline size_t pending_inbound_count() const { return _pending_inbound.size(); }

		/**
		 * @brief Get failed outbound message count
		 *
		 * @return Number of failed messages
		 */
		inline size_t failed_outbound_count() const { return _failed_outbound.size(); }

		/**
		 * @brief Clear failed outbound messages
		 */
		void clear_failed_outbound();

		/**
		 * @brief Retry failed outbound messages
		 */
		void retry_failed_outbound();

		/**
		 * @brief Packet callback for receiving LXMF messages
		 *
		 * Called by RNS when a packet is received for the delivery destination.
		 * NOTE: Public for static callback access, not intended for direct use.
		 *
		 * @param data Packet data
		 * @param packet The received packet
		 */
		void on_packet(const RNS::Bytes& data, const RNS::Packet& packet);

		/**
		 * @brief Handle link established callback
		 *
		 * NOTE: Public for static callback access, not intended for direct use.
		 *
		 * @param link The established link
		 */
		void on_link_established(const RNS::Link& link);

		/**
		 * @brief Handle link closed callback
		 *
		 * NOTE: Public for static callback access, not intended for direct use.
		 *
		 * @param link The closed link
		 */
		void on_link_closed(const RNS::Link& link);

	private:
		/**
		 * @brief Establish a link to a destination
		 *
		 * Creates or retrieves an existing link for DIRECT delivery.
		 *
		 * @param destination_hash Hash of destination to link to
		 * @return Link object (or empty if failed)
		 */
		RNS::Link get_link_for_destination(const RNS::Bytes& destination_hash);

		/**
		 * @brief Send message via existing link
		 *
		 * @param message Message to send
		 * @param link Link to send over
		 * @return True if send initiated successfully
		 */
		bool send_via_link(LXMessage& message, RNS::Link& link);

		/**
		 * @brief Send message via OPPORTUNISTIC delivery (single packet)
		 *
		 * @param message Message to send
		 * @param dest_identity Destination identity (for encryption)
		 * @return True if send initiated successfully
		 */
		bool send_opportunistic(LXMessage& message, const RNS::Identity& dest_identity);

	private:
		// Core components
		RNS::Identity _identity;                   // Local identity
		RNS::Destination _delivery_destination;    // For receiving messages
		std::string _storage_path;                 // Storage path for persistence

		// Message queues
		std::vector<LXMessage> _pending_outbound;  // Messages waiting to be sent
		std::vector<LXMessage> _pending_inbound;   // Messages received, waiting for processing
		std::vector<LXMessage> _failed_outbound;   // Messages that failed to send

		// Link management for DIRECT delivery
		std::map<RNS::Bytes, RNS::Link> _direct_links;  // dest_hash -> Link

		// Callbacks
		DeliveryCallback _delivery_callback;
		SentCallback _sent_callback;
		DeliveredCallback _delivered_callback;
		FailedCallback _failed_callback;

		// Announce settings
		uint32_t _announce_interval = 0;           // Seconds (0 = disabled)
		bool _announce_at_start = true;            // Announce on startup
		double _last_announce_time = 0.0;          // Last announce timestamp

		// Internal state
		bool _initialized = false;

		// Retry backoff
		double _next_outbound_process_time = 0.0;  // Next time to process outbound queue
		static constexpr double OUTBOUND_RETRY_DELAY = 5.0;  // Seconds between retries
		static constexpr double PATH_REQUEST_WAIT = 3.0;     // Seconds to wait after path request
	};

}  // namespace LXMF
