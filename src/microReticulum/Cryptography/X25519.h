/*
 * Copyright (c) 2023 Chad Attermann
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#pragma once

#include "../Bytes.h"
#include "../Log.h"

#include <Curve25519.h>
#include <cstring>

#include <memory>
#include <stdexcept>
#include <stdint.h>

namespace RNS { namespace Cryptography {

	class X25519PublicKey {

	public:
		using Ptr = std::shared_ptr<X25519PublicKey>;

	public:
/*
		X25519PublicKey(const Bytes& x) {
			_x = x;
		}
*/
		X25519PublicKey(const Bytes& publicKey) {
			_publicKey = publicKey;
		}
		~X25519PublicKey() {}

	public:
		// creates a new instance with specified seed
/*
		static inline Ptr from_public_bytes(const Bytes& data) {
			return Ptr(new X25519PublicKey(_unpack_number(data)));
		}
*/
		static inline Ptr from_public_bytes(const Bytes& publicKey) {
			return Ptr(new X25519PublicKey(publicKey));
		}

/*
		Bytes public_bytes() {
			return _pack_number(_x);
		}
*/
		Bytes public_bytes() {
			return _publicKey;
		}

	private:
		//Bytes _x;
		Bytes _publicKey;

	};

	class X25519PrivateKey {

	public:
		const float MIN_EXEC_TIME = 2;		// in milliseconds
		const float MAX_EXEC_TIME = 500;	// in milliseconds
		const uint8_t DELAY_WINDOW = 10;

		//z T_CLEAR = None
		const uint8_t T_MAX = 0;

		using Ptr = std::shared_ptr<X25519PrivateKey>;

	public:
/*
		X25519PrivateKey(const Bytes& a) {
			_a = a;
		}
*/
		X25519PrivateKey(const Bytes& privateKey) {
			try {
				if (privateKey) {
					// Apply RFC 7748 §5 scalar clamping before use, matching
					// Python's cryptography lib (X25519PrivateKey.from_private_bytes
					// clamps automatically).
					if (privateKey.size() >= 32) {
						Bytes::secure_assign(_privateKey, privateKey.data(), 32);
						uint8_t* clamped = _privateKey.writable(32);
						clamped[0]  &= 0xF8;
						clamped[31] &= 0x7F;
						clamped[31] |= 0x40;
					}
					else {
						Bytes::secure_assign(_privateKey, privateKey.data(), privateKey.size());
					}
					Curve25519::eval(_publicKey.writable(32), _privateKey.data(), 0);
				}
				else {
					Bytes::secure_assign_zeroed(_privateKey, 32);
					Curve25519::dh1(_publicKey.writable(32), _privateKey.writable(32));
				}
			}
			catch (...) {
				_privateKey.secure_clear();
				throw;
			}
		}
		~X25519PrivateKey() { _privateKey.secure_clear(); }

	public:
		// creates a new instance with a random seed
/*
		static inline Ptr generate() {
			return from_private_bytes(os.urandom(32));
		}
*/
		static inline Ptr generate() {
			return from_private_bytes({Bytes::NONE});
		}

		// creates a new instance with specified seed
/*
		static inline Ptr from_private_bytes(const Bytes& data) {
			return Ptr(new X25519PrivateKey(_fix_secret(_unpack_number(data))));
		}
*/
		static inline Ptr from_private_bytes(const Bytes& privateKey) {
			return Ptr(new X25519PrivateKey(privateKey));
		}

/*
		inline const Bytes private_bytes() {
			return _pack_number(_a);
		}
*/
		inline const Bytes& private_bytes() {
			return _privateKey;
		}

#ifdef LIBRARY_TEST
		Bytes test_private_key_view() const { return _privateKey; }
#endif

		// creates a new instance of public key for this private key
/*
		inline X25519PublicKey::Ptr public_key() {
			return X25519PublicKey::from_public_bytes(_pack_number(_raw_curve25519(9, _a)));
		}
*/
		inline X25519PublicKey::Ptr public_key() {
			return X25519PublicKey::from_public_bytes(_publicKey);
		}

/*
		inline const Bytes exchange(const Bytes& peer_public_key) {
			if isinstance(peer_public_key, bytes):
				peer_public_key = X25519PublicKey.from_public_bytes(peer_public_key)

			start = OS::time()
			
			shared = _pack_number(_raw_curve25519(peer_public_key.x, _a))
			
			end = OS::time()
			duration = end-start

			if X25519PrivateKey.T_CLEAR == None:
				X25519PrivateKey.T_CLEAR = end + X25519PrivateKey.DELAY_WINDOW

			if end > X25519PrivateKey.T_CLEAR:
				X25519PrivateKey.T_CLEAR = end + X25519PrivateKey.DELAY_WINDOW
				X25519PrivateKey.T_MAX = 0
			
			if duration < X25519PrivateKey.T_MAX or duration < X25519PrivateKey.MIN_EXEC_TIME:
				target = start+X25519PrivateKey.T_MAX

				if target > start+X25519PrivateKey.MAX_EXEC_TIME:
					target = start+X25519PrivateKey.MAX_EXEC_TIME

				if target < start+X25519PrivateKey.MIN_EXEC_TIME:
					target = start+X25519PrivateKey.MIN_EXEC_TIME

				try:
					OS::sleep(target-OS::time())
				except Exception as e:
					pass

			elif duration > X25519PrivateKey.T_MAX:
				X25519PrivateKey.T_MAX = duration

			return shared
		}
*/
		inline const Bytes exchange(const Bytes& peer_public_key) {
			DEBUGF("X25519PublicKey::exchange: public key:       %s", _publicKey.toHex().c_str());
			DEBUGF("X25519PublicKey::exchange: peer public key:  %s", peer_public_key.toHex().c_str());
			Bytes sharedKey;
			SecureBytesGuard shared_key_guard(sharedKey);
			if (!Curve25519::eval(sharedKey.writable(32), _privateKey.data(), peer_public_key.data())) {
				throw std::runtime_error("Peer key is invalid");
			}
			shared_key_guard.disarm();
			return sharedKey;
		}

		inline bool verify(const Bytes& peer_public_key) {
			DEBUGF("X25519PublicKey::exchange: public key:       %s", _publicKey.toHex().c_str());
			DEBUGF("X25519PublicKey::exchange: peer public key:  %s", peer_public_key.toHex().c_str());
			Bytes sharedKey = Bytes::secure_copy_prefix(peer_public_key, peer_public_key.size());
			SecureBytesGuard shared_key_guard(sharedKey);
			bool success = Curve25519::dh2(sharedKey.writable(32), _privateKey.writable(32));
			return success;
		}

	private:
		//Bytes _a;
		Bytes _privateKey;
		Bytes _publicKey;

	};

} }
