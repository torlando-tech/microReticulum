#include "LXMessage.h"
#include "../Log.h"
#include "../Utilities/OS.h"
#include "../Packet.h"
#include "../Resource.h"

#include <MsgPack.h>

using namespace LXMF;
using namespace RNS;

// Constructor with destination and source objects
LXMessage::LXMessage(
	const Destination& destination,
	const Destination& source,
	const Bytes& content,
	const Bytes& title,
	const std::map<Bytes, Bytes>& fields,
	Type::Message::Method desired_method
) :
	_destination(destination),
	_source(source),
	_content(content),
	_title(title),
	_fields(fields),
	_desired_method(desired_method),
	_method(desired_method)
{
	// Extract hashes from destination objects
	if (destination) {
		_destination_hash = destination.hash();
	}
	if (source) {
		_source_hash = source.hash();
	}

	INFO("Created new LXMF message");
	DEBUG("  Destination: " + _destination_hash.toHex());
	DEBUG("  Source: " + _source_hash.toHex());
	DEBUG("  Content size: " + std::to_string(_content.size()) + " bytes");
}

// Constructor with hashes only (for unpacking)
LXMessage::LXMessage(
	const Bytes& destination_hash,
	const Bytes& source_hash,
	const Bytes& content,
	const Bytes& title,
	const std::map<Bytes, Bytes>& fields,
	Type::Message::Method desired_method
) :
	_destination(RNS::Type::NONE),
	_source(RNS::Type::NONE),
	_destination_hash(destination_hash),
	_source_hash(source_hash),
	_content(content),
	_title(title),
	_fields(fields),
	_desired_method(desired_method),
	_method(desired_method)
{
	DEBUG("Created LXMF message from hashes");
}

LXMessage::~LXMessage() {
	TRACE("LXMessage destroyed");
}

// Pack the message into binary format
const Bytes& LXMessage::pack() {
	if (_packed_valid) {
		return _packed;
	}

	INFO("Packing LXMF message");

	// 1. Set timestamp if not already set
	if (_timestamp == 0.0) {
		_timestamp = Utilities::OS::time();
	}

	// 2. Create payload array: [timestamp, title, content, fields] - matches Python LXMF exactly
	MsgPack::Packer packer;
	packer.serialize(_timestamp);
	packer.serialize(_title);
	packer.serialize(_content);

	// Serialize fields as msgpack map
	packer.serialize((uint32_t)_fields.size());
	for (const auto& field : _fields) {
		packer.serialize(field.first);
		packer.serialize(field.second);
	}

	Bytes packed_payload(packer.data(), packer.size());

	// 3. Calculate hash: SHA256(dest_hash + source_hash + packed_payload)
	Bytes hashed_part;
	hashed_part << _destination_hash;
	hashed_part << _source_hash;
	hashed_part << packed_payload;

	_hash = Identity::full_hash(hashed_part);

	DEBUG("  Message hash: " + _hash.toHex());

	// 4. Create signed part: hashed_part + hash
	Bytes signed_part;
	signed_part << hashed_part;
	signed_part << _hash;

	// 5. Sign with source identity
	if (_source) {
		_signature = _source.sign(signed_part);
		_signature_validated = true;
		DEBUG("  Message signed (" + std::to_string(_signature.size()) + " bytes)");
	} else {
		ERROR("Cannot sign message - source destination not available");
		throw std::runtime_error("Cannot sign message without source destination");
	}

	// 6. Pack final message: dest_hash + source_hash + signature + packed_payload
	_packed.clear();
	_packed << _destination_hash;
	_packed << _source_hash;
	_packed << _signature;
	_packed << packed_payload;

	_packed_valid = true;

	// 7. Determine delivery method and representation
	size_t content_size = packed_payload.size() - Type::Constants::TIMESTAMP_SIZE - Type::Constants::STRUCT_OVERHEAD;

	// For Phase 1 MVP, we only support DIRECT delivery
	if (_desired_method == Type::Message::DIRECT) {
		if (content_size <= Type::Constants::LINK_PACKET_MAX_CONTENT) {
			_method = Type::Message::DIRECT;
			_representation = Type::Message::PACKET;
			INFO("  Message will be sent as single packet (" + std::to_string(_packed.size()) + " bytes)");
		} else {
			_method = Type::Message::DIRECT;
			_representation = Type::Message::RESOURCE;
			INFO("  Message will be sent as resource (" + std::to_string(_packed.size()) + " bytes)");
		}
	} else {
		WARNING("Only DIRECT delivery method is supported in Phase 1 MVP");
		_method = Type::Message::DIRECT;
		_representation = Type::Message::PACKET;
	}

	_state = Type::Message::OUTBOUND;

	INFO("Message packed successfully (" + std::to_string(_packed.size()) + " bytes total)");
	DEBUG("  Overhead: " + std::to_string(Type::Constants::LXMF_OVERHEAD) + " bytes");
	DEBUG("  Payload: " + std::to_string(packed_payload.size()) + " bytes");

	return _packed;
}

// Unpack an LXMF message from bytes
LXMessage LXMessage::unpack_from_bytes(const Bytes& lxmf_bytes, Type::Message::Method original_method) {
	INFO("Unpacking LXMF message from " + std::to_string(lxmf_bytes.size()) + " bytes");

	// 1. Extract fixed-size fields
	if (lxmf_bytes.size() < 2 * Type::Constants::DESTINATION_LENGTH + Type::Constants::SIGNATURE_LENGTH) {
		throw std::runtime_error("LXMF message too short");
	}

	size_t offset = 0;

	Bytes destination_hash = lxmf_bytes.mid(offset, Type::Constants::DESTINATION_LENGTH);
	offset += Type::Constants::DESTINATION_LENGTH;

	Bytes source_hash = lxmf_bytes.mid(offset, Type::Constants::DESTINATION_LENGTH);
	offset += Type::Constants::DESTINATION_LENGTH;

	Bytes signature = lxmf_bytes.mid(offset, Type::Constants::SIGNATURE_LENGTH);
	offset += Type::Constants::SIGNATURE_LENGTH;

	Bytes packed_payload = lxmf_bytes.mid(offset);

	DEBUG("  Destination hash: " + destination_hash.toHex());
	DEBUG("  Source hash: " + source_hash.toHex());
	DEBUG("  Signature: " + std::to_string(signature.size()) + " bytes");
	DEBUG("  Payload: " + std::to_string(packed_payload.size()) + " bytes");

	// 2. Unpack payload: [timestamp, title, content, fields] - matches Python LXMF exactly
	MsgPack::Unpacker unpacker;
	unpacker.feed(packed_payload.data(), packed_payload.size());

	double timestamp = 0.0;
	Bytes title;
	Bytes content;
	std::map<Bytes, Bytes> fields;

	try {
		MsgPack::bin_t<uint8_t> title_bin;
		MsgPack::bin_t<uint8_t> content_bin;
		uint32_t num_fields = 0;

		// Unpack timestamp
		unpacker.deserialize(timestamp);

		// Unpack title (as binary) - Python's 2nd field
		unpacker.deserialize(title_bin);
		title = Bytes(title_bin);

		// Unpack content (as binary) - Python's 3rd field
		unpacker.deserialize(content_bin);
		content = Bytes(content_bin);

		// Unpack number of fields
		unpacker.deserialize(num_fields);

		// Unpack each field (key-value pairs)
		for (uint32_t i = 0; i < num_fields; ++i) {
			MsgPack::bin_t<uint8_t> key_bin;
			MsgPack::bin_t<uint8_t> value_bin;

			unpacker.deserialize(key_bin);
			unpacker.deserialize(value_bin);

			Bytes key(key_bin);
			Bytes value(value_bin);
			fields[key] = value;
		}

	} catch (const std::exception& e) {
		ERROR("Failed to unpack LXMF message payload: " + std::string(e.what()));
		throw;
	}

	DEBUG("  Timestamp: " + std::to_string(timestamp));
	DEBUG("  Title size: " + std::to_string(title.size()) + " bytes");
	DEBUG("  Content size: " + std::to_string(content.size()) + " bytes");
	DEBUG("  Fields: " + std::to_string(fields.size()));

	// 3. Create message object
	LXMessage message(destination_hash, source_hash, content, title, fields, original_method);
	message._timestamp = timestamp;
	message._signature = signature;
	message._packed = lxmf_bytes;
	message._packed_valid = true;
	message._incoming = true;
	message._state = Type::Message::DELIVERED;

	// 4. Calculate hash for verification
	Bytes hashed_part;
	hashed_part << destination_hash;
	hashed_part << source_hash;
	hashed_part << packed_payload;

	message._hash = Identity::full_hash(hashed_part);

	DEBUG("  Calculated hash: " + message._hash.toHex());

	// 5. Try to validate signature
	// Check if we have the source identity cached
	Identity source_identity = Identity::recall(source_hash);
	if (source_identity) {
		INFO("  Source identity found in cache, validating signature");
		message._source = Destination(source_identity, RNS::Type::Destination::OUT, RNS::Type::Destination::SINGLE, "lxmf", "delivery");

		// Validate signature
		Bytes signed_part;
		signed_part << hashed_part;
		signed_part << message._hash;

		if (source_identity.validate(signature, signed_part)) {
			message._signature_validated = true;
			INFO("  Signature validated successfully");
		} else {
			message._signature_validated = false;
			message._unverified_reason = Type::Message::SIGNATURE_INVALID;
			WARNING("  Signature validation failed!");
		}
	} else {
		message._signature_validated = false;
		message._unverified_reason = Type::Message::SOURCE_UNKNOWN;
		DEBUG("  Source identity unknown, signature not validated");
	}

	// Similarly try to get destination identity
	Identity dest_identity = Identity::recall(destination_hash);
	if (dest_identity) {
		message._destination = Destination(dest_identity, RNS::Type::Destination::OUT, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
	}

	INFO("Message unpacked successfully");
	return message;
}

// Validate the message signature
bool LXMessage::validate_signature() {
	if (_signature_validated) {
		return true;
	}

	INFO("Validating message signature");

	// Try to get source identity if not already available
	if (!_source) {
		Identity source_identity = Identity::recall(_source_hash);
		if (source_identity) {
			_source = Destination(source_identity, RNS::Type::Destination::OUT, RNS::Type::Destination::SINGLE, "lxmf", "delivery");
		} else {
			_unverified_reason = Type::Message::SOURCE_UNKNOWN;
			WARNING("Cannot validate signature - source identity unknown");
			return false;
		}
	}

	// Reconstruct signed part
	Bytes hashed_part;
	hashed_part << _destination_hash;
	hashed_part << _source_hash;

	// Need to repack payload for hashed_part
	MsgPack::Packer packer;
	packer.serialize(_timestamp);
	packer.serialize(_title);
	packer.serialize(_content);
	packer.serialize((uint32_t)_fields.size());
	for (const auto& field : _fields) {
		packer.serialize(field.first);
		packer.serialize(field.second);
	}
	Bytes packed_payload(packer.data(), packer.size());
	hashed_part << packed_payload;

	Bytes signed_part;
	signed_part << hashed_part;
	signed_part << _hash;

	// Validate signature
	if (_source.identity().validate(_signature, signed_part)) {
		_signature_validated = true;
		INFO("Signature validated successfully");
		return true;
	} else {
		_signature_validated = false;
		_unverified_reason = Type::Message::SIGNATURE_INVALID;
		WARNING("Signature validation failed");
		return false;
	}
}

// Send the message via a link
bool LXMessage::send_via_link(const Link& link) {
	INFO("Sending LXMF message via link");

	// Ensure message is packed
	if (!_packed_valid) {
		pack();
	}

	// Check that link is active
	if (!link || link.status() != RNS::Type::Link::ACTIVE) {
		ERROR("Cannot send message - link is not active");
		return false;
	}

	_state = Type::Message::SENDING;

	try {
		if (_representation == Type::Message::PACKET) {
			// Send as single packet over link
			INFO("  Sending as single packet (" + std::to_string(_packed.size()) + " bytes)");

			Packet packet(link, _packed);
			packet.send();

			_state = Type::Message::SENT;
			INFO("Message sent successfully as packet");
			return true;

		} else if (_representation == Type::Message::RESOURCE) {
			// Send as resource over link
			INFO("  Sending as resource (" + std::to_string(_packed.size()) + " bytes)");

			// TODO: Implement resource transfer with callbacks
			// For now, we'll create the resource but won't set callbacks
			Resource resource(_packed, link);

			_state = Type::Message::SENT;
			INFO("Message resource transfer initiated");
			return true;

		} else {
			ERROR("Unknown message representation");
			_state = Type::Message::FAILED;
			return false;
		}
	} catch (const std::exception& e) {
		ERROR("Failed to send message: " + std::string(e.what()));
		_state = Type::Message::FAILED;
		return false;
	}
}

// String representation
std::string LXMessage::toString() const {
	if (_hash) {
		return "<LXMessage " + _hash.toHex() + ">";
	} else {
		return "<LXMessage [unpacked]>";
	}
}
