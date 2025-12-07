#include "LXMessage.h"

#include "../Cryptography/Hashes.h"
#include "../Utilities/OS.h"
#include "../Log.h"

#include <MsgPack.h>

#include <cstring>
#include <sstream>
#include <iomanip>

using namespace RNS;
using namespace RNS::LXMF;

//=============================================================================
// Constructors
//=============================================================================

LXMessage::LXMessage(
	const Destination& destination,
	const Destination& source,
	const Bytes& content,
	const Bytes& title,
	const std::map<uint8_t, Bytes>& fields,
	DeliveryMethod desired_method
) {
	_object = std::make_shared<Object>();
	_object->_destination = destination;
	_object->_source = source;
	_object->_content = content;
	_object->_title = title;
	_object->_fields = fields;
	_object->_desired_method = desired_method;
	_object->_state = MessageState::GENERATING;

	// Extract hashes from destinations
	if (destination) {
		_object->_destination_hash = destination.hash();
	}
	if (source) {
		_object->_source_hash = source.hash();
	}
}

LXMessage LXMessage::create(
	const Destination& destination,
	const Destination& source,
	const std::string& content,
	const std::string& title,
	const std::map<uint8_t, Bytes>& fields,
	DeliveryMethod desired_method
) {
	return LXMessage(
		destination,
		source,
		Bytes(reinterpret_cast<const uint8_t*>(content.data()), content.size()),
		Bytes(reinterpret_cast<const uint8_t*>(title.data()), title.size()),
		fields,
		desired_method
	);
}

//=============================================================================
// Wire Format Operations
//=============================================================================

bool LXMessage::pack() {
	if (!_object) {
		ERROR("LXMessage::pack: No object");
		return false;
	}

	// Don't repack if already packed
	if (_object->_packed) {
		DEBUG("LXMessage::pack: Already packed");
		return true;
	}

	// Set timestamp if not already set
	if (_object->_timestamp == 0) {
		_object->_timestamp = Utilities::OS::time();
	}

	// Build the payload as msgpack array: [timestamp, title, content, fields]
	Bytes packed_payload = pack_payload();

	// Compute hashed_part = dest_hash + src_hash + msgpack(payload)
	Bytes hashed_part;
	hashed_part.append(_object->_destination_hash);
	hashed_part.append(_object->_source_hash);
	hashed_part.append(packed_payload);

	// Compute message hash/ID
	_object->_hash = Identity::full_hash(hashed_part);

	// Sign: signed_part = hashed_part + hash
	Bytes signed_part = hashed_part + _object->_hash;

	// Get signature from source identity
	if (_object->_source) {
		_object->_signature = _object->_source.identity().sign(signed_part);
		_object->_signature_validated = true;
	} else {
		ERROR("LXMessage::pack: No source identity for signing");
		return false;
	}

	// Build packed message: dest_hash + src_hash + signature + payload
	_object->_packed.clear();
	_object->_packed.append(_object->_destination_hash);
	_object->_packed.append(_object->_source_hash);
	_object->_packed.append(_object->_signature);
	_object->_packed.append(packed_payload);

	// Calculate content size for determining delivery method
	size_t content_size = packed_payload.size() - Wire::TIMESTAMP_SIZE - Wire::STRUCT_OVERHEAD;

	// Determine delivery method and representation
	if (_object->_desired_method == DeliveryMethod::UNKNOWN) {
		_object->_desired_method = DeliveryMethod::DIRECT;
	}

	if (_object->_desired_method == DeliveryMethod::OPPORTUNISTIC) {
		// Check if fits in single encrypted packet
		if (content_size <= Wire::ENCRYPTED_PACKET_MAX_CONTENT) {
			_object->_method = DeliveryMethod::OPPORTUNISTIC;
			_object->_representation = Representation::PACKET;
		} else {
			// Fall back to direct
			DEBUG("LXMessage::pack: Message too large for opportunistic, falling back to direct");
			_object->_desired_method = DeliveryMethod::DIRECT;
		}
	}

	if (_object->_desired_method == DeliveryMethod::DIRECT) {
		_object->_method = DeliveryMethod::DIRECT;
		if (content_size <= Wire::LINK_PACKET_MAX_CONTENT) {
			_object->_representation = Representation::PACKET;
		} else {
			_object->_representation = Representation::RESOURCE;
		}
	}

	if (_object->_desired_method == DeliveryMethod::PROPAGATED) {
		_object->_method = DeliveryMethod::PROPAGATED;
		// For propagated, content size determines packet vs resource over link to PN
		if (content_size <= Wire::LINK_PACKET_MAX_CONTENT) {
			_object->_representation = Representation::PACKET;
		} else {
			_object->_representation = Representation::RESOURCE;
		}
	}

	_object->_state = MessageState::OUTBOUND;
	return true;
}

LXMessage LXMessage::unpack_from_opportunistic(
	const Bytes& lxmf_bytes,
	const Destination& destination
) {
	if (!destination) {
		ERROR("LXMessage::unpack_from_opportunistic: Invalid destination");
		return LXMessage{Type::NONE};
	}

	// Reconstruct full LXMF data by prepending destination hash
	Bytes full_lxmf_bytes = destination.hash() + lxmf_bytes;

	return unpack_from_bytes(full_lxmf_bytes, DeliveryMethod::OPPORTUNISTIC);
}

LXMessage LXMessage::unpack_from_bytes(const Bytes& lxmf_bytes, DeliveryMethod original_method) {
	// Minimum size check
	if (lxmf_bytes.size() < Wire::LXMF_OVERHEAD) {
		ERROR("LXMessage::unpack_from_bytes: Data too short");
		return LXMessage{Type::NONE};
	}

	// Extract fixed-length fields
	size_t pos = 0;

	Bytes destination_hash = lxmf_bytes.mid(pos, Wire::DESTINATION_LENGTH);
	pos += Wire::DESTINATION_LENGTH;

	Bytes source_hash = lxmf_bytes.mid(pos, Wire::DESTINATION_LENGTH);
	pos += Wire::DESTINATION_LENGTH;

	Bytes signature = lxmf_bytes.mid(pos, Wire::SIGNATURE_LENGTH);
	pos += Wire::SIGNATURE_LENGTH;

	Bytes packed_payload = lxmf_bytes.mid(pos);

	// Parse the msgpack payload
	double timestamp = 0;
	Bytes title, content, stamp;
	std::map<uint8_t, Bytes> fields;

	if (!parse_msgpack_payload(packed_payload, timestamp, title, content, fields, stamp)) {
		ERROR("LXMessage::unpack_from_bytes: Failed to parse payload");
		return LXMessage{Type::NONE};
	}

	// For hash computation and signature verification, we must use the original packed payload
	// bytes, not re-serialized bytes (which might differ due to encoding variations).
	// However, if the message had a stamp, we need the payload without it.
	// For now, assume no stamp (v1) and use original packed_payload.
	// TODO: Handle stamp stripping for v2

	// Compute message hash using original packed payload
	Bytes hashed_part = destination_hash + source_hash + packed_payload;
	Bytes message_hash = Identity::full_hash(hashed_part);

	// Create message object
	LXMessage message{Type::NONE};
	message._object = std::make_shared<Object>();
	message._object->_destination_hash = destination_hash;
	message._object->_source_hash = source_hash;
	message._object->_signature = signature;
	message._object->_timestamp = timestamp;
	message._object->_title = title;
	message._object->_content = content;
	message._object->_fields = fields;
	message._object->_stamp = stamp;
	message._object->_packed = lxmf_bytes;
	message._object->_packed_payload = packed_payload;  // Store for signature verification
	message._object->_hash = message_hash;
	message._object->_incoming = true;
	message._object->_desired_method = original_method;
	message._object->_method = original_method;

	// Try to recall destination and source identities
	Identity dest_identity = Identity::recall(destination_hash);
	if (dest_identity) {
		message._object->_destination = Destination(
			dest_identity,
			Type::Destination::OUT,
			Type::Destination::SINGLE,
			APP_NAME,
			ASPECT_DELIVERY
		);
	}

	Identity src_identity = Identity::recall(source_hash);
	if (src_identity) {
		message._object->_source = Destination(
			src_identity,
			Type::Destination::OUT,
			Type::Destination::SINGLE,
			APP_NAME,
			ASPECT_DELIVERY
		);
	}

	// Validate signature if we have the source identity
	message.validate_signature();

	return message;
}

const Bytes& LXMessage::packed() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_packed;
}

Bytes LXMessage::packed_opportunistic() const {
	if (!_object || _object->_packed.size() < Wire::DESTINATION_LENGTH) {
		return {};
	}
	// Return packed data without destination hash (first 16 bytes)
	return _object->_packed.mid(Wire::DESTINATION_LENGTH);
}

size_t LXMessage::packed_size() const {
	if (!_object) return 0;
	return _object->_packed.size();
}

//=============================================================================
// Signature Validation
//=============================================================================

bool LXMessage::validate_signature() {
	if (!_object) return false;

	// If already validated, return cached result
	if (_object->_signature_validated) {
		return true;
	}

	// Need source identity to validate
	if (!_object->_source) {
		// Try to recall source identity
		Identity src_identity = Identity::recall(_object->_source_hash);
		if (src_identity) {
			_object->_source = Destination(
				src_identity,
				Type::Destination::OUT,
				Type::Destination::SINGLE,
				APP_NAME,
				ASPECT_DELIVERY
			);
		} else {
			_object->_unverified_reason = UnverifiedReason::SOURCE_UNKNOWN;
			return false;
		}
	}

	// Use stored packed_payload for incoming messages, or repack for outgoing
	// Using original bytes is critical for signature verification to work correctly
	Bytes packed_payload_for_hash;
	if (_object->_packed_payload.size() > 0) {
		// Incoming message - use stored original payload bytes
		packed_payload_for_hash = _object->_packed_payload;
	} else {
		// Outgoing message - repack payload
		packed_payload_for_hash = pack_payload();
	}

	Bytes hashed_part = _object->_destination_hash + _object->_source_hash + packed_payload_for_hash;
	Bytes signed_part = hashed_part + _object->_hash;

	// Validate signature
	try {
		if (_object->_source.identity().validate(_object->_signature, signed_part)) {
			_object->_signature_validated = true;
			_object->_unverified_reason = UnverifiedReason::NONE;
			return true;
		} else {
			_object->_signature_validated = false;
			_object->_unverified_reason = UnverifiedReason::SIGNATURE_INVALID;
			return false;
		}
	} catch (const std::exception& e) {
		ERRORF("LXMessage::validate_signature: Exception: %s", e.what());
		_object->_signature_validated = false;
		_object->_unverified_reason = UnverifiedReason::SIGNATURE_INVALID;
		return false;
	}
}

bool LXMessage::signature_validated() const {
	if (!_object) return false;
	return _object->_signature_validated;
}

UnverifiedReason LXMessage::unverified_reason() const {
	if (!_object) return UnverifiedReason::NONE;
	return _object->_unverified_reason;
}

//=============================================================================
// Getters - Message Identity
//=============================================================================

const Bytes& LXMessage::hash() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_hash;
}

const Bytes& LXMessage::message_id() const {
	return hash();
}

const Bytes& LXMessage::transient_id() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_transient_id;
}

//=============================================================================
// Getters - Content
//=============================================================================

double LXMessage::timestamp() const {
	if (!_object) return 0;
	return _object->_timestamp;
}

const Bytes& LXMessage::destination_hash() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_destination_hash;
}

const Bytes& LXMessage::source_hash() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_source_hash;
}

const Bytes& LXMessage::title() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_title;
}

const Bytes& LXMessage::content() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_content;
}

const std::map<uint8_t, Bytes>& LXMessage::fields() const {
	static std::map<uint8_t, Bytes> empty;
	if (!_object) return empty;
	return _object->_fields;
}

const Bytes& LXMessage::signature() const {
	static Bytes empty;
	if (!_object) return empty;
	return _object->_signature;
}

std::string LXMessage::title_as_string() const {
	if (!_object) return "";
	return std::string(reinterpret_cast<const char*>(_object->_title.data()), _object->_title.size());
}

std::string LXMessage::content_as_string() const {
	if (!_object) return "";
	return std::string(reinterpret_cast<const char*>(_object->_content.data()), _object->_content.size());
}

//=============================================================================
// Getters - State
//=============================================================================

MessageState LXMessage::state() const {
	if (!_object) return MessageState::FAILED;
	return _object->_state;
}

DeliveryMethod LXMessage::method() const {
	if (!_object) return DeliveryMethod::UNKNOWN;
	return _object->_method;
}

DeliveryMethod LXMessage::desired_method() const {
	if (!_object) return DeliveryMethod::UNKNOWN;
	return _object->_desired_method;
}

Representation LXMessage::representation() const {
	if (!_object) return Representation::UNKNOWN;
	return _object->_representation;
}

float LXMessage::progress() const {
	if (!_object) return 0.0f;
	return _object->_progress;
}

uint8_t LXMessage::delivery_attempts() const {
	if (!_object) return 0;
	return _object->_delivery_attempts;
}

bool LXMessage::incoming() const {
	if (!_object) return false;
	return _object->_incoming;
}

//=============================================================================
// Getters - Destinations
//=============================================================================

const Destination& LXMessage::destination() const {
	static Destination empty{Type::NONE};
	if (!_object) return empty;
	return _object->_destination;
}

const Destination& LXMessage::source() const {
	static Destination empty{Type::NONE};
	if (!_object) return empty;
	return _object->_source;
}

//=============================================================================
// Setters
//=============================================================================

void LXMessage::set_state(MessageState state) {
	if (!_object) return;
	_object->_state = state;
	if (_object->_delivery_callback) {
		_object->_delivery_callback(*this, state);
	}
}

void LXMessage::set_progress(float progress) {
	if (!_object) return;
	_object->_progress = progress;
}

void LXMessage::set_delivery_attempts(uint8_t attempts) {
	if (!_object) return;
	_object->_delivery_attempts = attempts;
}

void LXMessage::set_delivery_destination(const Destination& destination) {
	if (!_object) return;
	_object->_delivery_destination = destination;
}

void LXMessage::set_delivery_callback(Callbacks::MessageStateChanged callback) {
	if (!_object) return;
	_object->_delivery_callback = callback;
}

//=============================================================================
// Content Setters
//=============================================================================

void LXMessage::set_title(const Bytes& title) {
	ensure_object();
	_object->_title = title;
}

void LXMessage::set_title(const std::string& title) {
	ensure_object();
	_object->_title = Bytes(reinterpret_cast<const uint8_t*>(title.data()), title.size());
}

void LXMessage::set_content(const Bytes& content) {
	ensure_object();
	_object->_content = content;
}

void LXMessage::set_content(const std::string& content) {
	ensure_object();
	_object->_content = Bytes(reinterpret_cast<const uint8_t*>(content.data()), content.size());
}

void LXMessage::set_fields(const std::map<uint8_t, Bytes>& fields) {
	ensure_object();
	_object->_fields = fields;
}

void LXMessage::set_field(uint8_t field_id, const Bytes& data) {
	ensure_object();
	_object->_fields[field_id] = data;
}

void LXMessage::set_timestamp(double timestamp) {
	ensure_object();
	_object->_timestamp = timestamp;
}

//=============================================================================
// Utility
//=============================================================================

std::string LXMessage::toString() const {
	if (!_object) return "<LXMessage null>";
	if (_object->_hash) {
		return "<LXMessage " + _object->_hash.toHex() + ">";
	}
	return "<LXMessage>";
}

void LXMessage::ensure_object() {
	if (!_object) {
		_object = std::make_shared<Object>();
	}
}

//=============================================================================
// Internal Helpers - Msgpack Operations
//=============================================================================

Bytes LXMessage::pack_payload() const {
	if (!_object) return {};

	MsgPack::Packer packer;

	// Pack as array of 4 elements: [timestamp, title, content, fields]
	packer.pack(MsgPack::arr_size_t(4));

	// 1. Timestamp (float64)
	packer.serialize(_object->_timestamp);

	// 2. Title (binary)
	MsgPack::bin_t<uint8_t> title_bin(_object->_title.data(), _object->_title.data() + _object->_title.size());
	packer.serialize(title_bin);

	// 3. Content (binary)
	MsgPack::bin_t<uint8_t> content_bin(_object->_content.data(), _object->_content.data() + _object->_content.size());
	packer.serialize(content_bin);

	// 4. Fields (map)
	packer.pack(MsgPack::map_size_t(_object->_fields.size()));
	for (const auto& field : _object->_fields) {
		packer.serialize(field.first);
		MsgPack::bin_t<uint8_t> field_bin(field.second.data(), field.second.data() + field.second.size());
		packer.serialize(field_bin);
	}

	return Bytes(packer.data(), packer.size());
}

bool LXMessage::parse_msgpack_payload(
	const Bytes& packed_payload,
	double& timestamp,
	Bytes& title,
	Bytes& content,
	std::map<uint8_t, Bytes>& fields,
	Bytes& stamp
) {
	MsgPack::Unpacker unpacker;
	if (!unpacker.feed(packed_payload.data(), packed_payload.size())) {
		ERROR("LXMessage::parse_msgpack_payload: Failed to feed data");
		return false;
	}

	// Check if it's an array
	if (!unpacker.isArray()) {
		ERROR("LXMessage::parse_msgpack_payload: Expected array");
		return false;
	}

	size_t array_size = unpacker.unpackArraySize();
	if (array_size < 4) {
		ERRORF("LXMessage::parse_msgpack_payload: Array too small: %zu", array_size);
		return false;
	}

	// 1. Timestamp
	if (!unpacker.deserialize(timestamp)) {
		ERROR("LXMessage::parse_msgpack_payload: Failed to unpack timestamp");
		return false;
	}

	// 2. Title
	if (unpacker.isBin()) {
		MsgPack::bin_t<uint8_t> title_bin = unpacker.unpackBinary();
		title = Bytes(title_bin.data(), title_bin.size());
	} else if (unpacker.isStr()) {
		MsgPack::str_t title_str = unpacker.unpackString();
		title = Bytes(reinterpret_cast<const uint8_t*>(title_str.data()), title_str.size());
	} else if (unpacker.isNil()) {
		unpacker.unpackNil();
		title = Bytes();
	} else {
		ERROR("LXMessage::parse_msgpack_payload: Unexpected title type");
		return false;
	}

	// 3. Content
	if (unpacker.isBin()) {
		MsgPack::bin_t<uint8_t> content_bin = unpacker.unpackBinary();
		content = Bytes(content_bin.data(), content_bin.size());
	} else if (unpacker.isStr()) {
		MsgPack::str_t content_str = unpacker.unpackString();
		content = Bytes(reinterpret_cast<const uint8_t*>(content_str.data()), content_str.size());
	} else if (unpacker.isNil()) {
		unpacker.unpackNil();
		content = Bytes();
	} else {
		ERROR("LXMessage::parse_msgpack_payload: Unexpected content type");
		return false;
	}

	// 4. Fields (map)
	fields.clear();
	if (unpacker.isMap()) {
		size_t map_size = unpacker.unpackMapSize();
		for (size_t i = 0; i < map_size; i++) {
			uint8_t key = 0;
			if (!unpacker.deserialize(key)) {
				ERROR("LXMessage::parse_msgpack_payload: Failed to unpack field key");
				return false;
			}

			if (unpacker.isBin()) {
				MsgPack::bin_t<uint8_t> value_bin = unpacker.unpackBinary();
				fields[key] = Bytes(value_bin.data(), value_bin.size());
			} else if (unpacker.isStr()) {
				MsgPack::str_t value_str = unpacker.unpackString();
				fields[key] = Bytes(reinterpret_cast<const uint8_t*>(value_str.data()), value_str.size());
			} else if (unpacker.isNil()) {
				unpacker.unpackNil();
				fields[key] = Bytes();
			} else {
				// Skip unknown types by trying to deserialize as various types
				// This handles nested structures in fields
				DEBUGF("LXMessage::parse_msgpack_payload: Skipping field %02x with complex type", key);
				// Try to consume the element - this is a simplification
				// In a full implementation, we'd need proper type handling
				uint64_t dummy;
				unpacker.deserialize(dummy);
			}
		}
	} else if (unpacker.isNil()) {
		unpacker.unpackNil();
	} else {
		ERROR("LXMessage::parse_msgpack_payload: Expected map for fields");
		return false;
	}

	// 5. Optional stamp (if array has 5 elements)
	stamp = Bytes();
	if (array_size > 4) {
		if (unpacker.isBin()) {
			MsgPack::bin_t<uint8_t> stamp_bin = unpacker.unpackBinary();
			stamp = Bytes(stamp_bin.data(), stamp_bin.size());
		} else if (unpacker.isNil()) {
			unpacker.unpackNil();
		} else {
			DEBUG("LXMessage::parse_msgpack_payload: Unexpected stamp type, ignoring");
		}
	}

	return true;
}
