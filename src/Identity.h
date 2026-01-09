#pragma once

#include "Log.h"
#include "Bytes.h"
#include "Type.h"
#include "Cryptography/Hashes.h"
#include "Cryptography/Ed25519.h"
#include "Cryptography/X25519.h"
#include "Cryptography/Token.h"

#include <map>
#include <string>
#include <memory>
#include <cassert>

namespace RNS {

	class Destination;
	class Packet;

	class Identity {

	private:
		// Fixed-size arrays to avoid Bytes object metadata overhead (~24 bytes each)
		// This eliminates heap fragmentation from known destinations cache
		static constexpr size_t DEST_HASH_SIZE = 16;    // RNS truncated hash
		static constexpr size_t PACKET_HASH_SIZE = 32;  // SHA256 full hash
		static constexpr size_t PUBLIC_KEY_SIZE = 64;   // Ed25519 + X25519 public keys

		class IdentityEntry {
		public:
			IdentityEntry() : _timestamp(0) {
				memset(_packet_hash, 0, PACKET_HASH_SIZE);
				memset(_public_key, 0, PUBLIC_KEY_SIZE);
			}
			IdentityEntry(double timestamp, const Bytes& packet_hash, const Bytes& public_key, const Bytes& app_data)
				: _timestamp(timestamp), _app_data(app_data)
			{
				set_packet_hash(packet_hash);
				set_public_key(public_key);
			}

			// Helper methods for fixed array access
			Bytes packet_hash_bytes() const { return Bytes(_packet_hash, PACKET_HASH_SIZE); }
			Bytes public_key_bytes() const { return Bytes(_public_key, PUBLIC_KEY_SIZE); }
			void set_packet_hash(const Bytes& b) {
				size_t len = std::min(b.size(), PACKET_HASH_SIZE);
				memcpy(_packet_hash, b.data(), len);
				if (len < PACKET_HASH_SIZE) memset(_packet_hash + len, 0, PACKET_HASH_SIZE - len);
			}
			void set_public_key(const Bytes& b) {
				size_t len = std::min(b.size(), PUBLIC_KEY_SIZE);
				memcpy(_public_key, b.data(), len);
				if (len < PUBLIC_KEY_SIZE) memset(_public_key + len, 0, PUBLIC_KEY_SIZE - len);
			}

		public:
			double _timestamp = 0;
			uint8_t _packet_hash[PACKET_HASH_SIZE];   // Fixed array - no heap alloc
			uint8_t _public_key[PUBLIC_KEY_SIZE];    // Fixed array - no heap alloc
			Bytes _app_data;  // Keep as Bytes - variable size, typically small or empty
		};

		// Fixed pool for known destinations (replaces std::map<Bytes, IdentityEntry>)
		// Memory: 192 slots × ~121 bytes = ~23KB (reduced from ~38KB with Bytes objects)
		// Eliminated: 3 Bytes objects per slot × 24 bytes metadata = 72 bytes saved per slot
		static constexpr size_t KNOWN_DESTINATIONS_SIZE = 192;
		struct KnownDestinationSlot {
			bool in_use = false;
			uint8_t destination_hash[DEST_HASH_SIZE];  // Fixed array - no heap alloc
			IdentityEntry entry;

			// Helper methods
			Bytes hash_bytes() const { return Bytes(destination_hash, DEST_HASH_SIZE); }
			void set_hash(const Bytes& b) {
				size_t len = std::min(b.size(), DEST_HASH_SIZE);
				memcpy(destination_hash, b.data(), len);
				if (len < DEST_HASH_SIZE) memset(destination_hash + len, 0, DEST_HASH_SIZE - len);
			}
			bool hash_equals(const Bytes& b) const {
				if (b.size() != DEST_HASH_SIZE) return false;
				return memcmp(destination_hash, b.data(), DEST_HASH_SIZE) == 0;
			}
			void clear() {
				in_use = false;
				memset(destination_hash, 0, DEST_HASH_SIZE);
				entry = IdentityEntry();
			}
		};
		static KnownDestinationSlot _known_destinations_pool[KNOWN_DESTINATIONS_SIZE];

		static KnownDestinationSlot* find_known_destination_slot(const Bytes& hash);
		static KnownDestinationSlot* find_empty_known_destination_slot();

	public:
		static size_t known_destinations_count();
		//static std::map<Bytes, IdentityEntry> _known_destinations;
		static bool _saving_known_destinations;
		// CBA
		static uint16_t _known_destinations_maxsize;

		// Ratchet cache for forward secrecy - fixed pool
		// Memory: 128 slots × ~57 bytes = ~7.3KB (reduced from ~13KB with Bytes objects)
		static constexpr size_t KNOWN_RATCHETS_SIZE = 128;
		static constexpr size_t RATCHET_KEY_SIZE = 32;  // X25519 public key
		struct KnownRatchetSlot {
			bool in_use = false;
			uint8_t destination_hash[DEST_HASH_SIZE];  // Fixed array - no heap alloc
			uint8_t ratchet_public_key[RATCHET_KEY_SIZE];  // Fixed array - no heap alloc
			double timestamp = 0;  // For LRU-style culling

			// Helper methods
			Bytes hash_bytes() const { return Bytes(destination_hash, DEST_HASH_SIZE); }
			Bytes ratchet_bytes() const { return Bytes(ratchet_public_key, RATCHET_KEY_SIZE); }
			void set_hash(const Bytes& b) {
				size_t len = std::min(b.size(), DEST_HASH_SIZE);
				memcpy(destination_hash, b.data(), len);
				if (len < DEST_HASH_SIZE) memset(destination_hash + len, 0, DEST_HASH_SIZE - len);
			}
			void set_ratchet(const Bytes& b) {
				size_t len = std::min(b.size(), RATCHET_KEY_SIZE);
				memcpy(ratchet_public_key, b.data(), len);
				if (len < RATCHET_KEY_SIZE) memset(ratchet_public_key + len, 0, RATCHET_KEY_SIZE - len);
			}
			bool hash_equals(const Bytes& b) const {
				if (b.size() != DEST_HASH_SIZE) return false;
				return memcmp(destination_hash, b.data(), DEST_HASH_SIZE) == 0;
			}
			void clear() {
				in_use = false;
				memset(destination_hash, 0, DEST_HASH_SIZE);
				memset(ratchet_public_key, 0, RATCHET_KEY_SIZE);
				timestamp = 0;
			}
		};
		static KnownRatchetSlot _known_ratchets_pool[KNOWN_RATCHETS_SIZE];
		static bool _saving_known_ratchets;

		static KnownRatchetSlot* find_known_ratchet_slot(const Bytes& dest_hash);
		static KnownRatchetSlot* find_empty_known_ratchet_slot();

	public:
		Identity(bool create_keys = true);
		Identity(Type::NoneConstructor none) {
			MEM("Identity NONE object created, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
		}
		Identity(const Identity& identity) : _object(identity._object) {
			MEM("Identity object copy created, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
		}
		virtual ~Identity() {
			MEM("Identity object destroyed, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
		}

		inline Identity& operator = (const Identity& identity) {
			_object = identity._object;
			MEM("Identity object copy created by assignment, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
			return *this;
		}
		inline operator bool() const {
			return _object.get() != nullptr;
		}
		inline bool operator < (const Identity& identity) const {
			return _object.get() < identity._object.get();
		}

	public:
		void createKeys();

		/*
		:returns: The private key as *bytes*
		*/
		inline const Bytes get_private_key() const {
			assert(_object);
			return _object->_prv_bytes + _object->_sig_prv_bytes;
		}
		/*
		:returns: The public key as *bytes*
		*/
		inline const Bytes get_public_key() const {
			assert(_object);
			return _object->_pub_bytes + _object->_sig_pub_bytes;
		}
		bool load_private_key(const Bytes& prv_bytes);
		void load_public_key(const Bytes& pub_bytes);
		inline void update_hashes() {
			assert(_object);
			_object->_hash = truncated_hash(get_public_key());
			TRACE("Identity::update_hashes: hash: " + _object->_hash.toHex());
			_object->_hexhash = _object->_hash.toHex();
		};
		bool load(const char* path);
		bool to_file(const char* path);

		inline const Bytes& get_salt() const { assert(_object); return _object->_hash; }
		inline const Bytes get_context() const { return {Bytes::NONE}; }

		const Bytes encrypt(const Bytes& plaintext) const;
		const Bytes decrypt(const Bytes& ciphertext_token) const;
		const Bytes sign(const Bytes& message) const;
		bool validate(const Bytes& signature, const Bytes& message) const;
		// CBA following default for reference value requires inclusiion of header
		//void prove(const Packet& packet, const Destination& destination = {Type::NONE}) const;
		void prove(const Packet& packet, const Destination& destination) const;
		void prove(const Packet& packet) const;

		static const Identity from_file(const char* path);
		static void remember(const Bytes& packet_hash, const Bytes& destination_hash, const Bytes& public_key, const Bytes& app_data = {Bytes::NONE});
		static Identity recall(const Bytes& destination_hash);
		static Bytes recall_app_data(const Bytes& destination_hash);
		static bool save_known_destinations();
		static void load_known_destinations();
		// CBA
		static void cull_known_destinations();

		// Ratchet management
		static void remember_ratchet(const Bytes& destination_hash, const Bytes& ratchet_public_key);
		static Bytes recall_ratchet(const Bytes& destination_hash);
		static bool save_known_ratchets();
		static void load_known_ratchets();
		static size_t known_ratchets_count();
		static void cull_known_ratchets();

		/*
		Get a SHA-256 hash of passed data.

		:param data: Data to be hashed as *bytes*.
		:returns: SHA-256 hash as *bytes*
		*/
		static inline const Bytes full_hash(const Bytes& data) {
			return Cryptography::sha256(data);
		}

		/*
		Get a truncated SHA-256 hash of passed data.

		:param data: Data to be hashed as *bytes*.
		:returns: Truncated SHA-256 hash as *bytes*
		*/
		static inline const Bytes truncated_hash(const Bytes& data) {
			//p return Identity.full_hash(data)[:(Identity.TRUNCATED_HASHLENGTH//8)]
			return full_hash(data).left(Type::Identity::TRUNCATED_HASHLENGTH/8);
		}

		/*
		Get a random SHA-256 hash.

		:param data: Data to be hashed as *bytes*.
		:returns: Truncated SHA-256 hash of random data as *bytes*
		*/
		static inline const Bytes get_random_hash() {
			return truncated_hash(Cryptography::random(Type::Identity::TRUNCATED_HASHLENGTH/8));
		}

		static bool validate_announce(const Packet& packet);
		static void persist_data();
		static void exit_handler();

		// getters/setters
		inline const Bytes& encryptionPrivateKey() const { assert(_object); return _object->_prv_bytes; }
		inline const Bytes& signingPrivateKey() const { assert(_object); return _object->_sig_prv_bytes; }
		inline const Bytes& encryptionPublicKey() const { assert(_object); return _object->_pub_bytes; }
		inline const Bytes& signingPublicKey() const { assert(_object); return _object->_sig_pub_bytes; }
		inline const Bytes& hash() const { assert(_object); return _object->_hash; }
		inline std::string hexhash() const { assert(_object); return _object->_hexhash; }
		inline const Bytes& app_data() const { assert(_object); return _object->_app_data; }
		inline void app_data(const Bytes& app_data) { assert(_object); _object->_app_data = app_data; }
		inline const Cryptography::X25519PrivateKey::Ptr prv() const { assert(_object); return _object->_prv; }
		inline const Cryptography::Ed25519PrivateKey::Ptr sig_prv() const { assert(_object); return _object->_sig_prv; }
		inline const Cryptography::X25519PublicKey::Ptr pub() const { assert(_object); return _object->_pub; }
		inline const Cryptography::Ed25519PublicKey::Ptr sig_pub() const { assert(_object); return _object->_sig_pub; }

		inline std::string toString() const { if (!_object) return ""; return "{Identity:" + _object->_hash.toHex() + "}"; }

	private:
		class Object {
		public:
			Object() { MEM("Identity::Data object created, this: " + std::to_string((uintptr_t)this)); }
			virtual ~Object() { MEM("Identity::Data object destroyed, this: " + std::to_string((uintptr_t)this)); }
		private:

			Cryptography::X25519PrivateKey::Ptr _prv;
			Bytes _prv_bytes;

			Cryptography::Ed25519PrivateKey::Ptr _sig_prv;
			Bytes _sig_prv_bytes;

			Cryptography::X25519PublicKey::Ptr _pub;
			Bytes _pub_bytes;

			Cryptography::Ed25519PublicKey::Ptr _sig_pub;
			Bytes _sig_pub_bytes;

			Bytes _hash;
			std::string _hexhash;

			Bytes _app_data;

		friend class Identity;
		};
		std::shared_ptr<Object> _object;

	};

}