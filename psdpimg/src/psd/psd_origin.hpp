#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "psd/psd_document.hpp"

namespace psdpimg::psd
{
	struct origin
	{
		std::vector<uint8_t> bytes{};
		uint64_t             stamp{};
	};

	extern [[nodiscard]] auto  origin_stamp(const document& source) noexcept -> uint64_t;
	extern [[nodiscard]] auto   pack_origin(const origin& source) -> std::vector<uint8_t>;
	extern [[nodiscard]] auto unpack_origin(std::span<const uint8_t> data) -> origin;
}
