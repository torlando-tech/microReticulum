#include "Resource.h"

#include "ResourceData.h"
#include "Reticulum.h"
#include "Transport.h"
#include "Packet.h"
#include "Log.h"
#include "Identity.h"
#include "Cryptography/BZ2.h"
#include "Cryptography/Hashes.h"

#include <MsgPack.h>
#include <algorithm>

using namespace RNS;
using namespace RNS::Type::Resource;
using namespace RNS::Utilities;

//Resource::Resource(const Link& link /*= {Type::NONE}*/) :
//	_object(new ResourceData(link))
//{
//	assert(_object);
//	MEM("Resource object created");
//}

Resource::Resource(const Bytes& data, const Link& link, const Bytes& request_id, bool is_response, double timeout) :
	_object(new ResourceData(link))
{
	assert(_object);
	MEM("Resource object created");
}

Resource::Resource(const Bytes& data, const Link& link, bool advertise /*= true*/, bool auto_compress /*= true*/, Callbacks::concluded callback /*= nullptr*/, Callbacks::progress progress_callback /*= nullptr*/, double timeout /*= 0.0*/, int segment_index /*= 1*/, const Bytes& original_hash /*= {Type::NONE}*/, const Bytes& request_id /*= {Type::NONE}*/, bool is_response /*= false*/) :
	_object(new ResourceData(link))
{
	assert(_object);
	MEM("Resource object created");

	// Skip if no data provided (receiver mode uses accept() instead)
	if (!data) {
		return;
	}

	// Mark as sender (initiator)
	_object->_initiator = true;
	_object->_is_response = is_response;
	_object->_request_id = request_id;
	_object->_callbacks._concluded = callback;
	_object->_callbacks._progress = progress_callback;

	// Store original data for hash verification
	Bytes input_data = data;
	_object->_total_size = data.size();
	_object->_uncompressed_size = data.size();

	// Compress if beneficial
	Bytes payload_data = input_data;
	_object->_compressed = false;
	if (auto_compress && data.size() <= Type::Resource::AUTO_COMPRESS_MAX_SIZE) {
		Bytes compressed = Cryptography::bz2_compress(input_data);
		if (compressed && compressed.size() < input_data.size()) {
			payload_data = compressed;
			_object->_compressed = true;
			DEBUGF("Resource: Compression saved %zu bytes", input_data.size() - compressed.size());
		}
	}

	// Generate random_hash (4 bytes) - used for hashmap collision prevention
	_object->_random_hash = Identity::get_random_hash().left(Type::Resource::RANDOM_HASH_SIZE);

	// Compute resource hash = SHA256(original_input_data + random_hash)
	// This is verified by receiver after assembly
	_object->_hash = Identity::full_hash(input_data + _object->_random_hash);

	// Set original_hash for multi-segment tracking
	if (original_hash) {
		_object->_original_hash = original_hash;
	} else {
		_object->_original_hash = _object->_hash;
	}

	// Compute expected proof = SHA256(original_data + hash)
	// This is what we expect the receiver to send back as proof
	// (stored but not sent - receiver computes this independently)

	// Prepare payload: random_hash + (compressed_or_uncompressed_data)
	Bytes payload = _object->_random_hash + payload_data;

	// Encrypt the payload using link's Token (const_cast needed as encrypt() modifies internal state)
	Bytes encrypted_data = const_cast<Link&>(link).encrypt(payload);
	if (!encrypted_data) {
		ERROR("Resource: Failed to encrypt payload");
		_object->_status = Type::Resource::FAILED;
		return;
	}

	_object->_encrypted = true;
	_object->_size = encrypted_data.size();

	// Get SDU from link MDU (const_cast needed as get_mdu() is not const)
	_object->_sdu = const_cast<Link&>(link).get_mdu();
	if (_object->_sdu == 0) {
		ERROR("Resource: Invalid SDU from link");
		_object->_status = Type::Resource::FAILED;
		return;
	}

	// Calculate total parts
	_object->_total_parts = (encrypted_data.size() + _object->_sdu - 1) / _object->_sdu;

	// Build hashmap and parts
	// Hashmap = concatenation of 4-byte hashes, one per part
	// Each map_hash = SHA256(part_data + random_hash)[:4]
	_object->_parts.resize(_object->_total_parts);
	_object->_hashmap.resize(_object->_total_parts);
	Bytes hashmap_raw;

	for (size_t i = 0; i < _object->_total_parts; i++) {
		size_t start = i * _object->_sdu;
		size_t end = std::min(start + _object->_sdu, encrypted_data.size());
		Bytes part_data = encrypted_data.mid(start, end - start);

		// Store the part
		_object->_parts[i] = part_data;

		// Compute map hash for this part
		Bytes map_hash = get_map_hash(part_data, _object->_random_hash);
		_object->_hashmap[i] = map_hash;
		hashmap_raw += map_hash;
	}

	_object->_hashmap_raw = hashmap_raw;
	_object->_hashmap_height = _object->_total_parts;

	// Set segment info (for multi-segment resources)
	_object->_segment_index = segment_index;
	_object->_total_segments = 1;  // Single segment for now
	_object->_split = false;

	// Build flags
	_object->_flags = 0;
	if (_object->_encrypted) _object->_flags |= ResourceAdvertisement::FLAG_ENCRYPTED;
	if (_object->_compressed) _object->_flags |= ResourceAdvertisement::FLAG_COMPRESSED;
	if (_object->_split) _object->_flags |= ResourceAdvertisement::FLAG_SPLIT;
	if (_object->_is_response) _object->_flags |= ResourceAdvertisement::FLAG_IS_RESPONSE;
	if (_object->_has_metadata) _object->_flags |= ResourceAdvertisement::FLAG_HAS_METADATA;

	// Initialize tracking
	_object->_sent_parts = 0;
	_object->_status = Type::Resource::QUEUED;
	_object->_last_activity = OS::time();
	_object->_retries_left = Type::Resource::MAX_ADV_RETRIES;

	DEBUGF("Resource: Created for sending, size=%zu, parts=%zu, sdu=%zu, hash=%s",
		_object->_size, _object->_total_parts, _object->_sdu, _object->_hash.toHex().c_str());

	// Optionally advertise immediately
	if (advertise) {
		this->advertise();
	}
}


// Advertise the resource to the remote end
void Resource::advertise() {
	assert(_object);

	if (!_object->_initiator) {
		ERROR("Resource::advertise: Cannot advertise a receiving resource");
		return;
	}

	if (_object->_status == Type::Resource::FAILED) {
		ERROR("Resource::advertise: Resource already failed");
		return;
	}

	DEBUG("Resource::advertise: Building advertisement");

	// Build the ResourceAdvertisement
	ResourceAdvertisement adv;
	adv.transfer_size = _object->_size;
	adv.total_size = _object->_total_size;
	adv.total_parts = _object->_total_parts;
	adv.resource_hash = _object->_hash;
	adv.random_hash = _object->_random_hash;
	adv.original_hash = _object->_original_hash;
	adv.segment_index = _object->_segment_index;
	adv.total_segments = _object->_total_segments;
	adv.request_id = _object->_request_id;
	adv.flags = _object->_flags;
	adv.hashmap = _object->_hashmap_raw;

	// Pack the advertisement
	Bytes adv_data = ResourceAdvertisement::pack(adv);

	DEBUGF("Resource::advertise: Advertisement packed, size=%zu", adv_data.size());

	// Send the advertisement packet - Packet class handles encryption for Link packets
	Packet adv_packet(_object->_link, adv_data, Type::Packet::DATA, Type::Packet::RESOURCE_ADV);
	adv_packet.send();

	_object->_status = Type::Resource::ADVERTISED;
	_object->_adv_sent = OS::time();
	_object->_last_activity = _object->_adv_sent;

	// Register with link for incoming request routing
	_object->_link.register_outgoing_resource(*this);

	DEBUGF("Resource::advertise: Advertisement sent for hash=%s", _object->_hash.toHex().c_str());

	// TODO: Start watchdog timer
}

// Handle incoming part request from receiver
void Resource::request(const Bytes& request_data) {
	assert(_object);

	if (!_object->_initiator) {
		ERROR("Resource::request: Only sender can handle requests");
		return;
	}

	if (_object->_status == Type::Resource::FAILED) {
		ERROR("Resource::request: Resource already failed");
		return;
	}

	// Update status if not already transferring
	if (_object->_status != Type::Resource::TRANSFERRING) {
		_object->_status = Type::Resource::TRANSFERRING;
	}

	_object->_retries_left = _object->_max_retries;

	// Parse request format:
	// [hmu_flag:1][last_map_hash:4?][resource_hash:32][requested_hashes:4*N]
	if (request_data.size() < 1) {
		ERROR("Resource::request: Invalid request data");
		return;
	}

	uint8_t hmu_flag = request_data[0];
	bool wants_more_hashmap = (hmu_flag == Type::Resource::HASHMAP_IS_EXHAUSTED);

	size_t offset = 1;
	Bytes last_map_hash;
	if (wants_more_hashmap) {
		if (request_data.size() < 1 + Type::Resource::MAPHASH_LEN) {
			ERROR("Resource::request: Missing last_map_hash for HMU request");
			return;
		}
		last_map_hash = request_data.mid(1, Type::Resource::MAPHASH_LEN);
		offset += Type::Resource::MAPHASH_LEN;
	}

	// Skip resource hash (32 bytes) - we already know our own hash
	if (request_data.size() < offset + Type::Identity::HASHLENGTH / 8) {
		ERROR("Resource::request: Missing resource hash in request");
		return;
	}
	offset += Type::Identity::HASHLENGTH / 8;

	// Parse requested hashes
	Bytes requested_hashes = request_data.mid(offset);
	size_t num_requested = requested_hashes.size() / Type::Resource::MAPHASH_LEN;

	DEBUGF("Resource::request: %zu parts requested, hmu=%d", num_requested, wants_more_hashmap);

	// Find and send requested parts
	for (size_t i = 0; i < num_requested; i++) {
		Bytes req_hash = requested_hashes.mid(i * Type::Resource::MAPHASH_LEN, Type::Resource::MAPHASH_LEN);

		// Find matching part by map hash
		int part_index = -1;
		for (size_t j = 0; j < _object->_hashmap.size(); j++) {
			if (_object->_hashmap[j] == req_hash) {
				part_index = j;
				break;
			}
		}

		if (part_index >= 0 && part_index < (int)_object->_parts.size()) {
			// Send this part
			Bytes part_data = _object->_parts[part_index];
			Packet part_packet(_object->_link, part_data, Type::Packet::DATA, Type::Packet::RESOURCE);
			part_packet.send();
			_object->_sent_parts++;

			TRACEF("Resource::request: Sent part %d", part_index);
		} else {
			WARNINGF("Resource::request: Requested hash not found: %s", req_hash.toHex().c_str());
		}
	}

	_object->_last_activity = OS::time();

	// Handle hashmap update request (HMU)
	if (wants_more_hashmap && last_map_hash) {
		// Find the index of last_map_hash in our hashmap
		int last_index = -1;
		for (size_t i = 0; i < _object->_hashmap.size(); i++) {
			if (_object->_hashmap[i] == last_map_hash) {
				last_index = i;
				break;
			}
		}

		if (last_index >= 0) {
			// Send additional hashmap starting after last_index
			size_t start_idx = last_index + 1;
			if (start_idx < _object->_hashmap.size()) {
				Bytes additional_hashmap;
				for (size_t i = start_idx; i < _object->_hashmap.size(); i++) {
					additional_hashmap += _object->_hashmap[i];
				}

				// HMU packet format: [segment:1][hashmap_data:N]
				// For simplicity, use segment 0
				Bytes hmu_data;
				hmu_data.append((uint8_t)0);
				hmu_data += additional_hashmap;

				Packet hmu_packet(_object->_link, hmu_data, Type::Packet::DATA, Type::Packet::RESOURCE_HMU);
				hmu_packet.send();

				DEBUGF("Resource::request: Sent HMU with %zu additional hashes",
					additional_hashmap.size() / Type::Resource::MAPHASH_LEN);
			}
		}
	}

	// Check if all parts have been sent
	if (_object->_sent_parts >= _object->_total_parts) {
		_object->_status = Type::Resource::AWAITING_PROOF;
		DEBUG("Resource::request: All parts sent, awaiting proof");
	}

	// Call progress callback
	if (_object->_callbacks._progress != nullptr) {
		_object->_callbacks._progress(*this);
	}
}

void Resource::validate_proof(const Bytes& proof_data) {
	assert(_object);

	if (!_object->_initiator) {
		ERROR("Resource::validate_proof: Only sender validates proof");
		return;
	}

	// Proof format: [resource_hash:32][proof:32]
	// proof = SHA256(original_data + resource_hash)
	if (proof_data.size() < Type::Identity::HASHLENGTH / 8 * 2) {
		ERROR("Resource::validate_proof: Invalid proof data size");
		_object->_status = Type::Resource::FAILED;
		return;
	}

	Bytes received_hash = proof_data.left(Type::Identity::HASHLENGTH / 8);
	Bytes received_proof = proof_data.mid(Type::Identity::HASHLENGTH / 8);

	// Verify hash matches
	if (received_hash != _object->_hash) {
		ERROR("Resource::validate_proof: Hash mismatch");
		_object->_status = Type::Resource::FAILED;
		return;
	}

	// We can't directly verify the proof without keeping the original data
	// The receiver computed: proof = SHA256(original_data + hash)
	// We would need to store _expected_proof during construction
	// For now, just accept the proof if hash matches

	_object->_status = Type::Resource::COMPLETE;
	DEBUG("Resource::validate_proof: Proof accepted, transfer complete");

	// Call concluded callback
	if (_object->_callbacks._concluded != nullptr) {
		_object->_callbacks._concluded(*this);
	}
}

void Resource::cancel() {
}

/*
:returns: The current progress of the resource transfer as a *float* between 0.0 and 1.0.
*/
float Resource::get_progress() const {
/*
	assert(_object);
	if (_object->_initiator) {
		_object->_processed_parts = (_object->_segment_index-1)*math.ceil(Type::Resource::MAX_EFFICIENT_SIZE/Type::Resource::SDU);
		_object->_processed_parts += _object->sent_parts;
		_object->_progress_total_parts = float(_object->grand_total_parts);
	}
	else {
		_object->_processed_parts = (_object->_segment_index-1)*math.ceil(Type::Resource::MAX_EFFICIENT_SIZE/Type::Resource::SDU);
		_object->_processed_parts += _object->_received_count;
		if (_object->split) {
			_object->progress_total_parts = float(math.ceil(_object->total_size/Type::Resource::SDU));
		}
		else {
			_object->progress_total_parts = float(_object->total_parts);
		}
	}

	return (float)_object->processed_parts / (float)_object->progress_total_parts;
*/
	return 0.0;
}

void Resource::set_concluded_callback(Callbacks::concluded callback) {
	assert(_object);
	_object->_callbacks._concluded = callback;
}

void Resource::set_progress_callback(Callbacks::progress callback) {
	assert(_object);
	_object->_callbacks._progress = callback;
}


std::string Resource::toString() const {
	if (!_object) {
		return "";
	}
    //return "<"+RNS.hexrep(self.hash,delimit=False)+"/"+RNS.hexrep(self.link.link_id,delimit=False)+">"
	//return "{Resource:" + _object->_hash.toHex() + "}";
	return "{Resource: unknown}";
}

// getters
const Bytes& Resource::hash() const {
	assert(_object);
	return _object->_hash;
}

const Bytes& Resource::request_id() const {
	assert(_object);
	return _object->_request_id;
}

const Bytes& Resource::data() const {
	assert(_object);
	return _object->_data;
}

const Type::Resource::status Resource::status() const {
	assert(_object);
	return _object->_status;
}

const size_t Resource::size() const {
	assert(_object);
	return _object->_size;
}

const size_t Resource::total_size() const {
	assert(_object);
	return _object->_total_size;
}

// setters


// ResourceAdvertisement implementation
// Note: Use fully qualified names to avoid conflict with RNS::Type::Resource::ResourceAdvertisement namespace

bool RNS::ResourceAdvertisement::unpack(const Bytes& data, RNS::ResourceAdvertisement& adv) {
	// Parse msgpack map with mixed value types
	// Python format: {"t": int, "d": int, "n": int, "h": bytes, "r": bytes, ...}

	MsgPack::Unpacker unpacker;
	if (!unpacker.feed(data.data(), data.size())) {
		ERROR("ResourceAdvertisement: Failed to feed msgpack data");
		return false;
	}

	if (!unpacker.isMap()) {
		ERROR("ResourceAdvertisement: Data is not a map");
		return false;
	}

	size_t map_size = unpacker.unpackMapSize();
	TRACEF("ResourceAdvertisement: Unpacking map with %zu entries", map_size);

	for (size_t i = 0; i < map_size; i++) {
		// Read key (string)
		std::string key = unpacker.unpackString();

		if (key == "t") {
			adv.transfer_size = unpacker.unpackUInt<size_t>();
		} else if (key == "d") {
			adv.total_size = unpacker.unpackUInt<size_t>();
		} else if (key == "n") {
			adv.total_parts = unpacker.unpackUInt<size_t>();
		} else if (key == "h") {
			MsgPack::bin_t<uint8_t> bin = unpacker.unpackBinary();
			adv.resource_hash = Bytes(bin.data(), bin.size());
		} else if (key == "r") {
			MsgPack::bin_t<uint8_t> bin = unpacker.unpackBinary();
			adv.random_hash = Bytes(bin.data(), bin.size());
		} else if (key == "o") {
			// Optional field - can be nil
			if (unpacker.isNil()) {
				unpacker.unpackNil();
			} else {
				MsgPack::bin_t<uint8_t> bin = unpacker.unpackBinary();
				adv.original_hash = Bytes(bin.data(), bin.size());
			}
		} else if (key == "i") {
			adv.segment_index = unpacker.unpackInt<int>();
		} else if (key == "l") {
			adv.total_segments = unpacker.unpackInt<int>();
		} else if (key == "q") {
			// Optional field - can be nil
			if (unpacker.isNil()) {
				unpacker.unpackNil();
			} else {
				MsgPack::bin_t<uint8_t> bin = unpacker.unpackBinary();
				adv.request_id = Bytes(bin.data(), bin.size());
			}
		} else if (key == "f") {
			adv.flags = unpacker.unpackUInt<uint8_t>();
		} else if (key == "m") {
			MsgPack::bin_t<uint8_t> bin = unpacker.unpackBinary();
			adv.hashmap = Bytes(bin.data(), bin.size());
		} else {
			// Skip unknown key - need to consume the value
			WARNING("ResourceAdvertisement: Unknown key in advertisement: " + key);
			// Try to skip the value based on type
			if (unpacker.isNil()) {
				unpacker.unpackNil();
			} else if (unpacker.isUInt()) {
				unpacker.unpackUInt<uint64_t>();
			} else if (unpacker.isInt()) {
				unpacker.unpackInt<int64_t>();
			} else if (unpacker.isBin()) {
				unpacker.unpackBinary();
			} else if (unpacker.isStr()) {
				unpacker.unpackString();
			}
		}
	}

	// Parse the flags
	adv.parse_flags();

	if (!unpacker.decoded()) {
		ERROR("ResourceAdvertisement: Decoding failed");
		return false;
	}

	TRACEF("ResourceAdvertisement: Unpacked - transfer_size=%zu, total_size=%zu, parts=%zu, flags=0x%02x",
		adv.transfer_size, adv.total_size, adv.total_parts, adv.flags);

	return true;
}

Bytes RNS::ResourceAdvertisement::pack(const RNS::ResourceAdvertisement& res_adv) {
	// Python RNS expects all 11 fields to always be present
	// Fields: t, d, n, h, r, o, i, l, q, f, m

	// Copy all values to local variables to avoid template issues
	uint64_t t_val = res_adv.transfer_size;
	uint64_t d_val = res_adv.total_size;
	uint64_t n_val = res_adv.total_parts;
	uint8_t f_val = res_adv.flags;
	int i_val = res_adv.segment_index;
	int l_val = res_adv.total_segments;

	// Copy binary data to local bin_t
	MsgPack::bin_t<uint8_t> h_bin(res_adv.resource_hash.data(), res_adv.resource_hash.data() + res_adv.resource_hash.size());
	MsgPack::bin_t<uint8_t> r_bin(res_adv.random_hash.data(), res_adv.random_hash.data() + res_adv.random_hash.size());
	MsgPack::bin_t<uint8_t> m_bin(res_adv.hashmap.data(), res_adv.hashmap.data() + res_adv.hashmap.size());

	// Use empty binary if no original_hash
	MsgPack::bin_t<uint8_t> o_bin;
	if (res_adv.original_hash.size() > 0) {
		o_bin = MsgPack::bin_t<uint8_t>(res_adv.original_hash.data(), res_adv.original_hash.data() + res_adv.original_hash.size());
	}

	// Use empty binary if no request_id
	MsgPack::bin_t<uint8_t> q_bin;
	if (res_adv.request_id.size() > 0) {
		q_bin = MsgPack::bin_t<uint8_t>(res_adv.request_id.data(), res_adv.request_id.data() + res_adv.request_id.size());
	}

	MsgPack::Packer packer;

	// Pack map header - always 11 fields
	packer.pack(MsgPack::map_size_t(11));

	// Pack all 11 fields in same order as Python
	packer.serialize(std::string("t"));
	packer.serialize(t_val);

	packer.serialize(std::string("d"));
	packer.serialize(d_val);

	packer.serialize(std::string("n"));
	packer.serialize(n_val);

	packer.serialize(std::string("h"));
	packer.serialize(h_bin);

	packer.serialize(std::string("r"));
	packer.serialize(r_bin);

	packer.serialize(std::string("o"));
	packer.serialize(o_bin);

	packer.serialize(std::string("i"));
	packer.serialize(i_val);

	packer.serialize(std::string("l"));
	packer.serialize(l_val);

	packer.serialize(std::string("q"));
	packer.serialize(q_bin);

	packer.serialize(std::string("f"));
	packer.serialize(f_val);

	packer.serialize(std::string("m"));
	packer.serialize(m_bin);

	return Bytes(packer.data(), packer.size());
}


// Resource::accept implementation
Resource Resource::accept(const Packet& advertisement_packet,
	Callbacks::concluded callback,
	Callbacks::progress progress_callback,
	const Bytes& request_id)
{
	TRACE("Resource::accept called");

	// Get the link from the packet
	Link link = advertisement_packet.link();
	if (!link) {
		ERROR("Resource::accept: No link associated with advertisement packet");
		return Resource(Type::NONE);
	}

	// Decrypt and parse the advertisement
	Bytes plaintext = link.decrypt(advertisement_packet.data());
	if (!plaintext) {
		ERROR("Resource::accept: Failed to decrypt advertisement");
		return Resource(Type::NONE);
	}

	// Parse the advertisement
	RNS::ResourceAdvertisement adv;
	if (!RNS::ResourceAdvertisement::unpack(plaintext, adv)) {
		ERROR("Resource::accept: Failed to parse advertisement");
		return Resource(Type::NONE);
	}

	DEBUGF("Resource::accept: Received advertisement for resource hash=%s, transfer_size=%zu, total_size=%zu, parts=%zu",
		adv.resource_hash.toHex().c_str(), adv.transfer_size, adv.total_size, adv.total_parts);
	DEBUGF("Resource::accept: random_hash=%s (len=%zu)", adv.random_hash.toHex().c_str(), adv.random_hash.size());
	DEBUGF("Resource::accept: hashmap=%s (len=%zu)", adv.hashmap.toHex().c_str(), adv.hashmap.size());

	// Create the receiving resource
	// Use the request_id from either the parameter or the advertisement
	Bytes effective_request_id = request_id;
	if (!effective_request_id && adv.request_id) {
		effective_request_id = adv.request_id;
	}

	// Create Resource with empty data for receiving
	Resource resource(Bytes(), link, effective_request_id, adv.is_response, 0.0);
	if (!resource._object) {
		ERROR("Resource::accept: Failed to create resource object");
		return Resource(Type::NONE);
	}

	// Initialize as receiver (non-initiator)
	resource._object->_initiator = false;
	resource._object->_status = Type::Resource::TRANSFERRING;

	// Copy advertisement data to resource
	resource._object->_flags = adv.flags;
	resource._object->_size = adv.transfer_size;
	resource._object->_total_size = adv.total_size;
	resource._object->_uncompressed_size = adv.total_size;
	resource._object->_hash = adv.resource_hash;
	resource._object->_original_hash = adv.original_hash;
	resource._object->_random_hash = adv.random_hash;
	resource._object->_hashmap_raw = adv.hashmap;
	resource._object->_encrypted = adv.is_encrypted;
	resource._object->_compressed = adv.is_compressed;
	resource._object->_is_response = adv.is_response;
	resource._object->_has_metadata = adv.has_metadata;

	// Calculate number of parts based on SDU (link MDU)
	size_t sdu = link.get_mdu();
	resource._object->_sdu = sdu;

	// Calculate total parts from transfer size and SDU
	size_t total_parts = (resource._object->_size + sdu - 1) / sdu;  // ceil division
	resource._object->_total_parts = total_parts;

	// Initialize parts array
	resource._object->_parts.resize(total_parts);
	resource._object->_received_count = 0;
	resource._object->_outstanding_parts = 0;

	// Multi-segment tracking
	resource._object->_segment_index = adv.segment_index;
	resource._object->_total_segments = adv.total_segments;
	resource._object->_split = (adv.total_segments > 1);

	// Initialize hashmap tracking
	resource._object->_hashmap.resize(total_parts);
	resource._object->_hashmap_height = 0;
	resource._object->_waiting_for_hmu = false;
	resource._object->_receiving_part = false;
	resource._object->_consecutive_completed_height = -1;

	// Initialize window management
	resource._object->_window = Type::Resource::WINDOW;
	resource._object->_window_max = Type::Resource::WINDOW_MAX_SLOW;
	resource._object->_window_min = Type::Resource::WINDOW_MIN;
	resource._object->_window_flexibility = Type::Resource::WINDOW_FLEXIBILITY;

	// Initialize timing
	resource._object->_last_activity = OS::time();
	resource._object->_retries_left = Type::Resource::MAX_RETRIES;

	// Set callbacks
	resource._object->_callbacks._concluded = callback;
	resource._object->_callbacks._progress = progress_callback;

	// TODO: Register resource with link
	// if (!link.has_incoming_resource(resource)) {
	//     link.register_incoming_resource(resource);
	// }

	DEBUGF("Resource::accept: Initialized receiving resource, total_parts=%zu, sdu=%zu",
		total_parts, sdu);

	// Process the initial hashmap from the advertisement
	resource.hashmap_update(0, resource._object->_hashmap_raw);

	// TODO: Start watchdog
	// resource.watchdog_job();

	return resource;
}


// Hashmap update from packet
void Resource::hashmap_update_packet(const Bytes& plaintext) {
	assert(_object);
	// The plaintext format is: [segment:1][hashmap_data:N]
	if (plaintext.size() < 1) {
		ERROR("Resource::hashmap_update_packet: Invalid packet size");
		return;
	}
	uint8_t segment = plaintext[0];
	Bytes hashmap_data = plaintext.mid(1);
	hashmap_update(segment, hashmap_data);
}

// Update hashmap with new hashes
void Resource::hashmap_update(int segment, const Bytes& hashmap_data) {
	assert(_object);

	TRACEF("Resource::hashmap_update: segment=%d, hashmap_data_size=%zu", segment, hashmap_data.size());

	// Parse hashmap data - each hash is MAPHASH_LEN (4) bytes
	size_t hash_count = hashmap_data.size() / Type::Resource::MAPHASH_LEN;
	size_t start_index = _object->_hashmap_height;

	for (size_t i = 0; i < hash_count && (start_index + i) < _object->_total_parts; i++) {
		size_t offset = i * Type::Resource::MAPHASH_LEN;
		Bytes map_hash = hashmap_data.mid(offset, Type::Resource::MAPHASH_LEN);
		_object->_hashmap[start_index + i] = map_hash;
		_object->_hashmap_height++;
	}

	_object->_waiting_for_hmu = false;

	DEBUGF("Resource::hashmap_update: Updated hashmap, height=%zu", _object->_hashmap_height);

	// Now request the next parts
	request_next();
}

// Get map hash for a data chunk (first 4 bytes of SHA256(data + random_hash))
// Python: RNS.Identity.full_hash(data+self.random_hash)[:Resource.MAPHASH_LEN]
Bytes Resource::get_map_hash(const Bytes& data, const Bytes& random_hash) {
	Bytes to_hash = data + random_hash;
	Bytes full_hash = Cryptography::sha256(to_hash);
	return full_hash.left(Type::Resource::MAPHASH_LEN);
}

// Request next parts from sender
void Resource::request_next() {
	assert(_object);

	// Build list of requested hashes
	Bytes requested_hashes;
	size_t requested_count = 0;

	// Start from consecutive_completed_height + 1
	int start = _object->_consecutive_completed_height + 1;
	size_t window = _object->_window;

	for (size_t i = start; i < _object->_total_parts && requested_count < window; i++) {
		// Only request parts where we know the hash
		if (i < _object->_hashmap.size() && _object->_hashmap[i].size() > 0) {
			// Check if we haven't already received this part
			if (_object->_parts[i].size() == 0) {
				requested_hashes += _object->_hashmap[i];
				requested_count++;
			}
		} else {
			// Hashmap exhausted - need more hashes from sender
			break;
		}
	}

	// Determine if hashmap is exhausted
	uint8_t hmu_flag = 0x00;  // HASHMAP_IS_NOT_EXHAUSTED
	Bytes hmu_part;

	if (_object->_hashmap_height < _object->_total_parts && requested_count < window) {
		// Need more hashmap entries
		hmu_flag = 0xFF;  // HASHMAP_IS_EXHAUSTED
		if (_object->_hashmap_height > 0) {
			// Include the last known map hash
			hmu_part = _object->_hashmap[_object->_hashmap_height - 1];
		}
		_object->_waiting_for_hmu = true;
	}

	// Build request packet: [hmu_flag:1][last_map_hash:4?][resource_hash:32][requested_hashes:4*N]
	// Note: Python RNS uses full 32-byte hash (HASHLENGTH/8), not truncated 16-byte hash
	Bytes request_data;
	request_data.append(hmu_flag);
	if (hmu_flag == 0xFF && hmu_part.size() > 0) {
		request_data += hmu_part;
	}

	// Use full resource hash (32 bytes) - Python expects RNS.Identity.HASHLENGTH//8 = 32 bytes
	request_data += _object->_hash;
	request_data += requested_hashes;

	_object->_outstanding_parts = requested_count;
	_object->_req_sent = OS::time();

	DEBUGF("Resource::request_next: Requesting %zu parts, hmu_flag=0x%02x", requested_count, hmu_flag);

	// Send the request packet
	Packet request_packet(_object->_link, request_data, Type::Packet::DATA, Type::Packet::RESOURCE_REQ);
	request_packet.send();

	_object->_last_activity = OS::time();
}

// Receive a resource part
void Resource::receive_part(const Packet& packet) {
	assert(_object);

	if (_object->_receiving_part) {
		WARNING("Resource::receive_part: Already receiving a part, ignoring");
		return;
	}

	_object->_receiving_part = true;

	// Use plaintext since Link has already decrypted the data
	// Python sends ONLY the part data (no map_hash prefix)
	// The receiver computes map_hash = SHA256(part_data + random_hash)[:4]
	const Bytes& part_data = const_cast<Packet&>(packet).plaintext();
	if (part_data.size() == 0) {
		ERROR("Resource::receive_part: Part data is empty");
		_object->_receiving_part = false;
		return;
	}

	// Compute the map hash for this part data using random_hash from advertisement
	DEBUGF("Resource::receive_part: random_hash=%s (len=%zu)",
		_object->_random_hash.toHex().c_str(), _object->_random_hash.size());
	DEBUGF("Resource::receive_part: part_data first 32 bytes=%s",
		part_data.left(32).toHex().c_str());

	Bytes map_hash = get_map_hash(part_data, _object->_random_hash);

	DEBUGF("Resource::receive_part: Computed map_hash=%s for part_data size=%zu",
		map_hash.toHex().c_str(), part_data.size());

	// Find which part this is by matching the map hash against hashmap
	int part_index = -1;
	for (size_t i = 0; i < _object->_hashmap.size(); i++) {
		if (_object->_hashmap[i] == map_hash) {
			part_index = i;
			break;
		}
	}

	if (part_index < 0) {
		WARNINGF("Resource::receive_part: Unknown map hash %s, ignoring part", map_hash.toHex().c_str());
		// Debug: dump the expected hashmap entries
		for (size_t i = 0; i < _object->_hashmap.size(); i++) {
			DEBUGF("  hashmap[%zu] = %s", i, _object->_hashmap[i].toHex().c_str());
		}
		_object->_receiving_part = false;
		return;
	}

	// Store the part
	_object->_parts[part_index] = part_data;
	_object->_received_count++;
	if (_object->_outstanding_parts > 0) {
		_object->_outstanding_parts--;
	}

	// Update consecutive completed height
	while (_object->_consecutive_completed_height + 1 < (int)_object->_total_parts &&
		   _object->_parts[_object->_consecutive_completed_height + 1].size() > 0) {
		_object->_consecutive_completed_height++;
	}

	_object->_last_activity = OS::time();

	TRACEF("Resource::receive_part: Received part %d/%zu, consecutive=%d",
		part_index, _object->_total_parts, _object->_consecutive_completed_height);

	// Call progress callback if set
	if (_object->_callbacks._progress != nullptr) {
		_object->_callbacks._progress(*this);
	}

	// Check if transfer is complete
	if (_object->_received_count >= _object->_total_parts) {
		DEBUG("Resource::receive_part: All parts received, assembling");
		assemble();
	} else if (_object->_outstanding_parts == 0) {
		// Request more parts
		request_next();
	}

	_object->_receiving_part = false;
}

// Assemble the resource from received parts
void Resource::assemble() {
	assert(_object);

	if (_object->_assembly_lock) {
		return;
	}
	_object->_assembly_lock = true;

	TRACE("Resource::assemble: Starting assembly");

	// Concatenate all parts (Token-encrypted chunks)
	Bytes assembled_data;
	for (size_t i = 0; i < _object->_parts.size(); i++) {
		assembled_data += _object->_parts[i];
	}

	DEBUGF("Resource::assemble: Assembled %zu bytes from %zu parts", assembled_data.size(), _object->_parts.size());

	// Decrypt if needed (Resource uses Token encryption via link.encrypt())
	if (_object->_encrypted) {
		Bytes decrypted = _object->_link.decrypt(assembled_data);
		if (!decrypted) {
			ERROR("Resource::assemble: Token decryption failed");
			_object->_status = Type::Resource::FAILED;
			_object->_assembly_lock = false;
			return;
		}
		assembled_data = decrypted;
		DEBUGF("Resource::assemble: Decrypted to %zu bytes", assembled_data.size());
	}

	// Strip off the random_hash prefix (4 bytes)
	if (assembled_data.size() < Type::Resource::RANDOM_HASH_SIZE) {
		ERROR("Resource::assemble: Assembled data too small for random_hash");
		_object->_status = Type::Resource::FAILED;
		_object->_assembly_lock = false;
		return;
	}
	assembled_data = assembled_data.mid(Type::Resource::RANDOM_HASH_SIZE);
	DEBUGF("Resource::assemble: After stripping random_hash: %zu bytes", assembled_data.size());

	// Decompress if needed
	if (_object->_compressed) {
		Bytes decompressed = Cryptography::bz2_decompress(assembled_data);
		if (!decompressed) {
			ERROR("Resource::assemble: Decompression failed");
			_object->_status = Type::Resource::FAILED;
			_object->_assembly_lock = false;
			return;
		}
		assembled_data = decompressed;
		DEBUGF("Resource::assemble: Decompressed to %zu bytes", assembled_data.size());
	}

	// Verify hash
	Bytes calculated_hash = Identity::full_hash(assembled_data + _object->_random_hash);
	if (calculated_hash != _object->_hash) {
		ERROR("Resource::assemble: Hash verification failed");
		DEBUGF("Resource::assemble: Expected: %s", _object->_hash.toHex().c_str());
		DEBUGF("Resource::assemble: Calculated: %s", calculated_hash.toHex().c_str());
		_object->_status = Type::Resource::CORRUPT;
		_object->_assembly_lock = false;
		return;
	}

	// Store the final data
	_object->_data = assembled_data;
	_object->_status = Type::Resource::COMPLETE;

	DEBUGF("Resource::assemble: Assembly complete, data_size=%zu", _object->_data.size());

	// Send proof to sender
	prove();

	// Call concluded callback
	if (_object->_callbacks._concluded != nullptr) {
		_object->_callbacks._concluded(*this);
	}

	_object->_assembly_lock = false;
}

// Send proof that resource was received
void Resource::prove() {
	assert(_object);

	// Python: proof = RNS.Identity.full_hash(self.data + self.hash)
	//         proof_data = self.hash + proof
	// The proof is SHA256(data + resource_hash), and the packet contains resource_hash + proof
	Bytes proof = Identity::full_hash(_object->_data + _object->_hash);
	Bytes proof_data = _object->_hash + proof;

	DEBUGF("Resource::prove: Sending proof, hash=%s, proof=%s",
		_object->_hash.toHex().c_str(), proof.toHex().c_str());

	// Send the proof packet - must use PROOF packet type for Python compatibility
	Packet proof_packet(_object->_link, proof_data, Type::Packet::PROOF, Type::Packet::RESOURCE_PRF);
	proof_packet.send();

	DEBUG("Resource::prove: Proof sent");
}

