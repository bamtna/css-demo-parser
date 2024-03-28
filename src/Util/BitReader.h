#pragma once

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <bit>
#include <cstring>
#include <cstddef>
#include <cmath>

class BitReader {
    std::vector<std::byte> data;
    size_t bit_offset = 0;

public:
    explicit BitReader(const std::vector<std::byte>& source)
        : data(source) {}

    int bits_left() const {
        int total_bits = static_cast<int>(data.size()) * 8;
        return total_bits - bit_offset;
    }

    bool read_bool() {
        return read_bit() != 0;
    }

    int read_bit() {
        if (bit_offset >= static_cast<int>(data.size()) * 8) {
            throw std::out_of_range("Attempting to read beyond the buffer limit.");
        }
        int byte_index = bit_offset / 8;
        int bit_in_byte = bit_offset % 8;
        int bit_value = (std::to_integer<int>(data[byte_index]) >> bit_in_byte) & 1;
        ++bit_offset;
        return bit_value;
    }

    uint32_t read_bits(int num_bits) {
        if (num_bits > 32) throw std::invalid_argument("Bit count exceeds 32");
        uint32_t result = 0;
        for (int i = 0; i < num_bits; ++i) {
            result |= (read_bit() << i);
        }
        return result;
    }

    uint32_t peek_bits(int num_bits) {
        if (num_bits > 32) throw std::invalid_argument("Bit count exceeds 32");

        int saved_bit_offset = bit_offset;
        uint32_t result = read_bits(num_bits);
        bit_offset = saved_bit_offset;

        return result;
    }

    std::vector<bool> read_bit_array(size_t num_bits) {
        std::vector<bool> bit_array;
        bit_array.reserve(num_bits);

        for (size_t i = 0; i < num_bits; ++i) {
            bit_array.push_back(read_bit());
        }

        return bit_array;
    }

    std::vector<std::byte> read_many_bits(size_t num_bits) {
        std::vector<std::byte> result;
        size_t num_full_bytes = num_bits / 8;
        size_t remaining_bits = num_bits % 8;

        for (size_t i = 0; i < num_full_bytes; ++i) {
            uint32_t byte = read_bits(8);
            result.push_back(static_cast<std::byte>(byte));
        }
        if (remaining_bits > 0) {
            uint32_t last_partial_byte = read_bits(remaining_bits);
            result.push_back(static_cast<std::byte>(last_partial_byte));
        }

        return result;
    }


    int read_signed_bits(int num_bits) {
        if (num_bits == 0 || num_bits > 32) throw std::invalid_argument("Bit count out of range");
        int shift = 32 - num_bits;
        return (read_bits(num_bits) << shift) >> shift; // sign extend
    }

    std::byte read_byte() {
        return std::byte(read_bits(8));
    }

    std::vector<std::byte> read_bytes(size_t count) {
        std::vector<std::byte> bytes;
        bytes.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            bytes.push_back(read_byte());
        }
        return bytes;
    }

    std::string read_ascii_string(int limit = 0) {
        std::string result;
        while (limit == 0 || result.size() < limit) {
            char val = static_cast<char>(read_bits(8));
            if (val == '\0') {
                break;
            }
            result += val;
        }

        return result;
    }

    int8_t read_int8() {
        return static_cast<int8_t>(read_signed_bits(8));
    }

    uint8_t read_uint8() {
        return static_cast<uint8_t>(read_bits(8));
    }

    int16_t read_short() {
        return (short)read_bits(16);
    }

    int16_t read_int16() {
        return static_cast<int16_t>(read_signed_bits(16));
    }

    uint16_t read_uint16() {
        return static_cast<uint16_t>(read_bits(16));
    }

    int32_t read_int32() {
        return read_signed_bits(32);
    }

    uint32_t read_uint32() {
        return read_bits(32);
    }

    float read_float32() {
        uint32_t bits = read_uint32();
        float result;
        std::memcpy(&result, &bits, sizeof(bits));
        return result;
    }

    float read_bit_angle(int numbits) {
        if (numbits <= 0 || numbits > 32) {
            throw std::invalid_argument("Invalid bit count for read_bit_angle");
        }
        float shift = std::pow(2.0f, static_cast<float>(numbits));
        uint32_t i = read_bits(numbits);
        return static_cast<float>(i) * (360.0f / shift);
    }

    Vector read_bit_vec3_coord() {
        Vector fa{};
        int xflag = read_bit();
        int yflag = read_bit();
        int zflag = read_bit();

        if (xflag)
            fa.x = read_bit_coord();
        if (yflag)
            fa.y = read_bit_coord();
        if (zflag)
            fa.z = read_bit_coord();
        return fa;
    }

    float read_bit_coord() {
        int intval = 0, fractval = 0, signbit = 0;
        float value = 0.0;

        intval = read_bit();
        fractval = read_bit();

        if (intval || fractval) {
            signbit = read_bit();

            if (intval) {
                intval = read_bits(COORD_INTEGER_BITS) + 1;
            }
            if (fractval) {
                fractval = read_bits(COORD_FRACTIONAL_BITS);
            }
            value = intval + (static_cast<float>(fractval) * COORD_RESOLUTION);
            if (signbit) value = -value;
        }

        return value;
    }

    uint32_t read_var_int32() {
        uint32_t result = 0;
        int count = 0;
        uint32_t b;

        do {
            if (count == 5) {
                throw std::runtime_error("VarInt32 too long");
            }
            b = read_bits(8);
            result |= (b & 0x7F) << (7 * count); 
            ++count;
        } while (b & 0x80);

        return result;
    }

    void seek(int position) {
        if (position > static_cast<int>(data.size()) * 8) {
            throw std::out_of_range("Seek position is beyond the buffer limit.");
        }
        bit_offset = position;
    }

    int tell() const {
        return bit_offset;
    }

    void reset() {
        bit_offset = 0;
    }
};
