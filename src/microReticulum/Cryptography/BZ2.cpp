#include "BZ2.h"

#if defined(NATIVE)
#include <bzlib.h>
#elif defined(ARDUINO)
#include "bzlib.h"
#endif

#include <cstring>
#include <vector>
#include <limits>
#include "../Log.h"
#include "../Utilities/OS.h"

#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

namespace RNS { namespace Cryptography {

const Bytes bz2_decompress(const Bytes& data, size_t max_output_size) {
#if defined(NATIVE) || defined(ARDUINO)
	if (data.empty()) {
		return Bytes();
	}

#ifdef ARDUINO
	// Keep each synchronous decoder call bounded. Resource assembly runs on the
	// Reticulum owner task, so a very large output buffer can monopolise it long
	// enough to trigger the task watchdog before the application sees progress.
	const size_t MIN_OUTPUT_SIZE = 16 * 1024;
	const size_t MAX_OUTPUT_SIZE = 512 * 1024; // 512KB max
#else
	// Native: Use larger buffers for efficiency
	const size_t MIN_OUTPUT_SIZE = 2 * 1024 * 1024;  // 2MB minimum
	const size_t MAX_OUTPUT_SIZE = 16 * 1024 * 1024; // 16MB max
#endif
	const size_t output_limit = max_output_size == 0
		? std::numeric_limits<size_t>::max() : max_output_size;
#ifdef ARDUINO
	const size_t output_size = std::min(MIN_OUTPUT_SIZE, output_limit);
#else
	const size_t output_size = std::min(
		std::min(std::max(data.size() * 100, MIN_OUTPUT_SIZE), MAX_OUTPUT_SIZE),
		output_limit);
#endif

#ifdef ARDUINO
	// Allocate from PSRAM if available
	char* output = (char*)heap_caps_malloc(output_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!output) {
		// Fall back to regular heap
		output = (char*)malloc(output_size);
	}
	if (!output) {
		ERROR("bz2_decompress: Failed to allocate output buffer");
		return Bytes();
	}
#else
	std::vector<char> output_vec(output_size);
	char* output = output_vec.data();
#endif

	bz_stream stream;
	memset(&stream, 0, sizeof(stream));

	int ret = BZ2_bzDecompressInit(&stream, 0,
#ifdef ARDUINO
		1
#else
		0
#endif
	);
	if (ret != BZ_OK) {
#ifdef ARDUINO
		free(output);
#endif
		return Bytes();
	}

	stream.next_in = const_cast<char*>(reinterpret_cast<const char*>(data.data()));
	stream.avail_in = data.size();
	stream.next_out = output;
	stream.avail_out = output_size;

	Bytes result;
	int iteration = 0;
	do {
		const unsigned int input_before = stream.avail_in;
		ret = BZ2_bzDecompress(&stream);
		iteration++;

		if (ret == BZ_OK || ret == BZ_STREAM_END) {
			// Append decompressed data
			size_t decompressed = output_size - stream.avail_out;
			if (decompressed > output_limit - std::min(result.size(), output_limit)) {
				WARNINGF("bz2_decompress: output exceeds %zu byte limit", output_limit);
				BZ2_bzDecompressEnd(&stream);
#ifdef ARDUINO
				free(output);
#endif
				return Bytes();
			}
			result.append(reinterpret_cast<uint8_t*>(output), decompressed);

			DEBUGF("bz2_decompress: iter=%d, ret=%d, decompressed=%zu, total=%zu, avail_in=%u",
			       iteration, ret, decompressed, result.size(), stream.avail_in);
			if (ret == BZ_OK && decompressed == 0 && stream.avail_in == input_before) {
				WARNING("bz2_decompress: malformed stream made no progress");
				BZ2_bzDecompressEnd(&stream);
#ifdef ARDUINO
				free(output);
#endif
				return Bytes();
			}

			if (ret == BZ_STREAM_END) {
				break;
			}

			// Reset output buffer for more data
			stream.next_out = output;
			stream.avail_out = output_size;
			Utilities::OS::run_loop();
		} else {
			DEBUGF("bz2_decompress: iter=%d, FAILED ret=%d", iteration, ret);
			BZ2_bzDecompressEnd(&stream);
#ifdef ARDUINO
			free(output);
#endif
			return Bytes();
		}
	} while (stream.avail_in > 0 || ret != BZ_STREAM_END);

	BZ2_bzDecompressEnd(&stream);
#ifdef ARDUINO
	free(output);
#endif
	DEBUGF("bz2_decompress: final output size=%zu", result.size());
	return result;
#else
	ERROR("bz2_decompress: BZ2 support not available on this platform");
	return Bytes();
#endif
}

const Bytes bz2_compress(const Bytes& data) {
#if defined(NATIVE) || defined(ARDUINO)
	if (data.empty()) {
		return Bytes();
	}

	// Compressed size is at most input size + 1% + 600 bytes
	size_t output_size = data.size() + data.size() / 100 + 600;

#ifdef ARDUINO
	// Allocate from PSRAM if available
	char* output = (char*)heap_caps_malloc(output_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!output) {
		output = (char*)malloc(output_size);
	}
	if (!output) {
		ERROR("bz2_compress: Failed to allocate output buffer");
		return Bytes();
	}
	// Use smaller block size (1 = 100k) on ESP32 to reduce memory usage
	const int block_size = 1;
#else
	std::vector<char> output_vec(output_size);
	char* output = output_vec.data();
	// Use largest block size (9 = 900k) on native for best compression
	const int block_size = 9;
#endif

	bz_stream stream;
	memset(&stream, 0, sizeof(stream));

	int ret = BZ2_bzCompressInit(&stream, block_size, 0, 0);
	if (ret != BZ_OK) {
#ifdef ARDUINO
		free(output);
#endif
		return Bytes();
	}

	stream.next_in = const_cast<char*>(reinterpret_cast<const char*>(data.data()));
	stream.avail_in = data.size();
	stream.next_out = output;
	stream.avail_out = output_size;

	ret = BZ2_bzCompress(&stream, BZ_FINISH);
	if (ret != BZ_STREAM_END) {
		BZ2_bzCompressEnd(&stream);
#ifdef ARDUINO
		free(output);
#endif
		return Bytes();
	}

	size_t compressed_size = output_size - stream.avail_out;
	BZ2_bzCompressEnd(&stream);

	Bytes result(reinterpret_cast<uint8_t*>(output), compressed_size);
#ifdef ARDUINO
	free(output);
#endif
	return result;
#else
	ERROR("bz2_compress: BZ2 support not available on this platform");
	return Bytes();
#endif
}

} }
