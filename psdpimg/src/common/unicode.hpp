#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace psdpimg::common
{
	extern [[nodiscard]] auto utf8_to_utf16(std::string_view text) -> std::vector<uint16_t>;
	extern [[nodiscard]] auto utf16_to_utf8(std::span<const uint16_t> units) -> std::string;
}
