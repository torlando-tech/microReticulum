#pragma once

#include <cstddef>
#include <cstdint>

namespace RNS { namespace Utilities {

inline bool within_size_limit(uint64_t size, size_t limit) {
	return limit == 0 || size <= static_cast<uint64_t>(limit);
}

} }
