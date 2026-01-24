#include "Bytes.h"
#include "BytesPool.h"

using namespace RNS;

// Creates new shared data for instance
// - If capacity is specified (>0) then create empty shared data with initial reserved capacity
// - If capacity is not specified (<=0) then create empty shared data with no initial capacity
// MEM-H1: Try BytesPool first for common sizes, fall back to make_shared for oversized
void Bytes::newData(size_t capacity /*= 0*/) {
//MEMF("Bytes is creating own data with capacity %u", capacity);
//MEM("newData: Creating new data...");

	// Try pool for common packet sizes (256, 512, 1024 bytes)
	if (capacity > 0 && capacity <= BytesPoolConfig::TIER_LARGE) {
		auto [pooled, tier] = BytesPool::instance().acquire(capacity);
		if (pooled) {
			// Got pooled Data - wrap in shared_ptr with custom deleter
			// Note: pooled is already empty with capacity reserved
			_data = SharedData(pooled, BytesPoolDeleter{tier});
			_exclusive = true;
//MEM("newData: Using pooled data");
			return;
		}
		// Pool exhausted - log and fall through to make_shared
		BytesPool::instance().recordFallback(capacity);
	}

	// Fallback: standard allocation for oversized or when pool exhausted
	try {
		_data = std::make_shared<Data>();
	} catch (const std::bad_alloc& e) {
		ERROR("Bytes::newData heap allocation failed: " + std::string(e.what()));
		_data = nullptr;
		_exclusive = false;
		return;  // Caller must check if _data is valid
	}
	if (!_data) {
		ERROR("Bytes failed to allocate empty data buffer");
		_data = nullptr;
		_exclusive = false;
		return;  // Graceful failure instead of crash
	}
//MEM("newData: Created new data");
	if (capacity > 0) {
//MEMF("newData: Reserving data capacity of %u...", capacity);
		_data->reserve(capacity);
//MEM("newData: Reserved data capacity");
	}
//MEM("newData: Assigning data to shared data pointer...");
//MEM("newData: Assigned data to shared data pointer");
	_exclusive = true;
}

// Ensures that instance has exclusive shared data
// - If instance has no shared data then create new shared data
// - If instance does not have exclusive on shared data that is not empty then make a copy of shared data (if requests) and reserve capacity (if requested)
// - If instance does not have exclusive on shared data that is empty then create new shared data
// - If instance already has exclusive on shared data then do nothing except reserve capacity (if requested)
// MEM-H1: Try BytesPool for COW copies to prevent fragmentation
void Bytes::exclusiveData(bool copy /*= true*/, size_t capacity /*= 0*/) {
	if (!_data) {
		newData(capacity);
	}
	else if (!_exclusive) {
		// COW copy creates new allocation per-write on shared Bytes
		if (copy && !_data->empty()) {
			//TRACE("Bytes is creating a writable copy of its shared data");
//MEM("exclusiveData: Creating new data...");

			// Calculate required capacity for COW copy
			size_t required_capacity = (capacity > _data->size()) ? capacity : _data->size();

			// Try pool for common sizes
			if (required_capacity <= BytesPoolConfig::TIER_LARGE) {
				auto [pooled, tier] = BytesPool::instance().acquire(required_capacity);
				if (pooled) {
					// Got pooled Data - copy existing content and wrap with custom deleter
					pooled->insert(pooled->begin(), _data->begin(), _data->end());
					_data = SharedData(pooled, BytesPoolDeleter{tier});
					_exclusive = true;
//MEM("exclusiveData: Using pooled data for COW copy");
					return;
				}
				// Pool exhausted - log and fall through to make_shared
				BytesPool::instance().recordFallback(required_capacity);
			}

			// Fallback: standard allocation
			std::shared_ptr<Data> new_data;
			try {
				new_data = std::make_shared<Data>();
			} catch (const std::bad_alloc& e) {
				ERROR("Bytes::exclusiveData heap allocation failed: " + std::string(e.what()));
				_data = nullptr;
				_exclusive = false;
				return;  // Caller must check if _data is valid
			}
			if (!new_data) {
				ERROR("Bytes failed to duplicate data buffer");
				_data = nullptr;
				_exclusive = false;
				return;  // Graceful failure instead of crash
			}
//MEM("exclusiveData: Created new data");
			new_data->reserve(required_capacity);
//MEM("exclusiveData: Copying existing data...");
			new_data->insert(new_data->begin(), _data->begin(), _data->end());
//MEM("exclusiveData: Copied existing data");
//MEM("exclusiveData: Assigning data to shared data pointer...");
			_data = new_data;
//MEM("exclusiveData: Assigned data to shared data pointer");
			_exclusive = true;
		}
		else {
//MEM("Bytes is creating its own data because shared is empty");
			//data = new Data();
			//if (data == nullptr) {
			//	ERROR("Bytes failed to allocate empty data buffer");
			//	throw std::runtime_error("Failed to allocate empty data buffer");
			//}
			//_data = SharedData(data);
			//_exclusive = true;
//MEM("exclusiveData: Creating new empty data...");
			newData(capacity);
//MEM("exclusiveData: Created new empty data");
		}
	}
	else if (capacity > 0 && capacity > size()) {
		reserve(capacity);
	}
}

int Bytes::compare(const Bytes& bytes) const {
	if (_data == bytes._data) {
		return 0;
	}
	else if (!_data) {
		return -1;
	}
	else if (!bytes._data) {
		return 1;
	}
	else if (*_data < *(bytes._data)) {
		return -1;
	}
	else if (*_data > *(bytes._data)) {
		return 1;
	}
	else {
		return 0;
	}
}

int Bytes::compare(const uint8_t* buf, size_t size) const {
	if (!_data && size == 0) {
		return 0;
	}
	else if (!_data) {
		return -1;
	}
	int cmp = memcmp(_data->data(), buf, (_data->size() < size) ? _data->size() : size);
	if (cmp == 0 && _data->size() < size) {
		return -1;
	}
	else if (cmp == 0 && _data->size() > size) {
		return 1;
	}
	return cmp;
}

void Bytes::assignHex(const uint8_t* hex, size_t hex_size) {
	// if assignment is empty then clear data and don't bother creating new
	if (hex == nullptr || hex_size <= 0) {
		_data = nullptr;
		_exclusive = true;
		return;
	}
	exclusiveData(false, hex_size / 2);
	// need to clear data since we're appending below
	_data->clear();
	for (size_t i = 0; i < hex_size; i += 2) {
		uint8_t byte = (hex[i] % 32 + 9) % 25 * 16 + (hex[i+1] % 32 + 9) % 25;
		_data->push_back(byte);
	}
}

void Bytes::appendHex(const uint8_t* hex, size_t hex_size) {
	// if append is empty then do nothing
	if (hex == nullptr || hex_size <= 0) {
		return;
	}
	exclusiveData(true, size() + (hex_size / 2));
	for (size_t i = 0; i < hex_size; i += 2) {
		uint8_t byte = (hex[i] % 32 + 9) % 25 * 16 + (hex[i+1] % 32 + 9) % 25;
		_data->push_back(byte);
	}
}

const char hex_upper_chars[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
const char hex_lower_chars[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

std::string RNS::hexFromByte(uint8_t byte, bool upper /*= true*/) {
	std::string hex;
	if (upper) {
		hex += hex_upper_chars[ (byte&  0xF0) >> 4];
		hex += hex_upper_chars[ (byte&  0x0F) >> 0];
	}
	else {
		hex += hex_lower_chars[ (byte&  0xF0) >> 4];
		hex += hex_lower_chars[ (byte&  0x0F) >> 0];
	}
	return hex;
}

std::string Bytes::toHex(bool upper /*= false*/) const {
	if (!_data) {
		return "";
	}
	std::string hex;
	hex.reserve(_data->size() * 2);
	for (uint8_t byte : *_data) {
		if (upper) {
			hex += hex_upper_chars[ (byte&  0xF0) >> 4];
			hex += hex_upper_chars[ (byte&  0x0F) >> 0];
		}
		else {
			hex += hex_lower_chars[ (byte&  0xF0) >> 4];
			hex += hex_lower_chars[ (byte&  0x0F) >> 0];
		}
	}
	return hex;
}

// mid
Bytes Bytes::mid(size_t beginpos, size_t len) const {
	if (!_data || beginpos >= size()) {
		return NONE;
	}
	if ((beginpos + len) >= size()) {
		len = (size() - beginpos);
	 }
	 return {data() + beginpos, len};
}

// to end
Bytes Bytes::mid(size_t beginpos) const {
	if (!_data || beginpos >= size()) {
		return NONE;
	}
	 return {data() + beginpos, size() - beginpos};
}

// MsgPack serialization - packs Bytes as binary data
#include <MsgPack.h>
void Bytes::to_msgpack(MsgPack::Packer& packer) const {
	packer.pack(data(), size());
}
