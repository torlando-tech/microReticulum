#pragma once

#include "Type.h"
#include "LXMessage.h"
#include "../Bytes.h"
#include "../Identity.h"
#include "../Destination.h"
#include "../Link.h"
#include "../Packet.h"

#include <map>
#include <vector>
#include <deque>
#include <set>
#include <string>
#include <functional>
#include <memory>

namespace LXMF {

	// Forward declarations
	class PropagationNodeManager;

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

		/**
		 * @brief Callback for sync completion
		 * @param messages_received Number of messages received from propagation node
		 */
		using SyncCompleteCallback = std::function<void(size_t messages_received)>;

		/**
		 * @brief Propagation sync state
		 */
		enum PropagationSyncState : uint8_t {
			PR_IDLE              = 0x00,
			PR_PATH_REQUESTED    = 0x01,
			PR_LINK_ESTABLISHING = 0x02,
			PR_LINK_ESTABLISHED  = 0x03,
			PR_REQUEST_SENT      = 0x04,
			PR_RECEIVING         = 0x05,
			PR_COMPLETE          = 0x06,
			PR_FAILED            = 0x07
		};

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
		 * @brief Set display name for announces
		 *
		 * @param name Display name to include in announces
		 */
		void set_display_name(const std::string& name);

		/**
		 * @brief Get current display name
		 *
		 * @return Display name string
		 */
		inline const std::string& display_name() const { return _display_name; }

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

		// ============== Propagation Node Support ==============

		/**
		 * @brief Set the propagation node manager
		 *
		 * @param manager Pointer to PropagationNodeManager (not owned)
		 */
		void set_propagation_node_manager(PropagationNodeManager* manager);

		/**
		 * @brief Set the outbound propagation node
		 *
		 * @param node_hash Destination hash of the propagation node
		 */
		void set_outbound_propagation_node(const RNS::Bytes& node_hash);

		/**
		 * @brief Get the current outbound propagation node
		 *
		 * @return Destination hash of the propagation node (or empty)
		 */
		RNS::Bytes get_outbound_propagation_node() const { return _outbound_propagation_node; }

		/**
		 * @brief Enable/disable fallback to PROPAGATED delivery
		 *
		 * When enabled, messages that fail DIRECT/OPPORTUNISTIC delivery will
		 * automatically be retried via a propagation node.
		 *
		 * @param enabled True to enable fallback
		 */
		void set_fallback_to_propagation(bool enabled) { _fallback_to_propagation = enabled; }

		/**
		 * @brief Check if fallback to propagation is enabled
		 *
		 * @return True if fallback is enabled
		 */
		bool fallback_to_propagation() const { return _fallback_to_propagation; }

		/**
		 * @brief Enable/disable propagation-only mode
		 *
		 * When enabled, all messages are sent via propagation node only.
		 * DIRECT and OPPORTUNISTIC delivery are skipped.
		 *
		 * @param enabled True to enable propagation-only mode
		 */
		void set_propagation_only(bool enabled) { _propagation_only = enabled; }

		/**
		 * @brief Check if propagation-only mode is enabled
		 *
		 * @return True if propagation-only mode is enabled
		 */
		bool propagation_only() const { return _propagation_only; }

		/**
		 * @brief Request messages from the propagation node
		 *
		 * Initiates a sync with the configured/selected propagation node to
		 * retrieve any pending messages.
		 */
		void request_messages_from_propagation_node();

		/**
		 * @brief Get the current sync state
		 *
		 * @return Current propagation sync state
		 */
		PropagationSyncState get_sync_state() const { return _sync_state; }

		/**
		 * @brief Get the current sync progress
		 *
		 * @return Progress from 0.0 to 1.0
		 */
		float get_sync_progress() const { return _sync_progress; }

		/**
		 * @brief Register callback for sync completion
		 *
		 * @param callback Function to call when sync completes
		 */
		void register_sync_complete_callback(SyncCompleteCallback callback);

		// ============== End Propagation Node Support ==============

		// ============== Stamp Enforcement ==============

		/**
		 * @brief Set the required stamp cost for incoming messages
		 *
		 * Messages without a valid stamp meeting this cost will be rejected
		 * when enforcement is enabled.
		 *
		 * @param cost Required number of leading zero bits (0 = no stamp required)
		 */
		void set_stamp_cost(uint8_t cost) { _stamp_cost = cost; }

		/**
		 * @brief Get the current stamp cost requirement
		 *
		 * @return Required stamp cost
		 */
		uint8_t stamp_cost() const { return _stamp_cost; }

		/**
		 * @brief Enable stamp enforcement
		 *
		 * When enabled, incoming messages must have a valid stamp meeting
		 * the configured cost or they will be dropped.
		 */
		void enforce_stamps() { _enforce_stamps = true; }

		/**
		 * @brief Disable stamp enforcement
		 *
		 * Incoming messages will be accepted regardless of stamp validity.
		 */
		void ignore_stamps() { _enforce_stamps = false; }

		/**
		 * @brief Check if stamp enforcement is enabled
		 *
		 * @return True if stamps are enforced
		 */
		bool stamps_enforced() const { return _enforce_stamps; }

		// ============== End Stamp Enforcement ==============

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

		/**
		 * @brief Handle incoming link established to our delivery destination
		 *
		 * Called when a remote peer establishes a link to send us DIRECT messages.
		 * Sets up resource callbacks to receive LXMF messages.
		 *
		 * @param link The incoming link (non-const to set callbacks)
		 */
		void on_incoming_link_established(RNS::Link& link);

		/**
		 * @brief Handle resource concluded on incoming link
		 *
		 * Called when an LXMF message resource transfer completes.
		 * Unpacks and queues the message for delivery.
		 *
		 * @param resource The completed resource containing LXMF message
		 */
		void on_resource_concluded(const RNS::Resource& resource);

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

		/**
		 * @brief Send message via PROPAGATED delivery
		 *
		 * @param message Message to send
		 * @return True if send initiated successfully
		 */
		bool send_propagated(LXMessage& message);

		/**
		 * @brief Handle message list response from propagation node
		 */
		void on_message_list_response(const RNS::Bytes& response);

		/**
		 * @brief Handle message get response from propagation node
		 */
		void on_message_get_response(const RNS::Bytes& response);

		/**
		 * @brief Process received propagated LXMF data
		 */
		void process_propagated_lxmf(const RNS::Bytes& lxmf_data);

		/**
		 * @brief Static callback for outbound propagation resource
		 */
		static void static_propagation_resource_concluded(const RNS::Resource& resource);

	private:
		// Core components
		RNS::Identity _identity;                   // Local identity
		RNS::Destination _delivery_destination;    // For receiving messages
		std::string _storage_path;                 // Storage path for persistence

		// Message queues (deque for efficient front/back operations)
		std::deque<LXMessage> _pending_outbound;  // Messages waiting to be sent
		std::deque<LXMessage> _pending_inbound;   // Messages received, waiting for processing
		std::deque<LXMessage> _failed_outbound;   // Messages that failed to send

		// Link management for DIRECT delivery
		std::map<RNS::Bytes, RNS::Link> _direct_links;  // dest_hash -> Link
		std::map<RNS::Bytes, double> _link_creation_times;  // dest_hash -> creation timestamp
		static constexpr double LINK_ESTABLISHMENT_TIMEOUT = 30.0;  // Seconds to wait for pending links

		// Proof tracking for delivery confirmation
		// Maps packet hash -> message hash so we can update message state when proof arrives
		static std::map<RNS::Bytes, RNS::Bytes> _pending_proofs;  // packet_hash -> message_hash
		static void static_proof_callback(const RNS::PacketReceipt& receipt);

	public:
		// Handle delivery proof for DIRECT messages (called from link packet callback)
		static void handle_direct_proof(const RNS::Bytes& message_hash);

	private:

		// Callbacks
		DeliveryCallback _delivery_callback;
		SentCallback _sent_callback;
		DeliveredCallback _delivered_callback;
		FailedCallback _failed_callback;

		// Announce settings
		uint32_t _announce_interval = 0;           // Seconds (0 = disabled)
		bool _announce_at_start = true;            // Announce on startup
		double _last_announce_time = 0.0;          // Last announce timestamp
		std::string _display_name;                 // Display name for announces

		// Internal state
		bool _initialized = false;

		// Retry backoff
		double _next_outbound_process_time = 0.0;  // Next time to process outbound queue
		static constexpr double OUTBOUND_RETRY_DELAY = 5.0;  // Seconds between retries
		static constexpr double PATH_REQUEST_WAIT = 3.0;     // Seconds to wait after path request

		// Propagation node support
		PropagationNodeManager* _propagation_manager = nullptr;
		RNS::Bytes _outbound_propagation_node;
		RNS::Link _outbound_propagation_link{RNS::Type::NONE};
		bool _fallback_to_propagation = true;
		bool _propagation_only = false;

		// Propagation sync state
		PropagationSyncState _sync_state = PR_IDLE;
		float _sync_progress = 0.0f;
		std::set<RNS::Bytes> _locally_delivered_transient_ids;
		SyncCompleteCallback _sync_complete_callback;

		// Track outbound propagation resources (resource_hash -> message_hash)
		static std::map<RNS::Bytes, RNS::Bytes> _pending_propagation_resources;

		// Stamp enforcement
		uint8_t _stamp_cost = 0;       // Required stamp cost (0 = no stamp required)
		bool _enforce_stamps = false;  // Whether to enforce stamp requirements
	};

}  // namespace LXMF
