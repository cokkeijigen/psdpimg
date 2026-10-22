#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psdpimg::psd
{
	extern [[nodiscard]] auto   pack_bits(std::span<const uint8_t> source) -> std::vector<uint8_t>;
	extern [[nodiscard]] auto unpack_bits(std::span<const uint8_t> source, size_t expected)
		-> std::vector<uint8_t>;
}
