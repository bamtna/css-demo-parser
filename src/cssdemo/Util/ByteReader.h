#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace cssdemo {

class ByteReader {
	std::span<const std::byte> data;
	size_t offset = 0;

public:
	explicit ByteReader(std::span<const std::byte> source) : data(source) {}

	size_t position() const { return offset; }
	size_t remaining() const { return data.size() - offset; }
	bool at_end() const { return offset >= data.size(); }

	std::span<const std::byte> read_span(size_t length) {
		require(length);
		auto result = data.subspan(offset, length);
		offset += length;
		return result;
	}

	std::vector<std::byte> read_bytes(size_t length) {
		auto span = read_span(length);
		return { span.begin(), span.end() };
	}

	uint8_t read_uint8() {
		return std::to_integer<uint8_t>(read_span(1)[0]);
	}

	int32_t read_int32() {
		int32_t value;
		std::memcpy(&value, read_span(sizeof(value)).data(), sizeof(value));
		return value;
	}

	float read_float32() {
		float value;
		std::memcpy(&value, read_span(sizeof(value)).data(), sizeof(value));
		return value;
	}

	std::string read_fixed_string(size_t length) {
		auto span = read_span(length);
		std::string s(reinterpret_cast<const char*>(span.data()), length);
		return std::string(s.c_str());
	}

	template <typename T>
	T read_struct() {
		T value;
		std::memcpy(&value, read_span(sizeof(T)).data(), sizeof(T));
		return value;
	}

private:
	void require(size_t length) const {
		if (length > remaining()) {
			throw std::out_of_range("ByteReader: unexpected end of data (wanted " + std::to_string(length)
				+ " bytes, " + std::to_string(remaining()) + " left)");
		}
	}
};

}
