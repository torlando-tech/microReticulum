#pragma once

//#include "Reticulum.h"
//#include "Link.h"
//#include "Interface.h"
#include "Identity.h"
#include "Bytes.h"
#include "Type.h"
#include "Cryptography/Ratchet.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <set>
#include <cassert>
#include <stdint.h>

namespace RNS {

	class Interface;
	class Link;
	class Packet;

	class RequestHandler {
	public:
		//p response_generator(path, data, request_id, link_id, remote_identity, requested_at)
		using response_generator = Bytes(*)(const Bytes& path, const Bytes& data, const Bytes& request_id, const Bytes& link_id, const Identity& remote_identity, double requested_at);
	public:
		static constexpr size_t ALLOWED_LIST_SIZE = 16;

		RequestHandler() = default;
		RequestHandler(const RequestHandler& handler) {
			_path = handler._path;
			_response_generator = handler._response_generator;
			_allow = handler._allow;
			_allowed_list_count = handler._allowed_list_count;
			for (size_t i = 0; i < _allowed_list_count; i++) {
				_allowed_list[i] = handler._allowed_list[i];
			}
		}
		RequestHandler& operator=(const RequestHandler& handler) {
			if (this != &handler) {
				_path = handler._path;
				_response_generator = handler._response_generator;
				_allow = handler._allow;
				_allowed_list_count = handler._allowed_list_count;
				for (size_t i = 0; i < _allowed_list_count; i++) {
					_allowed_list[i] = handler._allowed_list[i];
				}
			}
			return *this;
		}

		bool allowed_list_contains(const Bytes& hash) const {
			for (size_t i = 0; i < _allowed_list_count; i++) {
				if (_allowed_list[i] == hash) return true;
			}
			return false;
		}
		bool allowed_list_add(const Bytes& hash) {
			if (allowed_list_contains(hash)) return false;
			if (_allowed_list_count >= ALLOWED_LIST_SIZE) return false;
			_allowed_list[_allowed_list_count++] = hash;
			return true;
		}

		Bytes _path;
		response_generator _response_generator = nullptr;
		Type::Destination::request_policies _allow = Type::Destination::ALLOW_NONE;
		Bytes _allowed_list[ALLOWED_LIST_SIZE];
		size_t _allowed_list_count = 0;
	};

    /**
     * @brief A class used to describe endpoints in a Reticulum Network. Destination
	 * instances are used both to create outgoing and incoming endpoints. The
	 * destination type will decide if encryption, and what type, is used in
	 * communication with the endpoint. A destination can also announce its
	 * presence on the network, which will also distribute necessary keys for
	 * encrypted communication with it.
	 * 
	 * @param identity An instance of :ref:`RNS.Identity<api-identity>`. Can hold only public keys for an outgoing destination, or holding private keys for an ingoing.
	 * @param direction ``RNS.Destination.IN`` or ``RNS.Destination.OUT``.
	 * @param type ``RNS.Destination.SINGLE``, ``RNS.Destination.GROUP`` or ``RNS.Destination.PLAIN``.
	 * @param app_name A string specifying the app name.
	 * @param aspects Any non-zero number of string arguments.
	 */
	class Destination {

	public:
		class Callbacks {
		public:
			using link_established = void(*)(Link& link);
			//using packet = void(*)(uint8_t* data, uint16_t data_len, Packet *packet);
			using packet = void(*)(const Bytes& data, const Packet& packet);
			using proof_requested = bool(*)(const Packet& packet);
		public:
			link_established _link_established = nullptr;
			packet _packet = nullptr;
			proof_requested _proof_requested = nullptr;
		friend class Destination;
		};

		using PathResponse = std::pair<double, Bytes>;

	public:
		// Lightweight default constructor for static array allocation
		Destination() {}
		Destination(Type::NoneConstructor none) {
			MEM("Destination NONE object created, this: " + std::to_string((uintptr_t)this));
		}
		Destination(const Destination& destination) : _object(destination._object) {
			MEM("Destination object copy created, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
		}
		Destination(
			const Identity& identity,
			const Type::Destination::directions direction,
			const Type::Destination::types type,
			const char* app_name,
			const char* aspects
		);
		Destination(
			const Identity& identity,
			const Type::Destination::directions direction,
			const Type::Destination::types type,
			const Bytes& hash
		);
		virtual ~Destination();

		inline Destination& operator = (const Destination& destination) {
			_object = destination._object;
			MEM("Destination object copy created by assignment, this: " + std::to_string((uintptr_t)this) + ", data: " + std::to_string((uintptr_t)_object.get()));
			return *this;
		}
		inline operator bool() const {
			return _object.get() != nullptr;
		}
		inline bool operator < (const Destination& destination) const {
			return _object.get() < destination._object.get();
		}

	public:
		static std::string expand_name(const Identity& identity, const char* app_name, const char* aspects);
		static Bytes hash(const Identity& identity, const char* app_name, const char* aspects);
		static Bytes name_hash(const char* app_name, const char* aspects);
		static std::vector<std::string> app_and_aspects_from_name(const char* full_name);
		static Bytes hash_from_name_and_identity(const char* full_name, const Identity& identity);

	public:
		//Packet announce(const Bytes& app_data = {}, bool path_response = false, const Interface& attached_interface = {Type::NONE}, const Bytes& tag = {}, bool send = true);
		Packet announce(const Bytes& app_data, bool path_response, const Interface& attached_interface, const Bytes& tag = {}, bool send = true);
		Packet announce(const Bytes& app_data = {}, bool path_response = false);

		/*
		Set or query whether the destination accepts incoming link requests.

		:param accepts: If ``True`` or ``False``, this method sets whether the destination accepts incoming link requests. If not provided or ``None``, the method returns whether the destination currently accepts link requests.
		:returns: ``True`` or ``False`` depending on whether the destination accepts incoming link requests, if the *accepts* parameter is not provided or ``None``.
		*/
		inline void accepts_links(bool accepts) { assert(_object); _object->_accept_link_requests = accepts; }
		inline bool accepts_links() { assert(_object); return _object->_accept_link_requests; }

		/*
			Registers a function to be called when a link has been established to
			this destination.

			:param callback: A function or method with the signature *callback(link)* to be called when a new link is established with this destination.
		*/
		inline void set_link_established_callback(Callbacks::link_established callback) {
			assert(_object);
			_object->_callbacks._link_established = callback;
		}
		/*
			Registers a function to be called when a packet has been received by
			this destination.

			:param callback: A function or method with the signature *callback(data, packet)* to be called when this destination receives a packet.
		*/
		inline void set_packet_callback(Callbacks::packet callback)  {
			assert(_object);
			_object->_callbacks._packet = callback;
		}
		/*
			Registers a function to be called when a proof has been requested for
			a packet sent to this destination. Allows control over when and if
			proofs should be returned for received packets.

			:param callback: A function or method to with the signature *callback(packet)* be called when a packet that requests a proof is received. The callback must return one of True or False. If the callback returns True, a proof will be sent. If it returns False, a proof will not be sent.
		*/
		inline void set_proof_requested_callback(Callbacks::proof_requested callback)  {
			assert(_object);
			_object->_callbacks._proof_requested = callback;
		}

		/*
		Sets the destinations proof strategy.

		:param proof_strategy: One of ``RNS.Destination.PROVE_NONE``, ``RNS.Destination.PROVE_ALL`` or ``RNS.Destination.PROVE_APP``. If ``RNS.Destination.PROVE_APP`` is set, the `proof_requested_callback` will be called to determine whether a proof should be sent or not.
		*/
		inline void set_proof_strategy(Type::Destination::proof_strategies proof_strategy) {
			assert(_object);
			//if (proof_strategy <= PROOF_NONE) {
			//	throw throw std::invalid_argument("Unsupported proof strategy");
			//}
			_object->_proof_strategy = proof_strategy;
		}

		void receive(const Packet& packet);
		void incoming_link_request(const Bytes& data, const Packet& packet);

		virtual const Bytes encrypt(const Bytes& data);
		virtual const Bytes decrypt(const Bytes& data);
		virtual const Bytes sign(const Bytes& message);

		// Ratchet support for forward secrecy
		void enable_ratchets(const char* ratchets_path);
		void disable_ratchets();
		void rotate_ratchets(bool force = false);
		Bytes get_latest_ratchet_id() const;
		Bytes get_ratchet_public_bytes() const;

		// Ratchet circular buffer helpers
		bool ratchets_add(const Cryptography::Ratchet& ratchet);  // Add new, overwrite oldest if full
		const Cryptography::Ratchet* ratchets_find(const Bytes& public_key) const;
		inline size_t ratchets_count() const { assert(_object); return _object->_ratchets_count; }
		void ratchets_clear();

		// CBA
		bool has_link(const Link& link);
		void remove_link(const Link& link);

		inline std::string toString() const { if (!_object) return ""; return "{Destination:" + _object->_hash.toHex() + "}"; }

		// getters
		inline Type::Destination::types type() const { assert(_object); return _object->_type; }
		inline Type::Destination::directions direction() const { assert(_object); return _object->_direction; }
		inline Type::Destination::proof_strategies proof_strategy() const { assert(_object); return _object->_proof_strategy; }
		inline const Bytes& hash() const { assert(_object); return _object->_hash; }
		inline uint16_t mtu() const { assert(_object); return _object->_mtu; }
		// CBA LINK
		//inline const Bytes& link_id() const { assert(_object); return _object->_link_id; }
		//inline Type::Link::status status() const { assert(_object); return _object->_status; }
		inline const Callbacks& callbacks() const { assert(_object); return _object->_callbacks; }
		inline const Identity& identity() const { assert(_object); return _object->_identity; }
		// Fixed pool accessors - use count and index for iteration
		inline size_t path_responses_count() const {
			assert(_object);
			size_t count = 0;
			for (size_t i = 0; i < Object::PATH_RESPONSES_SIZE; i++) {
				if (_object->_path_responses[i].in_use) count++;
			}
			return count;
		}
		inline size_t request_handlers_count() const {
			assert(_object);
			size_t count = 0;
			for (size_t i = 0; i < Object::REQUEST_HANDLERS_SIZE; i++) {
				if (_object->_request_handlers[i].in_use) count++;
			}
			return count;
		}
		inline size_t links_count() const { assert(_object); return _object->_links.size(); }
		// Find request handler by path hash, returns nullptr if not found
		inline const RequestHandler* find_request_handler(const Bytes& path_hash) const {
			assert(_object);
			for (size_t i = 0; i < Object::REQUEST_HANDLERS_SIZE; i++) {
				if (_object->_request_handlers[i].in_use && _object->_request_handlers[i].path_hash == path_hash) {
					return &_object->_request_handlers[i].handler;
				}
			}
			return nullptr;
		}

		// setters
		// CBA Don't allow changing destination hash after construction since it's used as key in collections
		//inline void hash(const Bytes& hash) { assert(_object); _object->_hash = hash; _object->_hexhash = _object->_hash.toHex(); }
		inline void type(Type::Destination::types type) { assert(_object); _object->_type = type; }
		inline void mtu(uint16_t mtu) { assert(_object); _object->_mtu = mtu; }
		// CBA LINK
		//inline void link_id(const Bytes& id) { assert(_object); _object->_link_id = id; }
		//inline void last_outbound(double time) { assert(_object); _object->_last_outbound = time; }
		//inline void increment_tx() { assert(_object); ++_object->_tx; }
		//inline void increment_txbytes(uint16_t bytes) { assert(_object); _object->_txbytes += bytes; }

	private:
		class Object {
		public:
			// Fixed pool sizes for zero heap fragmentation
			static constexpr size_t REQUEST_HANDLERS_SIZE = 8;
			static constexpr size_t PATH_RESPONSES_SIZE = 8;

			struct RequestHandlerSlot {
				bool in_use = false;
				Bytes path_hash;
				RequestHandler handler;
				void clear() { in_use = false; path_hash.clear(); handler = RequestHandler(); }
			};

			struct PathResponseSlot {
				bool in_use = false;
				Bytes tag;
				PathResponse response;
				void clear() { in_use = false; tag.clear(); response = PathResponse(); }
			};

			Object(const Identity& identity) : _identity(identity) { MEM("Destination::Data object created, this: " + std::to_string((uintptr_t)this)); }
			virtual ~Object() { MEM("Destination::Data object destroyed, this: " + std::to_string((uintptr_t)this)); }
		private:
			bool _accept_link_requests = true;
			Callbacks _callbacks;
			RequestHandlerSlot _request_handlers[REQUEST_HANDLERS_SIZE];
			Type::Destination::types _type;
			Type::Destination::directions _direction;
			Type::Destination::proof_strategies _proof_strategy = Type::Destination::PROVE_NONE;
			uint16_t _mtu = 0;

			PathResponseSlot _path_responses[PATH_RESPONSES_SIZE];
			std::set<Link> _links;

			Identity _identity;
			std::string _name;

			// Generate the destination address hash
			Bytes _hash;
			Bytes _name_hash;
			std::string _hexhash;

			// CBA TODO when is _default_app_data a "callable"?
			Bytes _default_app_data;
			//z _callback = None
			//z _proofcallback = None

			// CBA LINK
			// CBA _link_id is expected by Packet but only present in Link
			// CBA TODO determine if Link needs to inherit from Destination or vice-versa
			//Bytes _link_id;

			//Type::Link::status _status;

			//double _last_outbound = 0.0;
			//uint16_t _tx = 0;
			//uint32_t _txbytes = 0;

			// Ratchet support for forward secrecy - fixed-size circular buffer
			static constexpr size_t RATCHETS_SIZE = 128;
			Cryptography::Ratchet _ratchets[RATCHETS_SIZE];
			size_t _ratchets_head = 0;   // Oldest entry index
			size_t _ratchets_count = 0;  // Number of valid entries
			Bytes _latest_ratchet_id;
			double _latest_ratchet_time = 0.0;
			double _ratchet_interval = Cryptography::Ratchet::DEFAULT_RATCHET_INTERVAL;
			std::string _ratchets_path;
			bool _ratchets_enabled = false;
			bool _enforce_ratchets = false;

		friend class Destination;
		};
		std::shared_ptr<Object> _object;

	};

}