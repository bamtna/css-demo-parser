#include "Compression.h"
#include <cstdint>
#include <stdexcept>

namespace cssdemo {

namespace {

bool has_magic(std::span<const std::byte> input, const char (&magic)[5])
{
	if (input.size() < 4) return false;
	for (int i = 0; i < 4; i++) {
		if (input[i] != static_cast<std::byte>(magic[i])) return false;
	}
	return true;
}

class InputCursor {
	std::span<const std::byte> input;
	const char* name;

public:
	size_t pos;

	InputCursor(std::span<const std::byte> in, size_t start, const char* codec)
		: input(in), name(codec), pos(start) {}

	bool at_end() const { return pos >= input.size(); }

	uint32_t next() {
		if (pos >= input.size()) throw std::runtime_error(std::string(name) + ": input truncated");
		return std::to_integer<uint32_t>(input[pos++]);
	}

	uint32_t peek() const {
		if (pos >= input.size()) throw std::runtime_error(std::string(name) + ": input truncated");
		return std::to_integer<uint32_t>(input[pos]);
	}

	uint32_t next_le(int num_bytes) {
		uint32_t value = 0;
		for (int i = 0; i < num_bytes; i++) value |= next() << (8 * i);
		return value;
	}
};

void copy_back(std::vector<std::byte>& output, size_t distance, size_t count, const char* codec)
{
	if (distance == 0 || distance > output.size()) {
		throw std::runtime_error(std::string(codec) + ": back-reference before start of output");
	}
	size_t source = output.size() - distance;
	for (size_t i = 0; i < count; i++) {
		output.push_back(output[source + i]);
	}
}

} // namespace

std::vector<std::byte> lzss_decompress(std::span<const std::byte> input)
{
	// "LZSS" + size, then a command byte per 8 items, set bit = 12 bit pos / 4 bit count backref
	InputCursor in(input, 4, "LZSS");
	uint32_t actual_size = in.next_le(4);

	std::vector<std::byte> output;
	output.reserve(actual_size);

	uint32_t cmd_byte = 0;
	int cmd_bits_left = 0;
	for (;;) {
		if (cmd_bits_left == 0) {
			cmd_byte = in.next();
			cmd_bits_left = 8;
		}
		if (cmd_byte & 1) {
			uint32_t position = in.next() << 4;
			position |= in.peek() >> 4;
			uint32_t count = (in.next() & 0x0F) + 1;
			if (count == 1) {
				break;
			}
			copy_back(output, position + 1, count, "LZSS");
		}
		else {
			output.push_back(static_cast<std::byte>(in.next()));
		}
		cmd_byte >>= 1;
		cmd_bits_left--;
	}

	if (output.size() != actual_size) {
		throw std::runtime_error("LZSS: decompressed size mismatch");
	}
	return output;
}

std::vector<std::byte> snappy_decompress(std::span<const std::byte> input)
{
	InputCursor in(input, 0, "Snappy");

	uint32_t expected_size = 0;
	for (int shift = 0;; shift += 7) {
		if (shift > 28) throw std::runtime_error("Snappy: bad length varint");
		uint32_t b = in.next();
		expected_size |= (b & 0x7F) << shift;
		if (!(b & 0x80)) break;
	}

	std::vector<std::byte> output;
	output.reserve(expected_size);

	while (!in.at_end()) {
		uint32_t tag = in.next();
		switch (tag & 3) {
		case 0: { // literal
			uint32_t length = tag >> 2;
			if (length >= 60) length = in.next_le(length - 59);
			length += 1;
			for (uint32_t i = 0; i < length; i++) {
				output.push_back(static_cast<std::byte>(in.next()));
			}
			break;
		}
		case 1: { // copy, 11-bit offset
			uint32_t length = 4 + ((tag >> 2) & 7);
			uint32_t offset = ((tag >> 5) << 8) | in.next();
			copy_back(output, offset, length, "Snappy");
			break;
		}
		case 2: // copy, 16-bit offset
			copy_back(output, in.next_le(2), (tag >> 2) + 1, "Snappy");
			break;
		case 3: // copy, 32-bit offset
			copy_back(output, in.next_le(4), (tag >> 2) + 1, "Snappy");
			break;
		}
	}

	if (output.size() != expected_size) {
		throw std::runtime_error("Snappy: decompressed size mismatch");
	}
	return output;
}

std::vector<std::byte> decompress_buffer(std::span<const std::byte> input)
{
	if (has_magic(input, "LZSS")) {
		return lzss_decompress(input);
	}
	if (has_magic(input, "SNAP")) {
		return snappy_decompress(input.subspan(4));
	}
	return { input.begin(), input.end() };
}

}
