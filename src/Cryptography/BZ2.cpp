#include "BZ2.h"

#ifdef NATIVE
#include <bzlib.h>
#endif

#include <cstring>
#include <vector>
#include "../Log.h"

namespace RNS { namespace Cryptography {

const Bytes bz2_decompress(const Bytes& data) {
#ifdef NATIVE
	if (data.empty()) {
		return Bytes();
	}

	// Start with output buffer sized for highly compressed data
	// Pattern data can have 1000x+ compression ratios (2MB -> 278 bytes)
	// Using 2MB minimum ensures single-iteration decompression for most cases
	const size_t MIN_OUTPUT_SIZE = 2 * 1024 * 1024;  // 2MB minimum
	size_t output_size = std::max(data.size() * 100, MIN_OUTPUT_SIZE);
	std::vector<char> output(output_size);

	bz_stream stream;
	memset(&stream, 0, sizeof(stream));

	int ret = BZ2_bzDecompressInit(&stream, 0, 0);
	if (ret != BZ_OK) {
		return Bytes();
	}

	stream.next_in = const_cast<char*>(reinterpret_cast<const char*>(data.data()));
	stream.avail_in = data.size();
	stream.next_out = output.data();
	stream.avail_out = output_size;

	Bytes result;
	int iteration = 0;
	do {
		ret = BZ2_bzDecompress(&stream);
		iteration++;

		if (ret == BZ_OK || ret == BZ_STREAM_END) {
			// Append decompressed data
			size_t decompressed = output_size - stream.avail_out;
			result.append(reinterpret_cast<uint8_t*>(output.data()), decompressed);

			DEBUGF("bz2_decompress: iter=%d, ret=%d, decompressed=%zu, total=%zu, avail_in=%u",
				iteration, ret, decompressed, result.size(), stream.avail_in);

			if (ret == BZ_STREAM_END) {
				break;
			}

			// Reset output buffer for more data
			stream.next_out = output.data();
			stream.avail_out = output_size;
		} else {
			DEBUGF("bz2_decompress: iter=%d, FAILED ret=%d", iteration, ret);
			BZ2_bzDecompressEnd(&stream);
			return Bytes();
		}
	} while (stream.avail_in > 0 || ret != BZ_STREAM_END);

	BZ2_bzDecompressEnd(&stream);
	DEBUGF("bz2_decompress: final output size=%zu", result.size());
	return result;
#else
	ERROR("bz2_decompress: BZ2 support not available on this platform");
	return Bytes();
#endif
}

const Bytes bz2_compress(const Bytes& data) {
#ifdef NATIVE
	if (data.empty()) {
		return Bytes();
	}

	// Compressed size is at most input size + 1% + 600 bytes
	size_t output_size = data.size() + data.size() / 100 + 600;
	std::vector<char> output(output_size);

	bz_stream stream;
	memset(&stream, 0, sizeof(stream));

	// Block size 9 (900k) with default work factor
	int ret = BZ2_bzCompressInit(&stream, 9, 0, 0);
	if (ret != BZ_OK) {
		return Bytes();
	}

	stream.next_in = const_cast<char*>(reinterpret_cast<const char*>(data.data()));
	stream.avail_in = data.size();
	stream.next_out = output.data();
	stream.avail_out = output_size;

	ret = BZ2_bzCompress(&stream, BZ_FINISH);
	if (ret != BZ_STREAM_END) {
		BZ2_bzCompressEnd(&stream);
		return Bytes();
	}

	size_t compressed_size = output_size - stream.avail_out;
	BZ2_bzCompressEnd(&stream);

	return Bytes(reinterpret_cast<uint8_t*>(output.data()), compressed_size);
#else
	ERROR("bz2_compress: BZ2 support not available on this platform");
	return Bytes();
#endif
}

} }
