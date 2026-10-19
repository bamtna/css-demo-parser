#pragma once
#include <cstddef>
#include <span>
#include <vector>

namespace cssdemo {

// handles "LZSS" and "SNAP" headers, anything else is returned as is
std::vector<std::byte> decompress_buffer(std::span<const std::byte> input);

std::vector<std::byte> lzss_decompress(std::span<const std::byte> input);
std::vector<std::byte> snappy_decompress(std::span<const std::byte> input); // raw Snappy block, no header

}
