#pragma once
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Protocol/Types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace cssdemo {

struct BitReaderOverflow : std::out_of_range {
	using std::out_of_range::out_of_range;
};

class BitReader;

struct BitBuffer {
	std::vector<std::byte> bytes;
	size_t num_bits = 0;

	BitBuffer() = default;
	explicit BitBuffer(std::vector<std::byte> data)
		: bytes(std::move(data)), num_bits(bytes.size() * 8) {}
	BitBuffer(std::vector<std::byte> data, size_t bits)
		: bytes(std::move(data)), num_bits(bits) {}

	BitReader reader() const;
	bool empty() const { return num_bits == 0; }
};

// bf_read, non-owning
class BitReader {
	std::span<const std::byte> data;
	size_t end_bit = 0;
	size_t bit_offset = 0;

public:
	BitReader() = default;
	explicit BitReader(std::span<const std::byte> source)
		: data(source), end_bit(source.size() * 8) {}
	BitReader(std::span<const std::byte> source, size_t num_bits)
		: data(source), end_bit(num_bits) {
		if (num_bits > source.size() * 8) throw std::invalid_argument("BitReader: bit count exceeds buffer");
	}

	size_t position() const { return bit_offset; }
	size_t size_bits() const { return end_bit; }
	size_t bits_left() const { return end_bit - bit_offset; }
	bool at_end() const { return bit_offset >= end_bit; }

	void seek(size_t position) {
		if (position > end_bit) throw BitReaderOverflow("BitReader: seek past end of buffer");
		bit_offset = position;
	}
	void skip(size_t num_bits) { seek(bit_offset + num_bits); }

	bool read_bool() { return read_bits(1) != 0; }
	int read_bit() { return static_cast<int>(read_bits(1)); }

	// ReadUBitLong
	uint32_t read_bits(int num_bits) {
		if (num_bits < 0 || num_bits > 32) throw std::invalid_argument("BitReader: bit count must be 0-32");
		if (static_cast<size_t>(num_bits) > bits_left()) {
			throw BitReaderOverflow("BitReader: read past end of buffer");
		}
		if (num_bits == 0) return 0;

		uint64_t chunk = 0;
		size_t first_byte = bit_offset / 8;
		size_t last_byte = (bit_offset + num_bits + 7) / 8;
		for (size_t i = first_byte; i < last_byte; ++i) {
			chunk |= static_cast<uint64_t>(std::to_integer<uint8_t>(data[i])) << ((i - first_byte) * 8);
		}
		chunk >>= bit_offset % 8;
		bit_offset += num_bits;
		return static_cast<uint32_t>(chunk & ((1ull << num_bits) - 1));
	}

	uint32_t peek_bits(int num_bits) {
		size_t saved = bit_offset;
		uint32_t result = read_bits(num_bits);
		bit_offset = saved;
		return result;
	}

	// ReadSBitLong
	int32_t read_signed_bits(int num_bits) {
		if (num_bits <= 0 || num_bits > 32) throw std::invalid_argument("BitReader: bit count must be 1-32");
		int shift = 32 - num_bits;
		return static_cast<int32_t>(read_bits(num_bits) << shift) >> shift;
	}

	uint8_t read_uint8() { return static_cast<uint8_t>(read_bits(8)); }
	int8_t read_int8() { return static_cast<int8_t>(read_bits(8)); }
	uint16_t read_uint16() { return static_cast<uint16_t>(read_bits(16)); }
	int16_t read_int16() { return static_cast<int16_t>(read_bits(16)); }
	uint32_t read_uint32() { return read_bits(32); }
	int32_t read_int32() { return static_cast<int32_t>(read_bits(32)); }

	float read_float32() {
		uint32_t bits = read_uint32();
		float result;
		std::memcpy(&result, &bits, sizeof(bits));
		return result;
	}

	std::vector<std::byte> read_bytes(size_t count) {
		std::vector<std::byte> result;
		result.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			result.push_back(static_cast<std::byte>(read_bits(8)));
		}
		return result;
	}

	BitBuffer read_buffer(size_t num_bits) {
		if (num_bits > bits_left()) throw BitReaderOverflow("BitReader: read past end of buffer");
		std::vector<std::byte> bytes;
		bytes.reserve((num_bits + 7) / 8);
		size_t remaining = num_bits;
		while (remaining >= 8) {
			bytes.push_back(static_cast<std::byte>(read_bits(8)));
			remaining -= 8;
		}
		if (remaining) {
			bytes.push_back(static_cast<std::byte>(read_bits(static_cast<int>(remaining))));
		}
		return BitBuffer(std::move(bytes), num_bits);
	}

	// ReadString, always consumes the terminator even when truncating
	std::string read_string(size_t max_length = 0) {
		std::string result;
		for (;;) {
			char c = static_cast<char>(read_bits(8));
			if (c == '\0') break;
			if (max_length == 0 || result.size() + 1 < max_length) result += c;
		}
		return result;
	}

	std::string read_fixed_string(size_t length) {
		std::string result;
		for (size_t i = 0; i < length; ++i) {
			result += static_cast<char>(read_bits(8));
		}
		return std::string(result.c_str());
	}

	float read_bit_angle(int num_bits) {
		uint32_t value = read_bits(num_bits);
		return static_cast<float>(value) * (360.0f / static_cast<float>(1ull << num_bits));
	}

	float read_bit_coord() {
		int intval = read_bit();
		int fractval = read_bit();
		if (!intval && !fractval) {
			return 0.0f;
		}
		int signbit = read_bit();
		if (intval) intval = static_cast<int>(read_bits(COORD_INTEGER_BITS)) + 1;
		if (fractval) fractval = static_cast<int>(read_bits(COORD_FRACTIONAL_BITS));
		float value = intval + fractval * COORD_RESOLUTION;
		return signbit ? -value : value;
	}

	float read_bit_coord_mp(bool integral, bool low_precision) {
		bool in_bounds = read_bool();
		int signbit = 0;
		float value = 0.0f;

		if (integral) {
			int intval = read_bit();
			if (intval) {
				signbit = read_bit();
				intval = static_cast<int>(read_bits(in_bounds ? COORD_INTEGER_BITS_MP : COORD_INTEGER_BITS)) + 1;
			}
			value = static_cast<float>(intval);
		}
		else {
			int intval = read_bit();
			signbit = read_bit();
			if (intval) {
				intval = static_cast<int>(read_bits(in_bounds ? COORD_INTEGER_BITS_MP : COORD_INTEGER_BITS)) + 1;
			}
			if (low_precision) {
				value = intval + read_bits(COORD_FRACTIONAL_BITS_MP_LOWPRECISION) * COORD_RESOLUTION_LOWPRECISION;
			}
			else {
				value = intval + read_bits(COORD_FRACTIONAL_BITS) * COORD_RESOLUTION;
			}
		}
		return signbit ? -value : value;
	}

	float read_bit_normal() {
		int signbit = read_bit();
		float value = read_bits(NORMAL_FRACTIONAL_BITS) * NORMAL_RESOLUTION;
		return signbit ? -value : value;
	}

	Vector read_bit_vec3_coord() {
		Vector v{};
		bool has_x = read_bool();
		bool has_y = read_bool();
		bool has_z = read_bool();
		if (has_x) v.x = read_bit_coord();
		if (has_y) v.y = read_bit_coord();
		if (has_z) v.z = read_bit_coord();
		return v;
	}

	// ReadUBitVar
	uint32_t read_ubit_var() {
		uint32_t sixbits = read_bits(6);
		uint32_t encoding = sixbits & 3;
		if (encoding == 0) {
			return sixbits >> 2;
		}
		bit_offset -= 4;
		static constexpr int payload_bits[] = { 4, 8, 12, 32 };
		return read_bits(payload_bits[encoding]);
	}

	// ReadVarInt32
	uint32_t read_var_int32() {
		uint32_t result = 0;
		for (int count = 0; count < 5; ++count) {
			uint32_t b = read_bits(8);
			result |= (b & 0x7F) << (7 * count);
			if (!(b & 0x80)) return result;
		}
		throw std::runtime_error("BitReader: varint too long");
	}

	int32_t read_signed_var_int32() {
		uint32_t n = read_var_int32();
		return static_cast<int32_t>(n >> 1) ^ -static_cast<int32_t>(n & 1); // zigzag
	}
};

inline BitReader BitBuffer::reader() const
{
	return BitReader(bytes, num_bits);
}

}
