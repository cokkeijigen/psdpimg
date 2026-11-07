#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace psdpimg::psd
{
	inline constexpr uint8_t signature_psd[4]{ '8', 'B', 'P', 'S' };
	inline constexpr size_t  header_size{ 26 };

	#pragma pack(push, 1)
	struct header
	{
		uint8_t  signature[4];
		uint16_t version;
		uint8_t  reserved[6];
		uint16_t channels;
		uint32_t height;
		uint32_t width;
		uint16_t depth;
		uint16_t color_mode;
	};
	#pragma pack(pop)

	static_assert(sizeof(psdpimg::psd::header) == psdpimg::psd::header_size);

	class view
	{
		std::span<const uint8_t>    m_raw{};
		const psdpimg::psd::header* m_header{};
		std::span<const uint8_t>    m_resources{};
		std::span<const uint8_t>    m_layer_block{};

	public:
		inline view() noexcept = default;

		inline explicit view(const std::span<const uint8_t> bytes) noexcept :
			m_raw{ bytes }
		{
			if (bytes.size() < psdpimg::psd::header_size)
			{
				return;
			}

			const psdpimg::psd::header* const candidate{ reinterpret_cast<const psdpimg::psd::header*>(bytes.data()) };

			if (std::memcmp(candidate->signature, psdpimg::psd::signature_psd, 4) != 0)
			{
				return;
			}

			const uint32_t color_mode_length{ std::byteswap(*reinterpret_cast<const uint32_t*>(bytes.data() + 26)) };

			const size_t resources_at{ 30 + color_mode_length };

			if (resources_at > bytes.size() - sizeof(uint32_t))
			{
				return;
			}

			const uint32_t resources_length{ std::byteswap(*reinterpret_cast<const uint32_t*>(bytes.data() + resources_at)) };

			if (resources_length > bytes.size() - resources_at - sizeof(uint32_t))
			{
				return;
			}

			const size_t layer_at{ resources_at + sizeof(uint32_t) + resources_length };

			if (layer_at > bytes.size() - sizeof(uint32_t))
			{
				return;
			}

			const uint32_t layer_length{ std::byteswap(*reinterpret_cast<const uint32_t*>(bytes.data() + layer_at)) };

			if (layer_length > bytes.size() - layer_at - sizeof(uint32_t))
			{
				return;
			}

			this->m_header      = candidate;
			this->m_resources   = bytes.subspan(resources_at + sizeof(uint32_t), resources_length);
			this->m_layer_block = bytes.subspan(layer_at + sizeof(uint32_t), layer_length);
		}

		[[nodiscard]] inline auto is_valid() const noexcept -> bool
		{
			return this->m_header != nullptr;
		}

		[[nodiscard]] inline auto raw() const noexcept -> std::span<const uint8_t>
		{
			return this->m_raw;
		}

		[[nodiscard]] inline auto header() const noexcept -> const psdpimg::psd::header*
		{
			return this->m_header;
		}

		[[nodiscard]] inline auto resources() const noexcept -> std::span<const uint8_t>
		{
			return this->m_resources;
		}

		[[nodiscard]] inline auto layer_block() const noexcept -> std::span<const uint8_t>
		{
			return this->m_layer_block;
		}
	};
}
