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
//#include "../Log.h"

#include <stdexcept>

namespace RNS { namespace Cryptography {

	class PKCS7 {

	public:

		static const size_t BLOCKSIZE = 16;

		static inline const Bytes pad(const Bytes& data, size_t bs = BLOCKSIZE) {
			const size_t padlen = bs - (data.size() % bs);
			uint8_t padding[BLOCKSIZE];
			if (padlen > sizeof(padding)) throw std::runtime_error("Cannot pad beyond fixed block size");
			memset(padding, static_cast<uint8_t>(padlen), padlen);
			return Bytes::secure_copy_with_suffix(data, padding, padlen);
		}

		static inline const Bytes unpad(const Bytes& data, size_t bs = BLOCKSIZE) {
			if (bs == 0 || data.empty() || data.size() % bs != 0) {
				throw std::invalid_argument("Invalid PKCS#7 padded length");
			}
			const size_t padlen = static_cast<size_t>(data[data.size() - 1]);
			if (padlen == 0 || padlen > bs || padlen > data.size()) {
				throw std::invalid_argument("Invalid PKCS#7 padding length");
			}
			for (size_t i = data.size() - padlen; i < data.size(); ++i) {
				if (data[i] != padlen) throw std::invalid_argument("Invalid PKCS#7 padding bytes");
			}
			return Bytes::secure_copy_prefix(data, data.size() - padlen);
		}

		// updates passed buffer
		static inline void inplace_pad(Bytes& data, size_t bs = BLOCKSIZE) {
			size_t len = data.size();
			//DEBUGF("PKCS7::pad: len: %lu", len);
			size_t padlen = bs - (len % bs);
			//DEBUGF("PKCS7::pad: pad len: %lu", padlen);
			// PKCS#7 (RFC 5652 §6.3): pad with `padlen` octets each having
			// value `padlen`. The previous implementation filled the buffer
			// with zeros and set only the LAST byte to `padlen`, which makes
			// self-roundtrip (pad -> unpad reads the last byte) work but
			// produces ciphertext that doesn't match canonical Python RNS,
			// Swift, or any spec-conformant PKCS#7 implementation.
			//p v = bytes([padlen])
			//p return data+v*padlen
			uint8_t pad[padlen];
			memset(pad, (uint8_t)padlen, padlen);
			// concatenate data with padding
			data.append(pad, padlen);
			//DEBUGF("PKCS7::pad: data size: %lu", data.size());
		}

		// updates passed buffer
		static inline void inplace_unpad(Bytes& data, size_t bs = BLOCKSIZE) {
			size_t len = data.size();
			//DEBUGF("PKCS7::unpad: len: %lu", len);
			// read last byte which is pad length
			//pad = data[-1]
			size_t padlen = (size_t)data.data()[data.size()-1];
			//DEBUGF("PKCS7::unpad: pad len: %lu", padlen);
			if (padlen > bs) {
				throw std::runtime_error("Cannot unpad, invalid padding length of " + std::to_string(padlen) + " bytes");
			}
			// truncate data to strip padding
			//return data[:len-padlen]
			data.resize(len - padlen);
			//DEBUGF("PKCS7::unpad: data size: %lu", data.size());
		}

	};

} }
