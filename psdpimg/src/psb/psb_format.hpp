#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "psb/psb_value.hpp"

namespace psdpimg::psb
{
	inline constexpr uint8_t magic_psb[4]{ 'P', 'S', 'B', 0 };
	inline constexpr uint8_t magic_psd[4]{ '8', 'B', 'P', 'S' };

	enum class obj_type_t : uint8_t
	{
		none           = 0x00u,
		null           = 0x01u,
		false_value    = 0x02u,
		true_value     = 0x03u,
		number_n0      = 0x04u,
		number_n1      = 0x05u,
		number_n2      = 0x06u,
		number_n3      = 0x07u,
		number_n4      = 0x08u,
		number_n5      = 0x09u,
		number_n6      = 0x0Au,
		number_n7      = 0x0Bu,
		number_n8      = 0x0Cu,
		array_n1       = 0x0Du,
		array_n8       = 0x14u,
		string_n1      = 0x15u,
		string_n4      = 0x18u,
		resource_n1    = 0x19u,
		resource_n4    = 0x1Cu,
		float0         = 0x1Du,
		float32        = 0x1Eu,
		float64        = 0x1Fu,
		list           = 0x20u,
		objects        = 0x21u,
		extra_chunk_n1 = 0x22u,
		extra_chunk_n4 = 0x25u,
	};

	struct archive
	{
		uint16_t version{};
		value    root{};
	};

	extern auto          read(std::span<const uint8_t> bytes) -> archive;
	extern auto         write(const archive& source) -> std::vector<uint8_t>;
	extern auto header_length(uint16_t version) noexcept -> uint32_t;
}
