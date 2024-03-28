#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

class BinaryReader {
    std::ifstream& file;

public:
    explicit BinaryReader(std::ifstream& file) : file(file) {}

    void seek(std::streampos position, std::ios_base::seekdir direction = std::ios::beg) {
        file.seekg(position, direction);
    }

    std::vector<std::byte> read_bytes(size_t length) {
        std::vector<std::byte> buffer(length);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), length)) {
            throw std::runtime_error("Failed to read " + std::to_string(length) + " bytes or reached EOF.");
        }
        return buffer;
    }

    std::string read_string(size_t length) {
        std::vector<char> buffer(length + 1, '\0');
        file.read(buffer.data(), length);
        return std::string(buffer.data());
    }

    uint8_t read_byte() {
        char buffer;
        if (!file.read(&buffer, 1)) {
            throw std::runtime_error("Failed to read byte or reached EOF.");
        }
        return buffer;
    }

    int32_t read_int32() {
        int32_t value = 0;
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
        return value;
    }

    float read_float32() {
        float value = 0.0f;
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
        return value;
    }

    bool eof() const {
        return file.eof();
    }
};
