#pragma once

#include "LXMFTypes.h"
#include "../Bytes.h"
#include "../Type.h"
#include "../Identity.h"
#include "../Link.h"
#include "../Destination.h"

#include <memory>
#include <map>
#include <string>

namespace RNS { namespace LXMF {

/**
 * LXMessage represents an LXMF message that can be serialized and transmitted
 * over the Reticulum network. It supports multiple delivery methods and provides
 * wire-format compatibility with Python LXMF.
 *
 * Wire Format:
 *   [dest_hash: 16][src_hash: 16][signature: 64][msgpack(payload)]
 *
 * Payload (msgpack array):
 *   [timestamp: float64, title: bin, content: bin, fields: map, stamp?: bin]
 *
 * Message ID = SHA256(dest_hash + src_hash + msgpack(payload))
 * Signature = Ed25519(dest_hash + src_hash + msgpack(payload) + message_id)
 */
class LXMessage {

public:
	//=========================================================================
	// Constructors (following microReticulum patterns)
	//=========================================================================

	/**
	 * Create an empty/null message (for use with Type::NONE pattern)
	 */
	LXMessage(Type::NoneConstructor none) {
		// Empty - _object remains null
	}

	/**
	 * Copy constructor - shares underlying object via shared_ptr
	 */
	LXMessage(const LXMessage& message) : _object(message._object) {
	}

	/**
	 * Create an outbound message with Bytes content
	 *
	 * @param destination Target destination for the message
	 * @param source Source destination (identity of sender)
	 * @param content Message content as bytes
	 * @param title Optional message title as bytes
	 * @param fields Optional field dictionary for attachments/metadata
	 * @param desired_method Preferred delivery method (default: DIRECT)
	 */
	LXMessage(
		const Destination& destination,
		const Destination& source,
		const Bytes& content,
		const Bytes& title,
		const std::map<uint8_t, Bytes>& fields = {},
		DeliveryMethod desired_method = DeliveryMethod::DIRECT
	);

	/**
	 * Create an outbound message with string content.
	 * This is the preferred constructor for text messages.
	 */
	static LXMessage create(
		const Destination& destination,
		const Destination& source,
		const std::string& content,
		const std::string& title = "",
		const std::map<uint8_t, Bytes>& fields = {},
		DeliveryMethod desired_method = DeliveryMethod::DIRECT
	);

	virtual ~LXMessage() = default;

	//=========================================================================
	// Operators
	//=========================================================================

	LXMessage& operator=(const LXMessage& message) {
		_object = message._object;
		return *this;
	}

	operator bool() const {
		return _object.get() != nullptr;
	}

	bool operator<(const LXMessage& message) const {
		return _object.get() < message._object.get();
	}

	bool operator==(const LXMessage& message) const {
		return _object.get() == message._object.get();
	}

	//=========================================================================
	// Wire Format Operations (Python interop critical)
	//=========================================================================

	/**
	 * Pack the message into wire format for transmission.
	 * Sets the hash, message_id, signature, and packed representation.
	 *
	 * @return true if packing succeeded
	 */
	bool pack();

	/**
	 * Unpack a message from wire format bytes.
	 * Static factory method that creates a new LXMessage from received data.
	 *
	 * @param lxmf_bytes Raw LXMF message bytes
	 * @param original_method The delivery method the message was received via
	 * @return LXMessage object (check with bool operator for validity)
	 */
	static LXMessage unpack_from_bytes(
		const Bytes& lxmf_bytes,
		DeliveryMethod original_method = DeliveryMethod::UNKNOWN
	);

	/**
	 * Unpack a message from opportunistic format (no destination hash).
	 * For opportunistic delivery, the destination hash is inferred from
	 * the packet destination, so it's not included in the wire data.
	 *
	 * Wire format: [src_hash: 16][signature: 64][msgpack(payload)]
	 * (destination hash is prepended internally from the destination parameter)
	 *
	 * @param lxmf_bytes LXMF message bytes without destination hash
	 * @param destination The destination that received the packet
	 * @return LXMessage object (check with bool operator for validity)
	 */
	static LXMessage unpack_from_opportunistic(
		const Bytes& lxmf_bytes,
		const Destination& destination
	);

	/**
	 * Get the packed wire format representation.
	 * Message must be pack()'ed first.
	 */
	const Bytes& packed() const;

	/**
	 * Get the packed format for opportunistic delivery (no dest_hash).
	 * Format: [src_hash: 16][signature: 64][msgpack(payload)]
	 * Message must be pack()'ed first.
	 */
	Bytes packed_opportunistic() const;

	/**
	 * Get the size of the packed representation.
	 */
	size_t packed_size() const;

	//=========================================================================
	// Signature Validation
	//=========================================================================

	/**
	 * Validate the message signature using the source identity.
	 * For incoming messages, attempts to recall the source identity.
	 *
	 * @return true if signature is valid
	 */
	bool validate_signature();

	/**
	 * Check if the signature has been validated.
	 */
	bool signature_validated() const;

	/**
	 * Get the reason why the signature could not be verified.
	 */
	UnverifiedReason unverified_reason() const;

	//=========================================================================
	// Getters - Message Identity
	//=========================================================================

	/**
	 * Get the message hash (SHA256 of dest+src+payload).
	 * Only valid after pack() or unpack_from_bytes().
	 */
	const Bytes& hash() const;

	/**
	 * Get the message ID (same as hash for non-propagated messages).
	 */
	const Bytes& message_id() const;

	/**
	 * Get the transient ID (used for propagation node deduplication).
	 * Only set for PROPAGATED method messages.
	 */
	const Bytes& transient_id() const;

	//=========================================================================
	// Getters - Content
	//=========================================================================

	double timestamp() const;
	const Bytes& destination_hash() const;
	const Bytes& source_hash() const;
	const Bytes& title() const;
	const Bytes& content() const;
	const std::map<uint8_t, Bytes>& fields() const;
	const Bytes& signature() const;

	/**
	 * Get the title as a UTF-8 string.
	 */
	std::string title_as_string() const;

	/**
	 * Get the content as a UTF-8 string.
	 */
	std::string content_as_string() const;

	//=========================================================================
	// Getters - State
	//=========================================================================

	MessageState state() const;
	DeliveryMethod method() const;
	DeliveryMethod desired_method() const;
	Representation representation() const;
	float progress() const;
	uint8_t delivery_attempts() const;
	bool incoming() const;

	//=========================================================================
	// Getters - Destinations
	//=========================================================================

	const Destination& destination() const;
	const Destination& source() const;

	//=========================================================================
	// Setters
	//=========================================================================

	void set_state(MessageState state);
	void set_progress(float progress);
	void set_delivery_attempts(uint8_t attempts);
	void set_delivery_destination(const Destination& destination);

	//=========================================================================
	// Callbacks
	//=========================================================================

	void set_delivery_callback(Callbacks::MessageStateChanged callback);

	//=========================================================================
	// Content Setters (for building messages)
	//=========================================================================

	void set_title(const Bytes& title);
	void set_title(const std::string& title);
	void set_content(const Bytes& content);
	void set_content(const std::string& content);
	void set_fields(const std::map<uint8_t, Bytes>& fields);
	void set_field(uint8_t field_id, const Bytes& data);
	void set_timestamp(double timestamp);

	//=========================================================================
	// Utility
	//=========================================================================

	std::string toString() const;

private:
	/**
	 * Internal data object (Pimpl pattern)
	 */
	class Object {
	public:
		// Destinations
		Destination _destination = {Type::NONE};
		Destination _source = {Type::NONE};
		Bytes _destination_hash;
		Bytes _source_hash;

		// Content
		Bytes _title;
		Bytes _content;
		std::map<uint8_t, Bytes> _fields;

		// Wire format
		Bytes _packed;
		Bytes _packed_payload;  // Original msgpack payload (for signature verification)
		Bytes _signature;
		Bytes _hash;
		Bytes _transient_id;
		double _timestamp = 0;

		// State machine
		MessageState _state = MessageState::GENERATING;
		DeliveryMethod _method = DeliveryMethod::UNKNOWN;
		DeliveryMethod _desired_method = DeliveryMethod::DIRECT;
		Representation _representation = Representation::UNKNOWN;
		float _progress = 0.0f;
		uint8_t _delivery_attempts = 0;

		// Delivery
		Destination _delivery_destination = {Type::NONE};
		Callbacks::MessageStateChanged _delivery_callback = nullptr;

		// Incoming message flags
		bool _incoming = false;
		bool _signature_validated = false;
		UnverifiedReason _unverified_reason = UnverifiedReason::NONE;

		// Stamp (deferred for v1)
		Bytes _stamp;

		// Radio quality metrics (for received messages)
		float _rssi = 0.0f;
		float _snr = 0.0f;
		float _q = 0.0f;
	};

	std::shared_ptr<Object> _object;

	//=========================================================================
	// Internal Helpers
	//=========================================================================

	/**
	 * Create the internal Object if it doesn't exist.
	 */
	void ensure_object();

	/**
	 * Pack a msgpack binary (bin format).
	 */
	static void pack_msgpack_bin(Bytes& output, const Bytes& data);

	/**
	 * Pack a msgpack map of fields.
	 */
	static void pack_msgpack_map(Bytes& output, const std::map<uint8_t, Bytes>& fields);

	/**
	 * Pack timestamp as msgpack float64.
	 */
	static void pack_msgpack_timestamp(Bytes& output, double timestamp);

	/**
	 * Parse the msgpack payload into components.
	 *
	 * @param packed_payload The msgpack-encoded payload
	 * @param timestamp Output: message timestamp
	 * @param title Output: title bytes
	 * @param content Output: content bytes
	 * @param fields Output: field map
	 * @param stamp Output: stamp bytes (if present)
	 * @return true if parsing succeeded
	 */
	static bool parse_msgpack_payload(
		const Bytes& packed_payload,
		double& timestamp,
		Bytes& title,
		Bytes& content,
		std::map<uint8_t, Bytes>& fields,
		Bytes& stamp
	);

	/**
	 * Pack the payload portion (without recalculating hash if already set).
	 * Used internally during initial pack().
	 */
	Bytes pack_payload() const;
};

}} // namespace RNS::LXMF
