#pragma once

#include "../Bytes.h"
#include <cstddef>

namespace RNS { namespace Cryptography {

	// max_output_size=0 preserves legacy unbounded-output behavior. A non-zero ceiling is
	// enforced while streaming; oversized streams return no partial plaintext.
	const Bytes bz2_decompress(const Bytes& data, size_t max_output_size = 0);
	const Bytes bz2_compress(const Bytes& data);

} }
