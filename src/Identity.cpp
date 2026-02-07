#include "Identity.h"
#include "FileStream.h"

#include "Reticulum.h"
#include "Transport.h"
#include "Packet.h"
#include "Log.h"
#include "Utilities/OS.h"
#include "Cryptography/Ed25519.h"
#include "Cryptography/X25519.h"
#include "Cryptography/HKDF.h"
#include "Cryptography/Token.h"
#include "Cryptography/Random.h"

#include <algorithm>
#include <string.h>
#ifdef ARDUINO
#include <Esp.h>  // For ESP.getMaxAllocHeap()
#include <esp_heap_caps.h>  // For heap_caps_malloc
#endif

using namespace RNS;
using namespace RNS::Type::Identity;
using namespace RNS::Cryptography;
using namespace RNS::Utilities;

// Pool for known destinations - allocated in PSRAM to free ~29KB internal RAM
/*static*/ Identity::KnownDestinationSlot* Identity::_known_destinations_pool = nullptr;
/*static*/ bool Identity::_saving_known_destinations = false;
/*static*/ uint16_t Identity::_known_destinations_maxsize = 2048;  // Matches KNOWN_DESTINATIONS_SIZE

// Initialize known destinations pool in PSRAM
/*static*/ bool Identity::init_known_destinations_pool() {
	if (_known_destinations_pool != nullptr) {
		return true;  // Already initialized
	}

	size_t pool_size = KNOWN_DESTINATIONS_SIZE * sizeof(KnownDestinationSlot);

#ifdef ARDUINO
	// Allocate from PSRAM (SPIRAM) with 8-byte alignment for double members
	// Use heap_caps_aligned_alloc for proper alignment
	_known_destinations_pool = (KnownDestinationSlot*)heap_caps_aligned_alloc(8, pool_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (_known_destinations_pool) {
		INFO("Identity: Known destinations pool allocated in PSRAM (" + std::to_string(pool_size) + " bytes)");
	} else {
		// Fall back to internal RAM with alignment
		_known_destinations_pool = (KnownDestinationSlot*)heap_caps_aligned_alloc(8, pool_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		if (_known_destinations_pool) {
			WARNING("Identity: Known destinations pool allocated in internal RAM (PSRAM unavailable)");
		}
	}
#else
	_known_destinations_pool = new KnownDestinationSlot[KNOWN_DESTINATIONS_SIZE];
#endif

	if (!_known_destinations_pool) {
		ERROR("Identity: Failed to allocate known destinations pool!");
		return false;
	}

	// Initialize all slots using placement new to properly construct objects
	for (size_t i = 0; i < KNOWN_DESTINATIONS_SIZE; ++i) {
		new (&_known_destinations_pool[i]) KnownDestinationSlot();
	}

	return true;
}

// Helper functions for known destinations pool
/*static*/ Identity::KnownDestinationSlot* Identity::find_known_destination_slot(const Bytes& hash) {
	if (!_known_destinations_pool) return nullptr;
	for (size_t i = 0; i < KNOWN_DESTINATIONS_SIZE; ++i) {
		if (_known_destinations_pool[i].in_use && _known_destinations_pool[i].hash_equals(hash)) {
			return &_known_destinations_pool[i];
		}
	}
	return nullptr;
}

/*static*/ Identity::KnownDestinationSlot* Identity::find_empty_known_destination_slot() {
	if (!_known_destinations_pool) return nullptr;
	for (size_t i = 0; i < KNOWN_DESTINATIONS_SIZE; ++i) {
		if (!_known_destinations_pool[i].in_use) {
			return &_known_destinations_pool[i];
		}
	}
	return nullptr;
}

/*static*/ size_t Identity::known_destinations_count() {
	if (!_known_destinations_pool) return 0;
	size_t count = 0;
	for (size_t i = 0; i < KNOWN_DESTINATIONS_SIZE; ++i) {
		if (_known_destinations_pool[i].in_use) {
			++count;
		}
	}
	return count;
}

// Ratchet cache static members - fixed pool for zero heap fragmentation
/*static*/ Identity::KnownRatchetSlot Identity::_known_ratchets_pool[Identity::KNOWN_RATCHETS_SIZE];
/*static*/ bool Identity::_saving_known_ratchets = false;

// Helper functions for known ratchets pool
/*static*/ Identity::KnownRatchetSlot* Identity::find_known_ratchet_slot(const Bytes& dest_hash) {
	for (size_t i = 0; i < KNOWN_RATCHETS_SIZE; ++i) {
		if (_known_ratchets_pool[i].in_use && _known_ratchets_pool[i].hash_equals(dest_hash)) {
			return &_known_ratchets_pool[i];
		}
	}
	return nullptr;
}

/*static*/ Identity::KnownRatchetSlot* Identity::find_empty_known_ratchet_slot() {
	for (size_t i = 0; i < KNOWN_RATCHETS_SIZE; ++i) {
		if (!_known_ratchets_pool[i].in_use) {
			return &_known_ratchets_pool[i];
		}
	}
	return nullptr;
}

/*static*/ size_t Identity::known_ratchets_count() {
	size_t count = 0;
	for (size_t i = 0; i < KNOWN_RATCHETS_SIZE; ++i) {
		if (_known_ratchets_pool[i].in_use) count++;
	}
	return count;
}

Identity::Identity(bool create_keys /*= true*/) : _object(new Object()) {
	if (create_keys) {
		createKeys();
	}
	MEM("Identity object created, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
}

void Identity::createKeys() {
	assert(_object);

	// CRYPTO: create encryption private keys
	_object->_prv           = Cryptography::X25519PrivateKey::generate();
	_object->_prv_bytes     = _object->_prv->private_bytes();
	//TRACE("Identity::createKeys: prv bytes:     " + _object->_prv_bytes.toHex());

	// CRYPTO: create signature private keys
	_object->_sig_prv       = Cryptography::Ed25519PrivateKey::generate();
	_object->_sig_prv_bytes = _object->_sig_prv->private_bytes();
	//TRACE("Identity::createKeys: sig prv bytes: " + _object->_sig_prv_bytes.toHex());

	// CRYPTO: create encryption public keys
	_object->_pub           = _object->_prv->public_key();
	_object->_pub_bytes     = _object->_pub->public_bytes();
	//TRACE("Identity::createKeys: pub bytes:     " + _object->_pub_bytes.toHex());

	// CRYPTO: create signature public keys
	_object->_sig_pub       = _object->_sig_prv->public_key();
	_object->_sig_pub_bytes = _object->_sig_pub->public_bytes();
	//TRACE("Identity::createKeys: sig pub bytes: " + _object->_sig_pub_bytes.toHex());

	update_hashes();

	VERBOSE("Identity keys created for " + _object->_hash.toHex());
}

/*
Load a private key into the instance.

:param prv_bytes: The private key as *bytes*.
:returns: True if the key was loaded, otherwise False.
*/
bool Identity::load_private_key(const Bytes& prv_bytes) {
	assert(_object);

	try {

		//p self.prv_bytes     = prv_bytes[:Identity.KEYSIZE//8//2]
		_object->_prv_bytes     = prv_bytes.left(Type::Identity::KEYSIZE/8/2);
		_object->_prv           = X25519PrivateKey::from_private_bytes(_object->_prv_bytes);
		//TRACE("Identity::load_private_key: prv bytes:     " + _object->_prv_bytes.toHex());

		//p self.sig_prv_bytes = prv_bytes[Identity.KEYSIZE//8//2:]
		_object->_sig_prv_bytes = prv_bytes.mid(Type::Identity::KEYSIZE/8/2);
		_object->_sig_prv       = Ed25519PrivateKey::from_private_bytes(_object->_sig_prv_bytes);
		//TRACE("Identity::load_private_key: sig prv bytes: " + _object->_sig_prv_bytes.toHex());

		_object->_pub           = _object->_prv->public_key();
		_object->_pub_bytes     = _object->_pub->public_bytes();
		//TRACE("Identity::load_private_key: pub bytes:     " + _object->_pub_bytes.toHex());

		_object->_sig_pub       = _object->_sig_prv->public_key();
		_object->_sig_pub_bytes = _object->_sig_pub->public_bytes();
		//TRACE("Identity::load_private_key: sig pub bytes: " + _object->_sig_pub_bytes.toHex());

		update_hashes();

		return true;
	}
	catch (std::exception& e) {
		//p raise e
		ERROR("Failed to load identity key");
		ERRORF("The contained exception was: %s", e.what());
		return false;
	}
}

/*
Load a public key into the instance.

:param pub_bytes: The public key as *bytes*.
:returns: True if the key was loaded, otherwise False.
*/
void Identity::load_public_key(const Bytes& pub_bytes) {
	assert(_object);

	try {

		//_pub_bytes     = pub_bytes[:Identity.KEYSIZE//8//2]
		_object->_pub_bytes     = pub_bytes.left(Type::Identity::KEYSIZE/8/2);
		//TRACE("Identity::load_public_key: pub bytes:     " + _object->_pub_bytes.toHex());

		//_sig_pub_bytes = pub_bytes[Identity.KEYSIZE//8//2:]
		_object->_sig_pub_bytes = pub_bytes.mid(Type::Identity::KEYSIZE/8/2);
		//TRACE("Identity::load_public_key: sig pub bytes: " + _object->_sig_pub_bytes.toHex());

		_object->_pub           = X25519PublicKey::from_public_bytes(_object->_pub_bytes);
		_object->_sig_pub       = Ed25519PublicKey::from_public_bytes(_object->_sig_pub_bytes);

		update_hashes();
	}
	catch (std::exception& e) {
		ERRORF("Error while loading public key, the contained exception was: %s", e.what());
	}
}

bool Identity::load(const char* path) {
	TRACE("Reading identity key from storage...");
#if defined(RNS_USE_FS)
	try {
		Bytes prv_bytes;
		if (OS::read_file(path, prv_bytes) > 0) {
			return load_private_key(prv_bytes);
		}
		else {
			return false;
		}
	}
	catch (std::exception& e) {
		ERROR("Error while loading identity from " + std::string(path));
		ERRORF("The contained exception was: %s", e.what());
	}
#endif
	return false;
}

/*
Saves the identity to a file. This will write the private key to disk,
and anyone with access to this file will be able to decrypt all
communication for the identity. Be very careful with this method.

:param path: The full path specifying where to save the identity.
:returns: True if the file was saved, otherwise False.
*/
bool Identity::to_file(const char* path) {
	TRACE("Writing identity key to storage...");
#if defined(RNS_USE_FS)
	try {
		return (OS::write_file(path, get_private_key()) == get_private_key().size());
	}
	catch (std::exception& e) {
		ERRORF("Error while saving identity to %s", path);
		ERRORF("The contained exception was: %s", e.what());
	}
#endif
	return false;
}


/*
Create a new :ref:`RNS.Identity<api-identity>` instance from a file.
Can be used to load previously created and saved identities into Reticulum.

:param path: The full path to the saved :ref:`RNS.Identity<api-identity>` data
:returns: A :ref:`RNS.Identity<api-identity>` instance, or *None* if the loaded data was invalid.
*/
/*static*/ const Identity Identity::from_file(const char* path) {
	Identity identity(false);
	if (identity.load(path)) {
		return identity;
	}
	return {Type::NONE};
}

/*static*/ void Identity::remember(const Bytes& packet_hash, const Bytes& destination_hash, const Bytes& public_key, const Bytes& app_data /*= {Bytes::NONE}*/) {
	if (public_key.size() != Type::Identity::KEYSIZE/8) {
		throw std::invalid_argument("Can't remember " + destination_hash.toHex() + ", the public key size of " + std::to_string(public_key.size()) + " is not valid.");
	}
	else {
		// Proactively cull before pool gets too full (creates buffer room)
		cull_known_destinations();

		// Check if this is a new destination or updated app_data
		bool should_save = false;
		KnownDestinationSlot* slot = find_known_destination_slot(destination_hash);
		if (slot == nullptr) {
			// New destination - find empty slot
			slot = find_empty_known_destination_slot();
			if (slot == nullptr) {
				// Pool still full after cull - shouldn't happen but handle it
				WARNING("Known destinations pool is full, cannot remember " + destination_hash.toHex());
				return;
			}
			slot->in_use = true;
			slot->set_hash(destination_hash);
			slot->entry = IdentityEntry(OS::time(), packet_hash, public_key, app_data);
			should_save = true;
		} else if (app_data && app_data.size() > 0 && slot->entry._app_data != app_data) {
			// Update existing with new app_data
			slot->entry._app_data = app_data;
			slot->entry._timestamp = OS::time();
			should_save = true;
		}

		// Persist to storage if changed
		if (should_save) {
			save_known_destinations();
		}
	}
}

/*
Recall identity for a destination hash.

:param destination_hash: Destination hash as *bytes*.
:returns: An :ref:`RNS.Identity<api-identity>` instance that can be used to create an outgoing :ref:`RNS.Destination<api-destination>`, or *None* if the destination is unknown.
*/
/*static*/ Identity Identity::recall(const Bytes& destination_hash) {
	TRACE("Identity::recall...");
	KnownDestinationSlot* slot = find_known_destination_slot(destination_hash);
	if (slot != nullptr) {
		TRACE("Identity::recall: Found identity entry for destination " + destination_hash.toHex());
		const IdentityEntry& identity_data = slot->entry;
		Identity identity(false);
		identity.load_public_key(identity_data.public_key_bytes());
		identity.app_data(identity_data._app_data);
		return identity;
	}
	else {
		TRACE("Identity::recall: Unable to find identity entry for destination " + destination_hash.toHex() + ", performing destination lookup...");
		Destination registered_destination(Transport::find_destination_from_hash(destination_hash));
		if (registered_destination) {
			TRACE("Identity::recall: Found destination " + destination_hash.toHex());
			Identity identity(false);
			identity.load_public_key(registered_destination.identity().get_public_key());
			identity.app_data({Bytes::NONE});
			return identity;
		}
		TRACE("Identity::recall: Unable to find destination " + destination_hash.toHex());
		return {Type::NONE};
	}
}

/*
Recall last heard app_data for a destination hash.

:param destination_hash: Destination hash as *bytes*.
:returns: *Bytes* containing app_data, or *None* if the destination is unknown.
*/
/*static*/ Bytes Identity::recall_app_data(const Bytes& destination_hash) {
	TRACE("Identity::recall_app_data...");
	KnownDestinationSlot* slot = find_known_destination_slot(destination_hash);
	if (slot != nullptr) {
		TRACE("Identity::recall_app_data: Found identity entry for destination " + destination_hash.toHex());
		const IdentityEntry& identity_data = slot->entry;
		return identity_data._app_data;
	}
	else {
		TRACE("Identity::recall_app_data: Unable to find identity entry for destination " + destination_hash.toHex());
		return {Bytes::NONE};
	}
}

/*static*/ bool Identity::save_known_destinations() {
	if (!_known_destinations_pool) return false;

	// Binary format - much more memory efficient than JSON
	// No heap allocation needed - writes directly from fixed arrays
	const char* storage_path = "/known_dst.bin";

	bool success = false;
	try {
		if (_saving_known_destinations) {
			double wait_interval = 0.2;
			double wait_timeout = 5;
			double wait_start = OS::time();
			while (_saving_known_destinations) {
				OS::sleep(wait_interval);
				if (OS::time() > (wait_start + wait_timeout)) {
					ERROR("Could not save known destinations to storage, waiting for previous save operation timed out.");
					return false;
				}
			}
		}

		_saving_known_destinations = true;

		double save_start = OS::time();

		size_t dest_count = known_destinations_count();
		DEBUG("Saving " + std::to_string(dest_count) + " known destinations to storage (binary)...");

		// Open file for writing using OS filesystem abstraction
		FileStream file = OS::open_file(storage_path, FileStream::MODE_WRITE);
		if (!file) {
			ERROR("Failed to open known destinations file for writing");
			_saving_known_destinations = false;
			return false;
		}

		// Write header: magic (4) + version (1) + count (2) = 7 bytes
		const uint8_t magic[4] = {'K', 'D', 'S', 'T'};
		const uint8_t version = 1;
		uint16_t count = static_cast<uint16_t>(dest_count);

		file.write(magic, 4);
		file.write(&version, 1);
		file.write((const uint8_t*)&count, sizeof(uint16_t));

		// Write each entry directly from fixed arrays - no heap allocation!
		for (size_t i = 0; i < KNOWN_DESTINATIONS_SIZE; ++i) {
			if (!_known_destinations_pool[i].in_use) continue;
			const KnownDestinationSlot& slot = _known_destinations_pool[i];

			// destination_hash: 16 bytes
			file.write(slot.destination_hash, DEST_HASH_SIZE);
			// timestamp: 8 bytes
			file.write((const uint8_t*)&slot.entry._timestamp, sizeof(double));
			// packet_hash: 32 bytes
			file.write(slot.entry._packet_hash, PACKET_HASH_SIZE);
			// public_key: 64 bytes
			file.write(slot.entry._public_key, PUBLIC_KEY_SIZE);
			// app_data_len: 2 bytes + app_data: variable
			uint16_t app_data_len = static_cast<uint16_t>(slot.entry._app_data.size());
			file.write((const uint8_t*)&app_data_len, sizeof(uint16_t));
			if (app_data_len > 0) {
				file.write(slot.entry._app_data.data(), app_data_len);
			}
		}

		file.close();

		std::string time_str;
		double save_time = OS::time() - save_start;
		if (save_time < 1) {
			time_str = std::to_string((int)(save_time*1000)) + " ms";
		}
		else {
			time_str = std::to_string(OS::round(save_time, 1)) + " s";
		}

		DEBUG("Saved " + std::to_string(dest_count) + " known destinations in " + time_str);

		success = true;
	}
	catch (std::exception& e) {
		ERRORF("Error while saving known destinations to disk, the contained exception was: %s", e.what());
	}

	_saving_known_destinations = false;

	return success;
}

/*static*/ void Identity::load_known_destinations() {
	if (!_known_destinations_pool) {
		ERROR("Cannot load known destinations - pool not initialized");
		return;
	}

	// Binary format - much more memory efficient than JSON
	const char* storage_path = "/known_dst.bin";

	if (!OS::file_exists(storage_path)) {
		DEBUG("No known destinations file found, starting fresh");
		return;
	}

	try {
		// Open file for reading using OS filesystem abstraction
		FileStream file = OS::open_file(storage_path, FileStream::MODE_READ);
		if (!file) {
			WARNING("Failed to open known destinations file");
			return;
		}

		// Read and verify header
		uint8_t magic[4];
		uint8_t version;
		uint16_t count;

		if (file.readBytes((char*)magic, 4) != 4 ||
		    magic[0] != 'K' || magic[1] != 'D' || magic[2] != 'S' || magic[3] != 'T') {
			WARNING("Invalid known destinations file magic");
			file.close();
			return;
		}

		if (file.readBytes((char*)&version, 1) != 1 || version != 1) {
			WARNING("Unknown known destinations file version");
			file.close();
			return;
		}

		if (file.readBytes((char*)&count, sizeof(uint16_t)) != sizeof(uint16_t)) {
			WARNING("Failed to read known destinations count");
			file.close();
			return;
		}

		DEBUG("Loading " + std::to_string(count) + " known destinations from storage (binary)...");

		size_t loaded_count = 0;

		// Temporary buffers on stack - no heap allocation
		uint8_t dest_hash_buf[DEST_HASH_SIZE];
		double timestamp;
		uint8_t packet_hash_buf[PACKET_HASH_SIZE];
		uint8_t public_key_buf[PUBLIC_KEY_SIZE];
		uint16_t app_data_len;

		for (uint16_t i = 0; i < count; ++i) {
			// Read fixed fields directly into stack buffers
			if (file.readBytes((char*)dest_hash_buf, DEST_HASH_SIZE) != DEST_HASH_SIZE) break;
			if (file.readBytes((char*)&timestamp, sizeof(double)) != sizeof(double)) break;
			if (file.readBytes((char*)packet_hash_buf, PACKET_HASH_SIZE) != PACKET_HASH_SIZE) break;
			if (file.readBytes((char*)public_key_buf, PUBLIC_KEY_SIZE) != PUBLIC_KEY_SIZE) break;
			if (file.readBytes((char*)&app_data_len, sizeof(uint16_t)) != sizeof(uint16_t)) break;

			// Read app_data if present
			Bytes app_data;
			if (app_data_len > 0) {
				if (app_data_len > 1024) {
					WARNING("Skipping entry with excessive app_data length");
					// Skip over app_data by reading and discarding
					for (uint16_t j = 0; j < app_data_len; ++j) file.read();
					continue;
				}
				// Use stack buffer for small app_data, then assign to Bytes
				uint8_t app_data_buf[1024];
				if (file.readBytes((char*)app_data_buf, app_data_len) != app_data_len) break;
				app_data.assign(app_data_buf, app_data_len);
			}

			// Create Bytes wrappers for the fixed buffers
			Bytes dest_hash(dest_hash_buf, DEST_HASH_SIZE);
			Bytes packet_hash(packet_hash_buf, PACKET_HASH_SIZE);
			Bytes public_key(public_key_buf, PUBLIC_KEY_SIZE);

			// Add to known destinations (don't overwrite existing)
			if (find_known_destination_slot(dest_hash) == nullptr) {
				KnownDestinationSlot* slot = find_empty_known_destination_slot();
				if (slot == nullptr) {
					WARNING("Known destinations pool is full while loading, skipping remaining entries");
					break;
				}
				slot->in_use = true;
				slot->set_hash(dest_hash);
				slot->entry = IdentityEntry(timestamp, packet_hash, public_key, app_data);
				loaded_count++;
			}
		}

		file.close();

		DEBUG("Loaded " + std::to_string(loaded_count) + " known destinations from storage");

	} catch (std::exception& e) {
		ERRORF("Error loading known destinations from disk: %s", e.what());
	}
}

/*static*/ void Identity::cull_known_destinations() {
	if (!_known_destinations_pool) return;
	size_t current_count = known_destinations_count();
	// Cull when pool is 90% full to create buffer room for new entries
	size_t cull_threshold = (KNOWN_DESTINATIONS_SIZE * 9) / 10;  // 90%
	size_t target_count = (KNOWN_DESTINATIONS_SIZE * 8) / 10;    // 80%

	if (current_count < cull_threshold) {
		return;  // Not at threshold yet
	}

	DEBUG("Culling known destinations: " + std::to_string(current_count) + " -> " + std::to_string(target_count));

	// Remove oldest entries one at a time (no temporary allocations)
	uint16_t removed = 0;
	while (known_destinations_count() > target_count) {
		// Find oldest entry
		KnownDestinationSlot* oldest = nullptr;
		for (size_t i = 0; i < KNOWN_DESTINATIONS_SIZE; ++i) {
			if (_known_destinations_pool[i].in_use) {
				if (oldest == nullptr || _known_destinations_pool[i].entry._timestamp < oldest->entry._timestamp) {
					oldest = &_known_destinations_pool[i];
				}
			}
		}
		if (oldest == nullptr) break;
		oldest->clear();
		++removed;
	}
	DEBUG("Removed " + std::to_string(removed) + " destination(s), now " + std::to_string(known_destinations_count()));
}

/*static*/ bool Identity::validate_announce(const Packet& packet) {
	try {
		if (packet.packet_type() == Type::Packet::ANNOUNCE) {
			Bytes destination_hash = packet.destination_hash();
			//TRACE("Identity::validate_announce: destination_hash: " + packet.destination_hash().toHex());
			Bytes public_key = packet.data().left(KEYSIZE/8);
			//TRACE("Identity::validate_announce: public_key:       " + public_key.toHex());
			Bytes name_hash = packet.data().mid(KEYSIZE/8, NAME_HASH_LENGTH/8);
			//TRACE("Identity::validate_announce: name_hash:        " + name_hash.toHex());
			Bytes random_hash = packet.data().mid(KEYSIZE/8 + NAME_HASH_LENGTH/8, RANDOM_HASH_LENGTH/8);
			//TRACE("Identity::validate_announce: random_hash:      " + random_hash.toHex());
			Bytes signature = packet.data().mid(KEYSIZE/8 + NAME_HASH_LENGTH/8 + RANDOM_HASH_LENGTH/8, SIGLENGTH/8);
			//TRACE("Identity::validate_announce: signature:        " + signature.toHex());

			// Extract ratchet public key if present (after signature, before app_data)
			// Ratchet is 32 bytes (Cryptography::Ratchet::RATCHET_LENGTH)
			Bytes ratchet_public_key;
			Bytes app_data;
			size_t base_announce_size = KEYSIZE/8 + NAME_HASH_LENGTH/8 + RANDOM_HASH_LENGTH/8 + SIGLENGTH/8;
			size_t ratchet_size = Cryptography::Ratchet::RATCHET_LENGTH;

			if (packet.data().size() >= base_announce_size + ratchet_size) {
				// There's enough data for a ratchet - extract it
				Bytes potential_ratchet = packet.data().mid(base_announce_size, ratchet_size);

				// Check if this looks like a ratchet (not all zeros)
				// This is a heuristic - ratchets are random X25519 public keys
				bool has_ratchet = false;
				for (size_t i = 0; i < potential_ratchet.size(); ++i) {
					if (potential_ratchet.data()[i] != 0) {
						has_ratchet = true;
						break;
					}
				}

				if (has_ratchet) {
					ratchet_public_key = potential_ratchet;
					DEBUG("Extracted ratchet from announce for " + packet.destination_hash().toHex());
					DEBUG("  Ratchet public key: " + ratchet_public_key.toHex());

					// App data comes after ratchet
					if (packet.data().size() > base_announce_size + ratchet_size) {
						app_data = packet.data().mid(base_announce_size + ratchet_size);
					}
				}
				else {
					// No ratchet - treat everything after signature as app_data
					app_data = packet.data().mid(base_announce_size);
				}
			}
			else if (packet.data().size() > base_announce_size) {
				// Not enough for ratchet - treat as app_data
				app_data = packet.data().mid(base_announce_size);
			}

			//TRACE("Identity::validate_announce: app_data:         " + app_data.toHex());
			//TRACE("Identity::validate_announce: app_data text:    " + app_data.toString());

			Bytes signed_data;
			signed_data << packet.destination_hash() << public_key << name_hash << random_hash+app_data;
			//TRACE("Identity::validate_announce: signed_data:      " + signed_data.toHex());

			Identity announced_identity(false);
			announced_identity.load_public_key(public_key);

			if (announced_identity.pub() && announced_identity.validate(signature, signed_data)) {
				Bytes hash_material = name_hash << announced_identity.hash();
				Bytes expected_hash = full_hash(hash_material).left(Type::Reticulum::TRUNCATED_HASHLENGTH/8);
				//TRACE("Identity::validate_announce: destination_hash: " + packet.destination_hash().toHex());
				//TRACE("Identity::validate_announce: expected_hash:    " + expected_hash.toHex());

				if (packet.destination_hash() == expected_hash) {
					// Check if we already have a public key for this destination
					// and make sure the public key is not different.
					KnownDestinationSlot* slot = find_known_destination_slot(packet.destination_hash());
					if (slot != nullptr) {
						IdentityEntry& identity_entry = slot->entry;
						if (public_key != identity_entry.public_key_bytes()) {
							// In reality, this should never occur, but in the odd case
							// that someone manages a hash collision, we reject the announce.
							CRITICAL("Received announce with valid signature and destination hash, but announced public key does not match already known public key.");
							CRITICAL("This may indicate an attempt to modify network paths, or a random hash collision. The announce was rejected.");
							return false;
						}
					}

					remember(packet.get_hash(), packet.destination_hash(), public_key, app_data);

					// Remember ratchet if one was included in the announce
					if (ratchet_public_key) {
						remember_ratchet(packet.destination_hash(), ratchet_public_key);
					}

					//p del announced_identity

					std::string signal_str;
// TODO
/*
					if packet.rssi != None or packet.snr != None:
						signal_str = " ["
						if packet.rssi != None:
							signal_str += "RSSI "+str(packet.rssi)+"dBm"
							if packet.snr != None:
								signal_str += ", "
						if packet.snr != None:
							signal_str += "SNR "+str(packet.snr)+"dB"
						signal_str += "]"
					else:
						signal_str = ""
*/

					if (packet.transport_id()) {
						TRACE("Valid announce for " + packet.destination_hash().toHex() + " " + std::to_string(packet.hops()) + " hops away, received via " + packet.transport_id().toHex() + " on " + packet.receiving_interface().toString() + signal_str);
					}
					else {
						TRACE("Valid announce for " + packet.destination_hash().toHex() + " " + std::to_string(packet.hops()) + " hops away, received on " + packet.receiving_interface().toString() + signal_str);
					}

					return true;
				}
				else {
					DEBUG("Received invalid announce for " + packet.destination_hash().toHex() + ": Destination mismatch.");
					return false;
				}
			}
			else {
				DEBUG("Received invalid announce for " + packet.destination_hash().toHex() + ": Invalid signature.");
				//p del announced_identity
				return false;
			}
		}
	}
	catch (std::exception& e) {
		ERROR("Error occurred while validating announce. The contained exception was: " + std::string(e.what()));
		return false;
	}
	return false;
}

/*static*/ void Identity::persist_data() {
	if (!Transport::reticulum() || !Transport::reticulum().is_connected_to_shared_instance()) {
		save_known_destinations();
	}
}

/*static*/ void Identity::exit_handler() {
	persist_data();
}

/*
Encrypts information for the identity.

:param plaintext: The plaintext to be encrypted as *bytes*.
:returns: Ciphertext token as *bytes*.
:raises: *KeyError* if the instance does not hold a public key.
*/
const Bytes Identity::encrypt(const Bytes& plaintext) const {
	assert(_object);
	TRACE("Identity::encrypt: encrypting data...");
	if (!_object->_pub) {
		throw std::runtime_error("Encryption failed because identity does not hold a public key");
	}
	Cryptography::X25519PrivateKey::Ptr ephemeral_key = Cryptography::X25519PrivateKey::generate();
	Bytes ephemeral_pub_bytes = ephemeral_key->public_key()->public_bytes();
	TRACE("Identity::encrypt: ephemeral public key: " + ephemeral_pub_bytes.toHex());

	// CRYPTO: create shared key for key exchange using own public key
	//shared_key = ephemeral_key.exchange(self.pub)
	Bytes shared_key = ephemeral_key->exchange(_object->_pub_bytes);
	TRACE("Identity::encrypt: shared key:           " + shared_key.toHex());

	Bytes derived_key = Cryptography::hkdf(
		DERIVED_KEY_LENGTH,
		shared_key,
		get_salt(),
		get_context()
	);
	TRACE("Identity::encrypt: derived key:          " + derived_key.toHex());

	Cryptography::Token token(derived_key);
	TRACE("Identity::encrypt: Token encrypting data of length " + std::to_string(plaintext.size()));
	TRACE("Identity::encrypt: plaintext:  " + plaintext.toHex());
	Bytes ciphertext = token.encrypt(plaintext);
	TRACE("Identity::encrypt: ciphertext: " + ciphertext.toHex());

	return ephemeral_pub_bytes + ciphertext;
}


/*
Decrypts information for the identity.

:param ciphertext: The ciphertext to be decrypted as *bytes*.
:returns: Plaintext as *bytes*, or *None* if decryption fails.
:raises: *KeyError* if the instance does not hold a private key.
*/
const Bytes Identity::decrypt(const Bytes& ciphertext_token) const {
	assert(_object);
	TRACE("Identity::decrypt: decrypting data...");
	if (!_object->_prv) {
		throw std::runtime_error("Decryption failed because identity does not hold a private key");
	}
	if (ciphertext_token.size() <= Type::Identity::KEYSIZE/8/2) {
		DEBUG("Decryption failed because the token size " + std::to_string(ciphertext_token.size()) + " was invalid.");
		return {Bytes::NONE};
	}
	Bytes plaintext;
	try {
		//peer_pub_bytes = ciphertext_token[:Identity.KEYSIZE//8//2]
		Bytes peer_pub_bytes = ciphertext_token.left(Type::Identity::KEYSIZE/8/2);
		//peer_pub = X25519PublicKey.from_public_bytes(peer_pub_bytes)
		//Cryptography::X25519PublicKey::Ptr peer_pub = Cryptography::X25519PublicKey::from_public_bytes(peer_pub_bytes);
		TRACE("Identity::decrypt: peer public key:      " + peer_pub_bytes.toHex());

		// CRYPTO: create shared key for key exchange using peer public key
		//shared_key = _object->_prv->exchange(peer_pub);
		Bytes shared_key = _object->_prv->exchange(peer_pub_bytes);
		TRACE("Identity::decrypt: shared key:           " + shared_key.toHex());

		Bytes derived_key = Cryptography::hkdf(
			DERIVED_KEY_LENGTH,
			shared_key,
			get_salt(),
			get_context()
		);
		TRACE("Identity::decrypt: derived key:          " + derived_key.toHex());

		Cryptography::Token token(derived_key);
		//ciphertext = ciphertext_token[Identity.KEYSIZE//8//2:]
		Bytes ciphertext(ciphertext_token.mid(Type::Identity::KEYSIZE/8/2));
		TRACE("Identity::decrypt: Token decrypting data of length " + std::to_string(ciphertext.size()));
		TRACE("Identity::decrypt: ciphertext: " + ciphertext.toHex());
		plaintext = token.decrypt(ciphertext);
		TRACE("Identity::decrypt: plaintext:  " + plaintext.toHex());
		//TRACE("Identity::decrypt: Token decrypted data of length " + std::to_string(plaintext.size()));
	}
	catch (std::exception& e) {
		DEBUG("Decryption by " + toString() + " failed: " + e.what());
	}
		
	return plaintext;
}

/*
Signs information by the identity.

:param message: The message to be signed as *bytes*.
:returns: Signature as *bytes*.
:raises: *KeyError* if the instance does not hold a private key.
*/
const Bytes Identity::sign(const Bytes& message) const {
	assert(_object);
	if (!_object->_sig_prv) {
		throw std::runtime_error("Signing failed because identity does not hold a private key");
	}
	try {
		return _object->_sig_prv->sign(message);
	}
	catch (std::exception& e) {
		ERROR("The identity " + toString() + " could not sign the requested message. The contained exception was: " + e.what());
		throw e;
	}
}

/*
Validates the signature of a signed message.

:param signature: The signature to be validated as *bytes*.
:param message: The message to be validated as *bytes*.
:returns: True if the signature is valid, otherwise False.
:raises: *KeyError* if the instance does not hold a public key.
*/
bool Identity::validate(const Bytes& signature, const Bytes& message) const {
	assert(_object);
	if (_object->_pub) {
		try {
			TRACE("Identity::validate: Attempting to verify signature: " + signature.toHex() + " and message: " + message.toHex());
			_object->_sig_pub->verify(signature, message);
			return true;
		}
		catch (std::exception& e) {
			return false;
		}
	}
	else {
		throw std::runtime_error("Signature validation failed because identity does not hold a public key");
	}
}

void Identity::prove(const Packet& packet, const Destination& destination /*= {Type::NONE}*/) const {
	assert(_object);
	Bytes signature(sign(packet.packet_hash()));
	Bytes proof_data;
	if (RNS::Reticulum::should_use_implicit_proof()) {
		proof_data = signature;
		TRACE("Identity::prove: implicit proof data: " + proof_data.toHex());
	}
	else {
		proof_data = packet.packet_hash() + signature;
		TRACE("Identity::prove: explicit proof data: " + proof_data.toHex());
	}
	
	if (!destination) {
		TRACE("Identity::prove: proving packet with proof destination...");
		ProofDestination proof_destination = packet.generate_proof_destination();
		Packet proof(proof_destination, packet.receiving_interface(), proof_data, Type::Packet::PROOF);
		proof.send();
	}
	else {
		TRACE("Identity::prove: proving packet with specified destination...");
		Packet proof(destination, packet.receiving_interface(), proof_data, Type::Packet::PROOF);
		proof.send();
	}
}

void Identity::prove(const Packet& packet) const {
	prove(packet, {Type::NONE});
}

// Ratchet management for forward secrecy

/*static*/ void Identity::remember_ratchet(const Bytes& destination_hash, const Bytes& ratchet_public_key) {
	if (ratchet_public_key.size() != Cryptography::Ratchet::RATCHET_LENGTH) {
		WARNING("Cannot remember ratchet for " + destination_hash.toHex() +
		        ": invalid ratchet key size " + std::to_string(ratchet_public_key.size()));
		return;
	}

	// Proactively cull before pool gets too full
	cull_known_ratchets();

	DEBUG("Remembering ratchet for destination " + destination_hash.toHex());
	DEBUG("  Ratchet public key: " + ratchet_public_key.toHex());

	// Check if slot already exists
	KnownRatchetSlot* slot = find_known_ratchet_slot(destination_hash);
	if (slot != nullptr) {
		// Update existing
		slot->set_ratchet(ratchet_public_key);
		slot->timestamp = OS::time();
		return;
	}

	// Find empty slot
	slot = find_empty_known_ratchet_slot();
	if (slot == nullptr) {
		// Pool still full after cull - shouldn't happen
		WARNING("Known ratchets pool is full, cannot remember ratchet");
		return;
	}

	slot->in_use = true;
	slot->set_hash(destination_hash);
	slot->set_ratchet(ratchet_public_key);
	slot->timestamp = OS::time();
}

/*
Recall ratchet public key for a destination hash.

:param destination_hash: Destination hash as *bytes*.
:returns: Ratchet public key as *bytes* (32 bytes), or empty Bytes if unknown.
*/
/*static*/ Bytes Identity::recall_ratchet(const Bytes& destination_hash) {
	KnownRatchetSlot* slot = find_known_ratchet_slot(destination_hash);
	if (slot != nullptr) {
		DEBUG("Recalled ratchet for destination " + destination_hash.toHex());
		DEBUG("  Ratchet public key: " + slot->ratchet_bytes().toHex());
		return slot->ratchet_bytes();
	}
	else {
		DEBUG("No ratchet found for destination " + destination_hash.toHex());
		return {Bytes::NONE};
	}
}

/*static*/ bool Identity::save_known_ratchets() {
	// TODO: Implement persistence to JSON file
	// Similar to save_known_destinations()
	// File format: ~/.reticulum/storage/known_ratchets
	// JSON structure: { "dest_hash_hex": { "ratchet": "ratchet_hex", "received": timestamp } }
	DEBUG("Saving known ratchets (persistence not yet implemented)");
	return true;
}

/*static*/ void Identity::load_known_ratchets() {
	// TODO: Implement loading from JSON file
	// Similar to load_known_destinations()
	DEBUG("Loading known ratchets (persistence not yet implemented)");
}

/*static*/ void Identity::cull_known_ratchets() {
	// Cull when pool is 90% full to create buffer room for new entries
	size_t current_count = known_ratchets_count();
	size_t cull_threshold = (KNOWN_RATCHETS_SIZE * 9) / 10;  // 90%
	size_t target_count = (KNOWN_RATCHETS_SIZE * 8) / 10;    // 80%

	if (current_count < cull_threshold) {
		return;  // Not at threshold yet
	}

	DEBUG("Culling known ratchets: " + std::to_string(current_count) + " -> " + std::to_string(target_count));

	// Remove oldest entries one at a time (no temporary allocations)
	uint16_t removed = 0;
	while (known_ratchets_count() > target_count) {
		// Find oldest entry
		KnownRatchetSlot* oldest = nullptr;
		for (size_t i = 0; i < KNOWN_RATCHETS_SIZE; ++i) {
			if (_known_ratchets_pool[i].in_use) {
				if (oldest == nullptr || _known_ratchets_pool[i].timestamp < oldest->timestamp) {
					oldest = &_known_ratchets_pool[i];
				}
			}
		}
		if (oldest == nullptr) break;
		oldest->clear();
		++removed;
	}
	DEBUG("Removed " + std::to_string(removed) + " ratchet entries, now " + std::to_string(known_ratchets_count()));
}
