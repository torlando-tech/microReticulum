#include "Destination.h"

#include "Transport.h"
#include "Interface.h"
#include "Packet.h"
#include "Log.h"

#include <vector>
#include <time.h>
#include <string.h>

using namespace RNS;
using namespace RNS::Type::Destination;
using namespace RNS::Utilities;

Destination::Destination(const Identity& identity, const directions direction, const types type, const char* app_name, const char* aspects) : _object(new Object(identity)) {
	assert(_object);
	MEM("Destination object creating..., this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));

	// Check input values and build name string
	if (strchr(app_name, '.') != nullptr) {
		throw std::invalid_argument("Dots can't be used in app names");
	}
	//TRACE("Destination::Destination: app name: " + std::string(app_name));

	_object->_type = type;
	_object->_direction = direction;

	std::string fullaspects(aspects);
	if (!identity && direction == IN && _object->_type != PLAIN) {
		TRACE("Destination::Destination: identity not provided, creating new one");
		_object->_identity = Identity();
		// CBA TODO determine why identity.hexhash is added both here and by expand_name called below
		fullaspects += "." + _object->_identity.hexhash();
	}
	//TRACE("Destination::Destination: full aspects: " + fullaspects);

	if (_object->_identity && _object->_type == PLAIN) {
		throw std::invalid_argument("Selected destination type PLAIN cannot hold an identity");
	}

	_object->_name = expand_name(_object->_identity, app_name, fullaspects.c_str());
	//TRACE("Destination::Destination: name: " + _object->_name);

	// Generate the destination address hash
	//TRACE("Destination::Destination: creating hash...");
	_object->_hash = hash(_object->_identity, app_name, fullaspects.c_str());
	_object->_hexhash = _object->_hash.toHex();
	TRACE("Destination::Destination: hash: " + _object->_hash.toHex());
	//TRACE("Destination::Destination: creating name hash...");
    //p self.name_hash = RNS.Identity.full_hash(self.expand_name(None, app_name, *aspects).encode("utf-8"))[:(RNS.Identity.NAME_HASH_LENGTH//8)]
	_object->_name_hash = name_hash(app_name, aspects);
	//TRACE("Destination::Destination: name hash: " + _object->_name_hash.toHex());

	//TRACE("Destination::Destination: calling register_destination");
	Transport::register_destination(*this);

	MEM("Destination object created, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
}

Destination::Destination(const Identity& identity, const Type::Destination::directions direction, const Type::Destination::types type, const Bytes& hash) : _object(new Object(identity)) {
	assert(_object);
	MEM("Destination object creating..., this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));

	_object->_type = type;
	_object->_direction = direction;

	if (_object->_identity && _object->_type == PLAIN) {
		throw std::invalid_argument("Selected destination type PLAIN cannot hold an identity");
	}

	_object->_hash = hash;
	_object->_hexhash = _object->_hash.toHex();
	TRACE("Destination::Destination: hash: " + _object->_hash.toHex());
	//TRACE("Destination::Destination: creating name hash...");
    //p self.name_hash = RNS.Identity.full_hash(self.expand_name(None, app_name, *aspects).encode("utf-8"))[:(RNS.Identity.NAME_HASH_LENGTH//8)]
	_object->_name_hash = name_hash("unknown", "unknown");
	//TRACE("Destination::Destination: name hash: " + _object->_name_hash.toHex());

	//TRACE("Destination::Destination: calling register_destination");
	Transport::register_destination(*this);

	MEM("Destination object created, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
}

/*virtual*/ Destination::~Destination() {
	MEM("Destination object destroyed, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
	if (_object && _object.use_count() == 1) {
		MEM("Destination object has last data reference");

		// CBA Can't call deregister_destination here because it's possible (likely even) that Destination
		//  is being destructed from that same collection which will result in a llop and memory errors.
		//TRACE("Destination::~Destination: calling deregister_destination");
		//Transport::deregister_destination(*this);
	}
}

/*
:returns: A destination name in adressable hash form, for an app_name and a number of aspects.
*/
/*static*/ Bytes Destination::hash(const Identity& identity, const char* app_name, const char* aspects) {
	//p name_hash = Identity::full_hash(Destination.expand_name(None, app_name, *aspects).encode("utf-8"))[:(RNS.Identity.NAME_HASH_LENGTH//8)]
	//p addr_hash_material = name_hash
	Bytes addr_hash_material = name_hash(app_name, aspects);
	if (identity) {
		addr_hash_material << identity.hash();
	}

    //p return RNS.Identity.full_hash(addr_hash_material)[:RNS.Reticulum.TRUNCATED_HASHLENGTH//8]
	// CBA TODO valid alternative?
	//return Identity::full_hash(addr_hash_material).left(Type::Reticulum::TRUNCATED_HASHLENGTH/8);
	return Identity::truncated_hash(addr_hash_material);
}

/*
:returns: A name in hash form, for an app_name and a number of aspects.
*/
/*static*/ Bytes Destination::name_hash(const char* app_name, const char* aspects) {
	//p name_hash = Identity::full_hash(Destination.expand_name(None, app_name, *aspects).encode("utf-8"))[:(RNS.Identity.NAME_HASH_LENGTH//8)]
	return Identity::full_hash(expand_name({Type::NONE}, app_name, aspects)).left(Type::Identity::NAME_HASH_LENGTH/8);
}

/*
:returns: A tuple containing the app name and a list of aspects, for a full-name string.
*/
/*static*/ std::vector<std::string> Destination::app_and_aspects_from_name(const char* full_name) {
	//p components = full_name.split(".")
	//p return (components[0], components[1:])
	std::vector<std::string> components;
	std::string name(full_name);
	std::size_t pos = name.find('.');
	components.push_back(name.substr(0, pos));
	if (pos != std::string::npos) {
		components.push_back(name.substr(pos+1));
	}
	return components;
}

/*
:returns: A destination name in adressable hash form, for a full name string and Identity instance.
*/
/*static*/ Bytes Destination::hash_from_name_and_identity(const char* full_name, const Identity& identity) {
	//p app_name, aspects = Destination.app_and_aspects_from_name(full_name)
	//p return Destination.hash(identity, app_name, *aspects)
	std::vector<std::string> components = app_and_aspects_from_name(full_name);
	if (components.size() == 0) {
		return {Bytes::NONE};
	}
	if (components.size() == 1) {
		return hash(identity, components[0].c_str(), "");
	}
	return hash(identity, components[0].c_str(), components[1].c_str());
}

/*
:returns: A string containing the full human-readable name of the destination, for an app_name and a number of aspects.
*/
/*static*/ std::string Destination::expand_name(const Identity& identity, const char* app_name, const char* aspects) {

	if (strchr(app_name, '.') != nullptr) {
		throw std::invalid_argument("Dots can't be used in app names");
	}

	std::string name(app_name);

	if (aspects != nullptr) {
		name += std::string(".") + aspects;
	}

	if (identity) {
		name += "." + identity.hexhash();
	}

	return name;
}

/*
Creates an announce packet for this destination and broadcasts it on all
relevant interfaces. Application specific data can be added to the announce.

:param app_data: *bytes* containing the app_data.
:param path_response: Internal flag used by :ref:`RNS.Transport<api-transport>`. Ignore.
*/
//Packet Destination::announce(const Bytes& app_data /*= {}*/, bool path_response /*= false*/, const Interface& attached_interface /*= {Type::NONE}*/, const Bytes& tag /*= {}*/, bool send /*= true*/) {
Packet Destination::announce(const Bytes& app_data, bool path_response, const Interface& attached_interface, const Bytes& tag /*= {}*/, bool send /*= true*/) {
	assert(_object);
	TRACE("Destination::announce: announcing destination...");

	if (_object->_type != SINGLE) {
		throw std::invalid_argument("Only SINGLE destination types can be announced");
	}

	if (_object->_direction != IN) {
		throw std::invalid_argument("Only IN destination types can be announced");
	}

	double now = OS::time();
	// Clean up expired path responses from fixed pool
	for (size_t i = 0; i < Object::PATH_RESPONSES_SIZE; i++) {
		if (_object->_path_responses[i].in_use) {
			PathResponse& entry = _object->_path_responses[i].response;
			if (now > (entry.first + PR_TAG_WINDOW)) {
				_object->_path_responses[i].clear();
			}
		}
	}

	Bytes announce_data;

/*
	// CBA TEST
	TRACE("Destination::announce: performing path test...");
	TRACE("Destination::announce: inserting path...");
	_object->_path_responses.insert({Bytes("foo_tag"), {0, Bytes("this is foo tag")}});
	TRACE("Destination::announce: inserting path...");
	_object->_path_responses.insert({Bytes("test_tag"), {0, Bytes("this is test tag")}});
	if (path_response) {
		TRACE("Destination::announce: path_response is true");
	}
	if (!tag.empty()) {
		TRACE("Destination::announce: tag is specified");
		std::string tagstr((const char*)tag.data(), tag.size());
		DEBUG(std::string("Destination::announce: tag: ") + tagstr);
		DEBUG(std::string("Destination::announce: tag len: ") + std::to_string(tag.size()));
		TRACE("Destination::announce: searching for tag...");
		if (_object->_path_responses.find(tag) != _object->_path_responses.end()) {
			TRACE("Destination::announce: found tag in _path_responses");
			DEBUG(std::string("Destination::announce: data: ") +_object->_path_responses[tag].second.toString());
		}
		else {
			TRACE("Destination::announce: tag not found in _path_responses");
		}
	}
	TRACE("Destination::announce: path test finished");
*/

	// Search for existing path response in fixed pool
	Object::PathResponseSlot* found_slot = nullptr;
	if (path_response && !tag.empty()) {
		for (size_t i = 0; i < Object::PATH_RESPONSES_SIZE; i++) {
			if (_object->_path_responses[i].in_use && _object->_path_responses[i].tag == tag) {
				found_slot = &_object->_path_responses[i];
				break;
			}
		}
	}
	if (found_slot) {
		// This code is currently not used, since Transport will block duplicate
		// path requests based on tags. When multi-path support is implemented in
		// Transport, this will allow Transport to detect redundant paths to the
		// same destination, and select the best one based on chosen criteria,
		// since it will be able to detect that a single emitted announce was
		// received via multiple paths. The difference in reception time will
		// potentially also be useful in determining characteristics of the
		// multiple available paths, and to choose the best one.
		//z TRACE("Using cached announce data for answering path request with tag "+RNS.prettyhexrep(tag));
		announce_data << found_slot->response.second;
	}
	else {
		Bytes destination_hash = _object->_hash;
		//p random_hash = Identity::get_random_hash()[0:5] + int(time.time()).to_bytes(5, "big")
		// Generate 5 random bytes + 5 bytes of current timestamp (big-endian)
		// This matches Python RNS format for announce emission time tracking
		Bytes random_bytes = Cryptography::random(5);  // First 5 bytes are random
		uint8_t time_bytes[5];
		uint64_t timestamp = static_cast<uint64_t>(OS::time());
		OS::to_bytes_big_endian(timestamp, time_bytes, 5);  // Last 5 bytes are timestamp
		Bytes random_hash = random_bytes + Bytes(time_bytes, 5);

		Bytes new_app_data(app_data);
        if (new_app_data.empty() && !_object->_default_app_data.empty()) {
			new_app_data = _object->_default_app_data;
		}

		Bytes signed_data;
		//TRACE("Destination::announce: hash:         " + _object->_hash.toHex());
		//TRACE("Destination::announce: public key:   " + _object->_identity.get_public_key().toHex());
		//TRACE("Destination::announce: name hash:    " + _object->_name_hash.toHex());
		//TRACE("Destination::announce: random hash:  " + random_hash.toHex());
		//TRACE("Destination::announce: app data:     " + new_app_data.toHex());
		//TRACE("Destination::announce: app data text:" + new_app_data.toString());
		signed_data << _object->_hash << _object->_identity.get_public_key() << _object->_name_hash << random_hash;
		if (new_app_data) {
			signed_data << new_app_data;
		}
		//TRACE("Destination::announce: signed data:  " + signed_data.toHex());

		Bytes signature(_object->_identity.sign(signed_data));
		//TRACE("Destination::announce: signature:    " + signature.toHex());

		announce_data << _object->_identity.get_public_key() << _object->_name_hash << random_hash << signature;

		// Include ratchet public key if ratchets are enabled
		if (_object->_ratchets_enabled && _object->_ratchets_count > 0) {
			Bytes ratchet_pub = get_ratchet_public_bytes();
			if (ratchet_pub) {
				DEBUG("Including ratchet in announce for " + _object->_hexhash);
				DEBUG("  Ratchet public key: " + ratchet_pub.toHex());
				announce_data << ratchet_pub;
			}
		}

		if (new_app_data) {
			announce_data << new_app_data;
		}

		// CBA ACCUMULATES - insert into fixed pool
		for (size_t i = 0; i < Object::PATH_RESPONSES_SIZE; i++) {
			if (!_object->_path_responses[i].in_use) {
				_object->_path_responses[i].in_use = true;
				_object->_path_responses[i].tag = tag;
				_object->_path_responses[i].response = {OS::time(), announce_data};
				break;
			}
		}
	}
	//TRACE("Destination::announce: announce_data:" + announce_data.toHex());

	Type::Packet::context_types announce_context = Type::Packet::CONTEXT_NONE;
	if (path_response) {
		announce_context = Type::Packet::PATH_RESPONSE;
	}

	//TRACE("Destination::announce: creating announce packet...");
    //p announce_packet = RNS.Packet(self, announce_data, RNS.Packet.ANNOUNCE, context = announce_context, attached_interface = attached_interface)
	//Packet announce_packet(*this, announce_data, Type::Packet::ANNOUNCE, announce_context, Type::Transport::BROADCAST, Type::Packet::HEADER_1, nullptr, attached_interface);
	Packet announce_packet(*this, attached_interface, announce_data, Type::Packet::ANNOUNCE, announce_context, Type::Transport::BROADCAST, Type::Packet::HEADER_1);

	if (send) {
		TRACE("Destination::announce: sending announce packet...");
		announce_packet.send();
		return {Type::NONE};
	}
	else {
		return announce_packet;
	}
}

Packet Destination::announce(const Bytes& app_data /*= {}*/, bool path_response /*= false*/) {
	return announce(app_data, path_response, {Type::NONE});
}


/*
Registers a request handler.

:param path: The path for the request handler to be registered.
:param response_generator: A function or method with the signature *response_generator(path, data, request_id, link_id, remote_identity, requested_at)* to be called. Whatever this funcion returns will be sent as a response to the requester. If the function returns ``None``, no response will be sent.
:param allow: One of ``RNS.Destination.ALLOW_NONE``, ``RNS.Destination.ALLOW_ALL`` or ``RNS.Destination.ALLOW_LIST``. If ``RNS.Destination.ALLOW_LIST`` is set, the request handler will only respond to requests for identified peers in the supplied list.
:param allowed_list: A list of *bytes-like* :ref:`RNS.Identity<api-identity>` hashes.
:raises: ``ValueError`` if any of the supplied arguments are invalid.
*/
/*
void Destination::register_request_handler(const Bytes& path, response_generator = None, request_policies allow = ALLOW_NONE, allowed_list = None) {
	if path == None or path == "":
		raise ValueError("Invalid path specified")
	elif not callable(response_generator):
		raise ValueError("Invalid response generator specified")
	elif not allow in Destination.request_policies:
		raise ValueError("Invalid request policy")
	else:
		path_hash = RNS.Identity.truncated_hash(path.encode("utf-8"))
		request_handler = [path, response_generator, allow, allowed_list]
		self.request_handlers[path_hash] = request_handler
}
*/

/*
Deregisters a request handler.

:param path: The path for the request handler to be deregistered.
:returns: True if the handler was deregistered, otherwise False.
*/
/*
bool Destination::deregister_request_handler(const Bytes& path) {
	path_hash = RNS.Identity.truncated_hash(path.encode("utf-8"))
	if path_hash in self.request_handlers:
		self.request_handlers.pop(path_hash)
		return True
	else:
		return False
}
*/

void Destination::receive(const Packet& packet) {
	assert(_object);
	if (packet.packet_type() == Type::Packet::LINKREQUEST) {
		Bytes plaintext(packet.data());
		incoming_link_request(plaintext, packet);
	}
	else {
		// CBA TODO Why isn't the Packet decrypting itself?
		Bytes plaintext(decrypt(packet.data()));
		//TRACE("Destination::receive: decrypted data: " + plaintext.toHex());
		if (plaintext) {
			if (packet.packet_type() == Type::Packet::DATA) {
				if (_object->_callbacks._packet) {
					try {
						_object->_callbacks._packet(plaintext, packet);
					}
					catch (std::exception& e) {
						DEBUG("Error while executing receive callback from " + toString() + ". The contained exception was: " + e.what());
					}
				}
			}
		}
	}
}

void Destination::incoming_link_request(const Bytes& data, const Packet& packet) {
	assert(_object);
	INFO(">>> Destination::incoming_link_request: entry, accept_link_requests=" + std::to_string(_object->_accept_link_requests));
	if (_object->_accept_link_requests) {
		INFO(">>> Destination::incoming_link_request: calling Link::validate_request");
		RNS::Link link = Link::validate_request(*this, data, packet);
		if (link) {
			INFO(">>> Destination::incoming_link_request: link validated, inserting into links set");
			_object->_links.insert(link);
			INFO(">>> Destination::incoming_link_request: link inserted, now have " + std::to_string(_object->_links.size()) + " links");
		} else {
			INFO(">>> Destination::incoming_link_request: link validation failed");
		}
	}
	INFO(">>> Destination::incoming_link_request: exit");
}

/*
Encrypts information for ``RNS.Destination.SINGLE`` or ``RNS.Destination.GROUP`` type destination.

:param plaintext: A *bytes-like* containing the plaintext to be encrypted.
:raises: ``ValueError`` if destination does not hold a necessary key for encryption.
*/
/*virtual*/ const Bytes Destination::encrypt(const Bytes& data) {
	assert(_object);
	TRACE("Destination::encrypt: encrypting data...");

	if (_object->_type == PLAIN) {
		return data;
	}

	if (_object->_type == SINGLE && _object->_identity) {
		return _object->_identity.encrypt(data);
	}

// TODO
/*
	if (_object->_type == GROUP {
		if hasattr(self, "prv") and self.prv != None:
			try:
				return self.prv.encrypt(plaintext)
			except Exception as e:
				RNS.log("The GROUP destination could not encrypt data", RNS.LOG_ERROR)
				RNS.log("The contained exception was: "+str(e), RNS.LOG_ERROR)
		else:
			raise ValueError("No private key held by GROUP destination. Did you create or load one?")
	}
*/

	// CBA Reference implementation does not handle this default case
	// CBA TODO Determine of returning plaintext is appropriate here (for now it's necessary for PROOF packets)
	return data;
}

/*
Decrypts information for ``RNS.Destination.SINGLE`` or ``RNS.Destination.GROUP`` type destination.

:param ciphertext: *Bytes* containing the ciphertext to be decrypted.
:raises: ``ValueError`` if destination does not hold a necessary key for decryption.
*/
/*virtual*/ const Bytes Destination::decrypt(const Bytes& data) {
	assert(_object);
	TRACE("Destination::decrypt: decrypting data...");

	if (_object->_type == PLAIN) {
		return data;
	}

	if (_object->_type == SINGLE && _object->_identity) {
		return _object->_identity.decrypt(data);
	}

/*
	if (_object->_type == GROUP) {
		if hasattr(self, "prv") and self.prv != None:
			try:
				return self.prv.decrypt(ciphertext)
			except Exception as e:
				RNS.log("The GROUP destination could not decrypt data", RNS.LOG_ERROR)
				RNS.log("The contained exception was: "+str(e), RNS.LOG_ERROR)
		else:
			raise ValueError("No private key held by GROUP destination. Did you create or load one?")
	}
*/
	// MOCK
	return {Bytes::NONE};
}

/*
Signs information for ``RNS.Destination.SINGLE`` type destination.

:param message: *Bytes* containing the message to be signed.
:returns: A *bytes-like* containing the message signature, or *None* if the destination could not sign the message.
*/
/*virtual*/ const Bytes Destination::sign(const Bytes& message) {
	assert(_object);
	if (_object->_type == SINGLE && _object->_identity) {
		return _object->_identity.sign(message);
	}
	return {Bytes::NONE};
}

bool Destination::has_link(const Link& link) {
	assert(_object);
	return _object->_links.find(link) != _object->_links.end();
}

void Destination::remove_link(const Link& link) {
	assert(_object);
	_object->_links.erase(link);
}

// Ratchet support for forward secrecy

/*
Enable ratchets for this destination to provide forward secrecy.

Ratchets rotate X25519 keys at regular intervals (default 30 minutes).
When enabled, the destination will encrypt packets using ratchets instead
of the long-term identity key.

:param ratchets_path: Path to directory where ratchets will be persisted
*/
void Destination::enable_ratchets(const char* ratchets_path) {
	assert(_object);

	if (_object->_ratchets_enabled) {
		WARNING("Ratchets already enabled for destination " + _object->_hexhash);
		return;
	}

	_object->_ratchets_path = ratchets_path;
	_object->_ratchets_enabled = true;

	INFO("Enabling ratchets for destination " + _object->_hexhash);
	DEBUG("  Ratchets path: " + _object->_ratchets_path);

	// Try to load existing ratchets from storage
	// TODO: Implement _load_ratchets() to load from JSON file

	// If no ratchets exist, create the first one
	if (_object->_ratchets_count == 0) {
		rotate_ratchets();
	}
}

/*
Disable ratchets for this destination.

After disabling, the destination will fall back to identity-based encryption.
*/
void Destination::disable_ratchets() {
	assert(_object);

	if (!_object->_ratchets_enabled) {
		return;
	}

	INFO("Disabling ratchets for destination " + _object->_hexhash);

	_object->_ratchets_enabled = false;
	ratchets_clear();
	_object->_latest_ratchet_id = {Bytes::NONE};
	_object->_latest_ratchet_time = 0.0;
}

/*
Rotate ratchets for this destination.

Creates a new ratchet and adds it to the circular buffer.
Maintains a maximum of 128 ratchets (oldest are overwritten).

Ratchets are automatically rotated based on _ratchet_interval (default 30 minutes).
*/
void Destination::rotate_ratchets(bool force) {
	assert(_object);

	if (!_object->_ratchets_enabled) {
		WARNING("Cannot rotate ratchets - ratchets not enabled for destination " + _object->_hexhash);
		return;
	}

	double current_time = Utilities::OS::time();

	// Check if enough time has elapsed since last rotation (unless forced)
	if (!force && _object->_ratchets_count > 0 &&
	    (current_time - _object->_latest_ratchet_time) < _object->_ratchet_interval) {
		DEBUG("Skipping ratchet rotation - interval not elapsed");
		DEBUG("  Time since last: " + std::to_string(current_time - _object->_latest_ratchet_time) + "s");
		DEBUG("  Interval: " + std::to_string(_object->_ratchet_interval) + "s");
		return;
	}

	INFO("Rotating ratchets for destination " + _object->_hexhash);

	// Generate new ratchet
	Cryptography::Ratchet new_ratchet = Cryptography::Ratchet::generate();

	// Add to circular buffer
	ratchets_add(new_ratchet);

	// Update latest ratchet tracking
	_object->_latest_ratchet_id = new_ratchet.get_id();
	_object->_latest_ratchet_time = current_time;

	DEBUG("  Total ratchets: " + std::to_string(_object->_ratchets_count));
	DEBUG("  Latest ratchet ID: " + _object->_latest_ratchet_id.toHex());

	// Persist ratchets to storage
	// TODO: Implement _persist_ratchets() to save to JSON file
}

/*
Get the latest ratchet ID for this destination.

:returns: Ratchet ID (10 bytes), or empty Bytes if ratchets not enabled
*/
Bytes Destination::get_latest_ratchet_id() const {
	assert(_object);

	if (!_object->_ratchets_enabled || _object->_ratchets_count == 0) {
		return {Bytes::NONE};
	}

	return _object->_latest_ratchet_id;
}

/*
Get the latest ratchet public bytes for inclusion in announces.

:returns: Ratchet public key (32 bytes), or empty Bytes if ratchets not enabled
*/
Bytes Destination::get_ratchet_public_bytes() const {
	assert(_object);

	if (!_object->_ratchets_enabled || _object->_ratchets_count == 0) {
		return {Bytes::NONE};
	}

	// Return public key of latest ratchet (most recently added)
	// The most recent ratchet is at the tail of the circular buffer
	// tail = (head + count - 1) % size, but since we're adding at the "front" logically,
	// the newest entry is the one before head (or wrapped around)
	size_t newest_idx = (_object->_ratchets_head + Object::RATCHETS_SIZE - 1) % Object::RATCHETS_SIZE;
	return _object->_ratchets[newest_idx].public_bytes();
}

/*
Add a ratchet to the circular buffer.

:param ratchet: The ratchet to add
:returns: true if added successfully
*/
bool Destination::ratchets_add(const Cryptography::Ratchet& ratchet) {
	assert(_object);

	// Calculate the insertion index (one before head, wrapping around)
	// This inserts at the "front" so newest entries are easily accessible
	size_t insert_idx;
	if (_object->_ratchets_count == 0) {
		insert_idx = 0;
		_object->_ratchets_head = 0;
	} else {
		insert_idx = (_object->_ratchets_head + Object::RATCHETS_SIZE - 1) % Object::RATCHETS_SIZE;
		_object->_ratchets_head = insert_idx;
	}

	_object->_ratchets[insert_idx] = ratchet;

	if (_object->_ratchets_count < Object::RATCHETS_SIZE) {
		_object->_ratchets_count++;
	}
	// When count == RATCHETS_SIZE, we're overwriting the oldest entry (which was at head)

	return true;
}

/*
Find a ratchet by its public key.

:param public_key: The public key to search for
:returns: Pointer to the ratchet if found, nullptr otherwise
*/
const Cryptography::Ratchet* Destination::ratchets_find(const Bytes& public_key) const {
	assert(_object);

	for (size_t i = 0; i < _object->_ratchets_count; i++) {
		size_t idx = (_object->_ratchets_head + i) % Object::RATCHETS_SIZE;
		if (_object->_ratchets[idx].public_bytes() == public_key) {
			return &_object->_ratchets[idx];
		}
	}

	return nullptr;
}

/*
Clear all ratchets from the circular buffer.
*/
void Destination::ratchets_clear() {
	assert(_object);

	// Reset to default-constructed ratchets
	for (size_t i = 0; i < Object::RATCHETS_SIZE; i++) {
		_object->_ratchets[i] = Cryptography::Ratchet();
	}
	_object->_ratchets_head = 0;
	_object->_ratchets_count = 0;
}
