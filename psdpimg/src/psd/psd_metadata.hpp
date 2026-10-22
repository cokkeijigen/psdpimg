#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace psdpimg::psd
{
	extern [[nodiscard]] auto resolution_info() noexcept -> std::span<const uint8_t>;
	extern [[nodiscard]] auto    version_info() noexcept -> std::span<const uint8_t>;
	extern [[nodiscard]] auto      xmp_packet() noexcept -> std::string_view;
	extern [[nodiscard]] auto        xmp_name() noexcept -> std::string_view;
}
