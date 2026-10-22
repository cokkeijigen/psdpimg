#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "common/image.hpp"

namespace psdpimg::tlg
{
	extern [[nodiscard]] auto is_tlg(std::span<const uint8_t> blob) noexcept -> bool;
	extern [[nodiscard]] auto decode(std::span<const uint8_t> blob) -> psdpimg::common::image;
	extern [[nodiscard]] auto encode(const psdpimg::common::image& picture) -> std::vector<uint8_t>;
}
