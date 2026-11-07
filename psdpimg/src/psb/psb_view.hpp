#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <psb_format.hpp>

namespace psdpimg::psb
{
	inline constexpr size_t header_common_size{ 40 };

	#pragma pack(push, 1)
	struct header
	{
		uint8_t  signature[4];
		uint16_t version;
		uint16_t reserved;
		uint32_t header_length;
		uint32_t names;
		uint32_t strings;
		uint32_t strings_data;
		uint32_t chunk_offsets;
		uint32_t chunk_lengths;
		uint32_t chunk_data;
		uint32_t entries;
	};
	#pragma pack(pop)

	static_assert(sizeof(psdpimg::psb::header) == psdpimg::psb::header_common_size);

	class view
	{
		std::span<const uint8_t>    m_raw{};
		const psdpimg::psb::header* m_header{};

	public:
		inline view() noexcept = default;

		inline explicit view(const std::span<const uint8_t> bytes) noexcept :
			m_raw{ bytes }
		{
			if (bytes.size() < psdpimg::psb::header_common_size)
			{
				return;
			}

			const psdpimg::psb::header* const candidate{ reinterpret_cast<const psdpimg::psb::header*>(bytes.data()) };

			if (std::memcmp(candidate->signature, psdpimg::psb::magic_psb, 4) != 0)
			{
				return;
			}

			this->m_header = candidate;
		}

		[[nodiscard]] inline auto is_valid() const noexcept -> bool
		{
			return this->m_header != nullptr;
		}

		[[nodiscard]] inline auto raw() const noexcept -> std::span<const uint8_t>
		{
			return this->m_raw;
		}

		[[nodiscard]] inline auto header() const noexcept -> const psdpimg::psb::header*
		{
			return this->m_header;
		}
	};
}
