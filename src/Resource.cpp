/*
 * Copyright (c) 2023 Chad Attermann
 * Resource transfer protocol implementation — port of python RNS.Resource.
 */

#include "Resource.h"

#include "ResourceData.h"
#include "Reticulum.h"
#include "Transport.h"
#include "Identity.h"
#include "Link.h"
#include "Packet.h"
#include "Log.h"
#include "Cryptography/Random.h"
#include "Cryptography/BZ2.h"

#include <MsgPack.h>

#include <algorithm>
#include <cstring>

using namespace RNS;
// Don't `using namespace RNS::Type::Resource` — its sub-namespace
// `ResourceAdvertisement` collides with our own `RNS::ResourceAdvertisement`
// class. Status enum values are referenced fully-qualified instead.
using namespace RNS::Utilities;

// ============================================================================
// Resource constructors
// ============================================================================

// Helper used by both constructors — split data into parts, build hashmap.
// `auto_compress` controls whether to bz2-compress before encryption to
// match python LXMF's wire shape.
static void init_sender_state(ResourceData& d, const Bytes& payload, const Link& link, bool auto_compress);

Resource::Resource(const Bytes& data, const Link& link, const Bytes& request_id, bool is_response, double timeout) :
	_object(new ResourceData(link))
{
	assert(_object);
	_object->_request_id = request_id;
	_object->_is_response = is_response;
	init_sender_state(*_object, data, link, /*advertise=*/true);
}

Resource::Resource(const Bytes& data, const Link& link, bool advertise /*= true*/, bool auto_compress /*= true*/, Callbacks::concluded callback /*= nullptr*/, Callbacks::progress progress_callback /*= nullptr*/, double timeout /*= 0.0*/, int segment_index /*= 1*/, const Bytes& original_hash /*= {Type::NONE}*/, const Bytes& request_id /*= {Type::NONE}*/, bool is_response /*= false*/) :
	_object(new ResourceData(link))
{
	assert(_object);
	(void)timeout; (void)segment_index;
	_object->_callbacks._concluded = callback;
	_object->_callbacks._progress = progress_callback;
	_object->_request_id = request_id;
	_object->_is_response = is_response;
	init_sender_state(*_object, data, link, auto_compress);
	if (!_object->_original_hash) {
		_object->_original_hash = original_hash ? original_hash : _object->_hash;
	}
	// Register with the link so inbound RESOURCE_REQ / RESOURCE_PRF can find us.
	const_cast<Link&>(link).register_outgoing_resource(*this);
	if (advertise) {
		this->advertise();
	}
}

static void init_sender_state(ResourceData& d, const Bytes& payload, const Link& link, bool auto_compress) {
	d._initiator = true;
	d._encrypted = true;
	d._segment_index = 1;
	d._total_segments = 1;
	d._split = false;
	d._has_metadata = false;
	d._total_size = payload.size();

	// Hash := full_hash(uncompressed_data || random_hash). The random
	// hash is generated EARLY, then both used in the hash AND prefixed
	// to the (possibly compressed) payload before encryption.
	Bytes rand4 = Cryptography::random(Resource::RANDOM_HASH_SIZE);
	d._random_hash = rand4;

	Bytes hash_input;
	hash_input << payload;
	hash_input << rand4;
	d._hash = Identity::full_hash(hash_input);
	d._truncated_hash = d._hash.left(Type::Identity::TRUNCATED_HASHLENGTH/8);
	d._original_hash = d._hash;

	// Expected proof := full_hash(uncompressed_data || hash)
	Bytes proof_input;
	proof_input << payload;
	proof_input << d._hash;
	d._expected_proof = Identity::full_hash(proof_input);

	// Auto-compress: try bz2 and use it if it shrinks the payload.
	// Matches python Resource.__init__ at lines 388-415.
	Bytes payload_for_wire = payload;
	d._compressed = false;
	if (auto_compress) {
		Bytes compressed = Cryptography::bz2_compress(payload);
		if (compressed.size() > 0 && compressed.size() < payload.size()) {
			payload_for_wire = compressed;
			d._compressed = true;
		}
	}

	Bytes prefixed;
	prefixed << rand4;
	prefixed << payload_for_wire;

	// Encrypt the prefixed payload over the link. The link's encrypt()
	// produces a single Token blob suitable for streaming.
	Bytes encrypted = const_cast<Link&>(link).encrypt(prefixed);
	d._size = encrypted.size();

	// Resource SDU matches python RNS.Resource.__init__:
	//   sdu = link.mtu - HEADER_MAXSIZE - IFAC_MIN_SIZE
	// which equals Reticulum::MDU at the default MTU (=500). This is
	// distinct from Link::MDU (which subtracts FERNET_OVERHEAD and rounds
	// to AES blocks) — using Link::MDU here would split into more parts
	// than the python receiver expects (it derives total_parts from
	// adv.t / sdu, not from adv.n), causing hashmap-dim mismatch.
	d._sdu = Type::Reticulum::MDU;
	if (d._sdu == 0) d._sdu = 200;

	// Split into parts.
	d._total_parts = (encrypted.size() + d._sdu - 1) / d._sdu;
	d._parts_sender.clear();
	d._parts_sender.reserve(d._total_parts);
	d._sent.assign(d._total_parts, false);
	Bytes hashmap_concat;
	for (size_t i = 0; i < d._total_parts; ++i) {
		size_t off = i * d._sdu;
		size_t len = std::min(d._sdu, encrypted.size() - off);
		Bytes chunk(encrypted.data() + off, len);
		d._parts_sender.push_back(chunk);

		// map_hash := full_hash(part_data || random_hash)[:MAPHASH_LEN]
		Bytes mh_input;
		mh_input << chunk;
		mh_input << d._random_hash;
		Bytes mh = Identity::full_hash(mh_input).left(Resource::MAPHASH_LEN);
		hashmap_concat << mh;
	}
	d._hashmap = hashmap_concat;
	d._sent_parts = 0;

	// Compute flags byte: encrypted=bit0, compressed=bit1 (etc).
	d._flags = (uint8_t)(d._encrypted ? 0x01 : 0x00);
	if (d._compressed) d._flags |= 0x02;

	d._status = Type::Resource::ADVERTISED;
	(void)link;
}

// ============================================================================
// ResourceAdvertisement
// ============================================================================

ResourceAdvertisement::ResourceAdvertisement(const Resource& resource) {
	// Use friend access via Resource's protected _object pointer.
	struct Probe : public Resource {
		using Resource::_object;
	};
	const ResourceData& rd = *static_cast<const Probe&>(resource)._object;
	t = (uint32_t)rd._size;
	d = (uint32_t)rd._total_size;
	n = (uint32_t)rd._total_parts;
	h = rd._hash;
	r = rd._random_hash;
	o = rd._original_hash;
	m = rd._hashmap;
	f = rd._flags;
	i = rd._segment_index;
	l = rd._total_segments;
	q = rd._request_id;
}

Bytes ResourceAdvertisement::pack() const {
	MsgPack::Packer p;
	p.packMapSize(11);
	// Each entry: pack key as fixed string, then the value with the right type.
	p.packString("t"); p.packInteger((uint32_t)t);
	p.packString("d"); p.packInteger((uint32_t)d);
	p.packString("n"); p.packInteger((uint32_t)n);
	p.packString("h"); p.packBinary(h.data(), h.size());
	p.packString("r"); p.packBinary(r.data(), r.size());
	p.packString("o"); p.packBinary(o.data(), o.size());
	p.packString("m"); p.packBinary(m.data(), m.size());
	p.packString("f"); p.packInteger((uint8_t)f);
	p.packString("i"); p.packInteger((uint16_t)i);
	p.packString("l"); p.packInteger((uint16_t)l);
	p.packString("q");
	if (q.size()) p.packBinary(q.data(), q.size());
	else p.packNil();
	return Bytes(p.data(), p.size());
}

ResourceAdvertisement ResourceAdvertisement::unpack(const Bytes& packed) {
	ResourceAdvertisement adv;
	MsgPack::Unpacker u;
	u.feed(packed.data(), packed.size());
	MsgPack::map_size_t map_size;
	u.deserialize(map_size);
	for (size_t k = 0; k < map_size.size(); ++k) {
		MsgPack::str_t key;
		u.deserialize(key);
		std::string ks = std::string(key.c_str());
		if (ks == "t") { uint32_t v=0; u.deserialize(v); adv.t = v; }
		else if (ks == "d") { uint32_t v=0; u.deserialize(v); adv.d = v; }
		else if (ks == "n") { uint32_t v=0; u.deserialize(v); adv.n = v; }
		else if (ks == "f") { uint8_t v=0; u.deserialize(v); adv.f = v; }
		else if (ks == "i") { uint16_t v=0; u.deserialize(v); adv.i = v; }
		else if (ks == "l") { uint16_t v=0; u.deserialize(v); adv.l = v; }
		else if (ks == "h") { MsgPack::bin_t<uint8_t> b; u.deserialize(b); adv.h = Bytes(b); }
		else if (ks == "r") { MsgPack::bin_t<uint8_t> b; u.deserialize(b); adv.r = Bytes(b); }
		else if (ks == "o") { MsgPack::bin_t<uint8_t> b; u.deserialize(b); adv.o = Bytes(b); }
		else if (ks == "m") { MsgPack::bin_t<uint8_t> b; u.deserialize(b); adv.m = Bytes(b); }
		else if (ks == "q") {
			// q can be nil OR bin. Peek at next byte to decide.
			uint8_t* raw = (uint8_t*)packed.data();
			(void)raw;
			// Best-effort: try bin, fall back if it's nil.
			try {
				MsgPack::bin_t<uint8_t> b;
				u.deserialize(b);
				adv.q = Bytes(b);
			} catch (...) {
				// nil — Unpacker will have advanced past it on its own
				// type-decoding step; nothing to do.
			}
		}
	}
	return adv;
}

// ============================================================================
// Resource sender API
// ============================================================================

void Resource::advertise() {
	assert(_object);
	ResourceAdvertisement adv(*this);
	Bytes adv_bytes = adv.pack();
	DEBUGF("Resource: advertising hash=%s parts=%zu size=%zu adv_size=%zu compressed=%d",
	       _object->_hash.toHex().c_str(), _object->_total_parts,
	       _object->_size, adv_bytes.size(), (int)_object->_compressed);
	Packet adv_packet(_object->_link, adv_bytes, Type::Packet::DATA, Type::Packet::RESOURCE_ADV);
	adv_packet.send();
	_object->_status = Type::Resource::ADVERTISED;
}

void Resource::request(const Bytes& request_data) {
	assert(_object);
	if (_object->_status == Type::Resource::FAILED) return;

	// Wire format: [hashmap_exhausted_flag][resource_hash 32][requested_map_hashes...]
	if (request_data.size() < 1 + Type::Identity::HASHLENGTH/8) {
		WARNING("Resource::request: payload too small");
		return;
	}
	bool wants_more_hashmap = (request_data.data()[0] == HASHMAP_IS_EXHAUSTED);
	size_t pad = wants_more_hashmap ? 1 + MAPHASH_LEN : 1;
	if (request_data.size() < pad + Type::Identity::HASHLENGTH/8) return;

	Bytes requested_hashes = request_data.mid(pad + Type::Identity::HASHLENGTH/8);
	size_t n_req = requested_hashes.size() / MAPHASH_LEN;

	_object->_status = Type::Resource::TRANSFERRING;

	// For each requested map_hash, find the matching part by scanning
	// the hashmap and send it.
	for (size_t i = 0; i < n_req; ++i) {
		Bytes mh = requested_hashes.mid(i * MAPHASH_LEN, MAPHASH_LEN);
		for (size_t p = 0; p < _object->_total_parts; ++p) {
			Bytes stored_mh = _object->_hashmap.mid(p * MAPHASH_LEN, MAPHASH_LEN);
			if (stored_mh == mh) {
				const Bytes& part_data = _object->_parts_sender[p];
				Packet part_packet(_object->_link, part_data, Type::Packet::DATA, Type::Packet::RESOURCE);
				part_packet.send();
				if (!_object->_sent[p]) {
					_object->_sent[p] = true;
					_object->_sent_parts++;
				}
				break;
			}
		}
	}

	if (_object->_sent_parts >= _object->_total_parts) {
		_object->_status = Type::Resource::AWAITING_PROOF;
	}

	// Fire the user-supplied progress callback after each batch of parts
	// is sent. Pre-this-fix the `progress_callback` ctor parameter was
	// stored on the resource but never invoked anywhere — only the
	// RequestReceipt response path in Link.cpp called it. Plain Resource
	// transfers (used by LXMF DIRECT-large delivery) silently dropped
	// every progress tick. Mirrors python Reticulum/RNS/Resource.py
	// `__progress_callback(self)` invocation pattern.
	if (_object->_callbacks._progress) {
		try {
			_object->_callbacks._progress(*this);
		}
		catch (const std::exception& e) {
			ERRORF("Error while executing resource sender progress callback: %s", e.what());
		}
	}
}

void Resource::validate_proof(const Bytes& proof_data) {
	assert(_object);
	if (_object->_status == Type::Resource::FAILED) return;
	// Wire format: [resource_hash 32][proof_hash 32]
	if (proof_data.size() != 2 * (Type::Identity::HASHLENGTH/8)) {
		WARNINGF("Resource::validate_proof: bad length %zu", proof_data.size());
		return;
	}
	Bytes proof_hash = proof_data.mid(Type::Identity::HASHLENGTH/8);
	if (proof_hash != _object->_expected_proof) {
		WARNING("Resource::validate_proof: proof hash mismatch");
		return;
	}
	_object->_status = Type::Resource::COMPLETE;
	if (_object->_callbacks._concluded) {
		try { _object->_callbacks._concluded(*this); }
		catch (const std::exception& e) {
			ERRORF("Resource concluded callback threw: %s", e.what());
		}
	}
}

// ============================================================================
// Resource receiver API
// ============================================================================

/*static*/ Resource Resource::accept(const Packet& advertisement_packet,
                                     Callbacks::concluded callback,
                                     Callbacks::progress progress_callback,
                                     const Bytes& request_id) {
	const Link& link = advertisement_packet.link();
	// Packet::plaintext() is non-const (it returns a ref into mutable
	// state); the dispatcher hands us a const Packet&, so cast to call.
	ResourceAdvertisement adv = ResourceAdvertisement::unpack(
	    const_cast<Packet&>(advertisement_packet).plaintext());

	Resource res{Type::NONE};
	res._object.reset(new ResourceData(link));
	res._object->_initiator = false;
	res._object->_callbacks._concluded = callback;
	res._object->_callbacks._progress = progress_callback;
	res._object->_request_id = request_id;
	res._object->_status = Type::Resource::TRANSFERRING;
	res._object->_size = adv.t;
	res._object->_total_size = adv.d;
	res._object->_total_parts = adv.n;
	res._object->_hash = adv.h;
	res._object->_truncated_hash = adv.h.left(Type::Identity::TRUNCATED_HASHLENGTH/8);
	res._object->_original_hash = adv.o;
	res._object->_random_hash = adv.r;
	res._object->_hashmap = adv.m;
	res._object->_flags = adv.f;
	res._object->_segment_index = adv.i;
	res._object->_total_segments = adv.l;
	res._object->_encrypted = (adv.f & 0x01) != 0;
	res._object->_compressed = (adv.f & 0x02) != 0;
	res._object->_split = (adv.f & 0x04) != 0;
	res._object->_parts_recv.assign(adv.n, Bytes());
	res._object->_window = WINDOW;
	res._object->_consecutive_completed_height = -1;
	res._object->_received_count = 0;
	res._object->_outstanding_parts = 0;
	// SDU matches python: link.mtu - HEADER_MAXSIZE - IFAC_MIN_SIZE
	// (== Reticulum::MDU at default MTU). See sender-side note.
	res._object->_sdu = Type::Reticulum::MDU;
	if (res._object->_sdu == 0) res._object->_sdu = 200;

	// Register with the link so inbound RESOURCE packets dispatch to us.
	const_cast<Link&>(link).register_incoming_resource(res);

	// Kick off the first request.
	res.request_next();
	return res;
}

void Resource::request_next() {
	assert(_object);
	if (_object->_status == Type::Resource::FAILED) return;

	uint8_t exhausted_flag = HASHMAP_IS_NOT_EXHAUSTED;
	Bytes requested_hashes;
	size_t pn = (_object->_consecutive_completed_height >= 0)
	            ? (size_t)(_object->_consecutive_completed_height + 1) : 0;
	size_t emitted = 0;
	for (size_t step = 0; step < _object->_window && pn < _object->_total_parts; ++step, ++pn) {
		if (_object->_parts_recv[pn].size() == 0) {
			Bytes mh = _object->_hashmap.mid(pn * MAPHASH_LEN, MAPHASH_LEN);
			requested_hashes << mh;
			emitted++;
		}
	}
	_object->_outstanding_parts = emitted;

	// Build request payload: [flag][resource_hash][requested_hashes...]
	Bytes payload;
	payload << (uint8_t)exhausted_flag;
	payload << _object->_hash;
	payload << requested_hashes;

	Packet req_packet(_object->_link, payload, Type::Packet::DATA, Type::Packet::RESOURCE_REQ);
	req_packet.send();
}

void Resource::receive_part(const Packet& packet) {
	assert(_object);
	if (_object->_status == Type::Resource::FAILED) return;

	// Prefer the link-decrypted plaintext set by Link.cpp dispatcher.
	const Bytes& part_data = const_cast<Packet&>(packet).plaintext().size()
	                          ? const_cast<Packet&>(packet).plaintext()
	                          : packet.data();
	Bytes part_hash = get_map_hash(part_data);

	// Find which part this map_hash matches in the window.
	size_t consecutive_index = (_object->_consecutive_completed_height >= 0)
	                           ? (size_t)_object->_consecutive_completed_height : 0;
	size_t i = consecutive_index;
	for (size_t step = 0; step < _object->_window && i < _object->_total_parts; ++step) {
		Bytes mh = _object->_hashmap.mid(i * MAPHASH_LEN, MAPHASH_LEN);
		if (mh == part_hash && _object->_parts_recv[i].size() == 0) {
			_object->_parts_recv[i] = part_data;
			_object->_received_count++;
			if (_object->_outstanding_parts > 0) _object->_outstanding_parts--;
			// Update consecutive_completed_height pointer.
			if ((int64_t)i == _object->_consecutive_completed_height + 1) {
				_object->_consecutive_completed_height = (int64_t)i;
			}
			int64_t cp = _object->_consecutive_completed_height + 1;
			while ((size_t)cp < _object->_total_parts && _object->_parts_recv[(size_t)cp].size() > 0) {
				_object->_consecutive_completed_height = cp;
				cp++;
			}
			break;
		}
		++i;
	}

	if (_object->_received_count >= _object->_total_parts) {
		_object->_status = Type::Resource::ASSEMBLING;
		assemble();
	}
	else if (_object->_outstanding_parts == 0) {
		request_next();
	}

	// Fire the user-supplied progress callback after each accepted part.
	// Same rationale as the sender-side firing in `request_part` —
	// without this the receiver's `progress_callback` parameter is dead
	// wiring. The callback gets `(float)_received_count / _total_parts`
	// via `get_progress()`, monotonically advancing toward 1.0.
	if (_object->_callbacks._progress) {
		try {
			_object->_callbacks._progress(*this);
		}
		catch (const std::exception& e) {
			ERRORF("Error while executing resource receiver progress callback: %s", e.what());
		}
	}
}

void Resource::assemble() {
	assert(_object);
	if (_object->_status == Type::Resource::FAILED) return;

	// Concatenate parts.
	Bytes stream;
	for (const auto& part : _object->_parts_recv) {
		stream << part;
	}

	// Decrypt (if encrypted, which is the common case — link auto-encryption).
	Bytes decrypted;
	if (_object->_encrypted) {
		decrypted = const_cast<Link&>(_object->_link).decrypt(stream);
	} else {
		decrypted = stream;
	}

	// Strip random_hash prefix (RANDOM_HASH_SIZE bytes).
	if (decrypted.size() < RANDOM_HASH_SIZE) {
		ERROR("Resource::assemble: decrypted stream shorter than random hash prefix");
		_object->_status = Type::Resource::CORRUPT;
		return;
	}
	Bytes data = decrypted.mid(RANDOM_HASH_SIZE);

	// Decompress if the sender flagged the resource as compressed
	// (python LXMF auto-compresses with bz2 when it shrinks the
	// payload). The conformance test deliberately uses highly-redundant
	// content which always triggers the compress path.
	if (_object->_compressed) {
		Bytes decompressed = Cryptography::bz2_decompress(data);
		if (decompressed.size() == 0) {
			ERROR("Resource::assemble: bz2_decompress failed");
			_object->_status = Type::Resource::CORRUPT;
			return;
		}
		data = decompressed;
	}

	// Verify hash := full_hash(data || random_hash)
	Bytes hash_input;
	hash_input << data;
	hash_input << _object->_random_hash;
	Bytes calc = Identity::full_hash(hash_input);
	if (calc != _object->_hash) {
		ERROR("Resource::assemble: hash mismatch — corrupt resource");
		_object->_status = Type::Resource::CORRUPT;
		return;
	}

	_object->_data = data;
	_object->_status = Type::Resource::COMPLETE;

	// Send proof.
	prove();

	// Fire concluded callback.
	if (_object->_callbacks._concluded) {
		try { _object->_callbacks._concluded(*this); }
		catch (const std::exception& e) {
			ERRORF("Resource concluded (RX) callback threw: %s", e.what());
		}
	}
}

void Resource::prove() {
	assert(_object);
	if (_object->_status == Type::Resource::FAILED) return;
	// proof := full_hash(data || hash); proof_data wire = hash || proof
	Bytes proof_input;
	proof_input << _object->_data;
	proof_input << _object->_hash;
	Bytes proof = Identity::full_hash(proof_input);
	Bytes proof_data;
	proof_data << _object->_hash;
	proof_data << proof;
	Packet proof_packet(_object->_link, proof_data, Type::Packet::PROOF, Type::Packet::RESOURCE_PRF);
	proof_packet.send();
	DEBUGF("Resource: sent proof for hash=%s", _object->_hash.toHex().c_str());
}

const Bytes Resource::get_map_hash(const Bytes& data) const {
	assert(_object);
	Bytes input;
	input << data;
	input << _object->_random_hash;
	return Identity::full_hash(input).left(MAPHASH_LEN);
}

void Resource::cancel() {
	if (_object) _object->_status = Type::Resource::FAILED;
}

float Resource::get_progress() const {
	if (!_object || _object->_total_parts == 0) return 0.0f;
	if (_object->_initiator) {
		return (float)_object->_sent_parts / (float)_object->_total_parts;
	}
	return (float)_object->_received_count / (float)_object->_total_parts;
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
	if (!_object) return "";
	return "{Resource: " + _object->_hash.toHex() + "}";
}

const Bytes& Resource::hash() const          { assert(_object); return _object->_hash; }
const Bytes& Resource::request_id() const     { assert(_object); return _object->_request_id; }
const Bytes& Resource::data() const           { assert(_object); return _object->_data; }
const Type::Resource::status Resource::status() const { assert(_object); return _object->_status; }
const size_t Resource::size() const           { assert(_object); return _object->_size; }
const size_t Resource::total_size() const     { assert(_object); return _object->_total_size; }
const Link& Resource::link() const            { assert(_object); return _object->_link; }
