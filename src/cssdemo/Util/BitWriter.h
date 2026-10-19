#pragma once
#include "BitReader.h"
#include "cssdemo/Protocol/Constants.h"
#include "cssdemo/Protocol/Types.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace cssdemo {

// bf_write, growing
class BitWriter {
	std::vector<std::byte> data;
	size_t bit_offset = 0;

public:
	size_t position() const { return bit_offset; }
	size_t size_bytes() const { return (bit_offset + 7) / 8; }
	const std::vector<std::byte>& bytes() const { return data; }

	// unused bits of the last byte are zero
	std::vector<std::byte> take_bytes() {
		std::vector<std::byte> result = std::move(data);
		data.clear();
		bit_offset = 0;
		return result;
	}
	BitBuffer take_buffer() {
		size_t bits = bit_offset;
		return BitBuffer(take_bytes(), bits);
	}
	BitReader reader() const { return BitReader(data, bit_offset); }

	void write_bool(bool value) { write_bits(value ? 1 : 0, 1); }
	void write_bit(int value) { write_bits(value ? 1 : 0, 1); }

	// WriteUBitLong, bits above num_bits are ignored
	void write_bits(uint32_t value, int num_bits) {
		if (num_bits < 0 || num_bits > 32) throw std::invalid_argument("BitWriter: bit count must be 0-32");
		for (int i = 0; i < num_bits;) {
			size_t byte = bit_offset / 8;
			int shift = static_cast<int>(bit_offset % 8);
			if (shift == 0) data.push_back(std::byte{ 0 });
			int count = std::min(8 - shift, num_bits - i);
			uint32_t chunk = (value >> i) & ((1u << count) - 1);
			data[byte] |= static_cast<std::byte>(chunk << shift);
			i += count;
			bit_offset += count;
		}
	}

	// WriteSBitLong
	void write_signed_bits(int32_t value, int num_bits) {
		if (num_bits <= 0 || num_bits > 32) throw std::invalid_argument("BitWriter: bit count must be 1-32");
		write_bits(static_cast<uint32_t>(value), num_bits);
	}

	void write_uint8(uint8_t value) { write_bits(value, 8); }
	void write_int8(int8_t value) { write_bits(static_cast<uint8_t>(value), 8); }
	void write_uint16(uint16_t value) { write_bits(value, 16); }
	void write_int16(int16_t value) { write_bits(static_cast<uint16_t>(value), 16); }
	void write_uint32(uint32_t value) { write_bits(value, 32); }
	void write_int32(int32_t value) { write_bits(static_cast<uint32_t>(value), 32); }

	void write_float32(float value) {
		uint32_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		write_uint32(bits);
	}

	void write_bytes(std::span<const std::byte> bytes) {
		for (std::byte b : bytes) write_bits(std::to_integer<uint8_t>(b), 8);
	}

	void write_buffer(const BitBuffer& buffer) {
		BitReader source = buffer.reader();
		write_from(source, buffer.num_bits);
	}

	void write_from(BitReader& source, size_t num_bits) {
		while (num_bits >= 32) {
			write_bits(source.read_bits(32), 32);
			num_bits -= 32;
		}
		if (num_bits) write_bits(source.read_bits(static_cast<int>(num_bits)), static_cast<int>(num_bits));
	}

	// WriteString
	void write_string(const std::string& value) {
		for (char c : value) {
			if (c == '\0') break;
			write_uint8(static_cast<uint8_t>(c));
		}
		write_uint8(0);
	}

	// exactly length bytes, zero padded
	void write_fixed_string(const std::string& value, size_t length) {
		for (size_t i = 0; i < length; ++i) {
			write_uint8(i < value.size() ? static_cast<uint8_t>(value[i]) : 0);
		}
	}

	void write_bit_angle(float angle, int num_bits) {
		uint32_t shift = 1u << num_bits;
		uint32_t mask = shift - 1;
		int d = static_cast<int>((angle / 360.0) * shift);
		write_bits(static_cast<uint32_t>(d) & mask, num_bits);
	}

	// -0 keeps its sign bit so decoded values re-encode to the same bits
	void write_bit_coord(float f) {
		int signbit = f <= -COORD_RESOLUTION || (f == 0.0f && std::signbit(f));
		int intval = static_cast<int>(std::fabs(f));
		int fractval = std::abs(static_cast<int>(f * COORD_DENOMINATOR)) & (COORD_DENOMINATOR - 1);

		write_bit(intval);
		write_bit(fractval);
		if (intval || fractval) {
			write_bit(signbit);
			if (intval) write_bits(static_cast<uint32_t>(intval - 1), COORD_INTEGER_BITS);
			if (fractval) write_bits(static_cast<uint32_t>(fractval), COORD_FRACTIONAL_BITS);
		}
	}

	void write_bit_coord_mp(float f, bool integral, bool low_precision) {
		float resolution = low_precision ? COORD_RESOLUTION_LOWPRECISION : COORD_RESOLUTION;
		int signbit = f <= -resolution || (f == 0.0f && std::signbit(f));
		int intval = static_cast<int>(std::fabs(f));
		int fractval = low_precision
			? (std::abs(static_cast<int>(f * COORD_DENOMINATOR_LOWPRECISION)) & (COORD_DENOMINATOR_LOWPRECISION - 1))
			: (std::abs(static_cast<int>(f * COORD_DENOMINATOR)) & (COORD_DENOMINATOR - 1));
		bool in_bounds = intval < (1 << COORD_INTEGER_BITS_MP);
		int int_bits = in_bounds ? COORD_INTEGER_BITS_MP : COORD_INTEGER_BITS;

		write_bool(in_bounds);
		if (integral) {
			write_bit(intval);
			if (intval) {
				write_bit(signbit);
				write_bits(static_cast<uint32_t>(intval - 1), int_bits);
			}
		}
		else {
			write_bit(intval);
			write_bit(signbit);
			if (intval) write_bits(static_cast<uint32_t>(intval - 1), int_bits);
			write_bits(static_cast<uint32_t>(fractval), low_precision ? COORD_FRACTIONAL_BITS_MP_LOWPRECISION : COORD_FRACTIONAL_BITS);
		}
	}

	void write_bit_normal(float f) {
		int signbit = f <= -NORMAL_RESOLUTION || (f == 0.0f && std::signbit(f));
		unsigned int fractval = static_cast<unsigned int>(std::abs(static_cast<int>(f * NORMAL_DENOMINATOR)));
		if (fractval > NORMAL_DENOMINATOR) fractval = NORMAL_DENOMINATOR;
		// truncation can land one below a decoded value
		float magnitude = std::fabs(f);
		if (fractval < NORMAL_DENOMINATOR && (fractval + 1) * NORMAL_RESOLUTION == magnitude) fractval++;
		write_bit(signbit);
		write_bits(fractval, NORMAL_FRACTIONAL_BITS);
	}

	void write_bit_vec3_coord(const Vector& v) {
		bool has_x = v.x >= COORD_RESOLUTION || v.x <= -COORD_RESOLUTION;
		bool has_y = v.y >= COORD_RESOLUTION || v.y <= -COORD_RESOLUTION;
		bool has_z = v.z >= COORD_RESOLUTION || v.z <= -COORD_RESOLUTION;
		write_bool(has_x);
		write_bool(has_y);
		write_bool(has_z);
		if (has_x) write_bit_coord(v.x);
		if (has_y) write_bit_coord(v.y);
		if (has_z) write_bit_coord(v.z);
	}

	// WriteUBitVar
	void write_ubit_var(uint32_t value) {
		if (value < 0x10u) { write_bits(0, 2); write_bits(value, 4); }
		else if (value < 0x100u) { write_bits(1, 2); write_bits(value, 8); }
		else if (value < 0x1000u) { write_bits(2, 2); write_bits(value, 12); }
		else { write_bits(3, 2); write_bits(value, 32); }
	}

	// WriteVarInt32
	void write_var_int32(uint32_t value) {
		while (value >= 0x80) {
			write_uint8(static_cast<uint8_t>(value | 0x80));
			value >>= 7;
		}
		write_uint8(static_cast<uint8_t>(value));
	}

	void write_signed_var_int32(int32_t value) {
		write_var_int32((static_cast<uint32_t>(value) << 1) ^ static_cast<uint32_t>(value >> 31)); // zigzag
	}
};

}
