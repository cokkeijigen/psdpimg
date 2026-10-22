#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace psdpimg::psd
{
	enum class section_t : uint32_t
	{
		layer         = 0u,
		open_folder   = 1u,
		closed_folder = 2u,
		divider       = 3u,
	};

	struct tag
	{
		std::string          key{};
		std::vector<uint8_t> data{};
	};

	struct layer
	{
		int32_t              left{};
		int32_t              top{};
		int32_t              right{};
		int32_t              bottom{};
		uint8_t              opacity{ 255 };
		uint8_t              clipping{};
		bool                 visible{ true };
		section_t            section{ psdpimg::psd::section_t::layer };
		bool                 has_layer_id{};
		uint32_t             layer_id{};
		bool                 placeholder{};
		std::string          name{};
		std::string          unicode_name{};
		std::vector<uint8_t> pixels{};
		std::vector<tag>     tags{};

		[[nodiscard]] inline auto width() const noexcept -> int32_t
		{
			return this->right - this->left;
		}

		[[nodiscard]] inline auto height() const noexcept -> int32_t
		{
			return this->bottom - this->top;
		}

		[[nodiscard]] inline auto empty() const noexcept -> bool
		{
			return this->width() <= 0 || this->height() <= 0;
		}

		[[nodiscard]] inline auto has_pixels() const noexcept -> bool
		{
			const bool usable{ !this->empty() };

			if (!usable)
			{
				return false;
			}

			const auto width{ static_cast<size_t>(this->width()) };
			const auto height{ static_cast<size_t>(this->height()) };
			const size_t expected{ width * height * 4 };

			return this->pixels.size() == expected;
		}

		[[nodiscard]] inline auto display_name() const noexcept -> const std::string&
		{
			const bool has_unicode{ !this->unicode_name.empty() };

			if (has_unicode)
			{
				return this->unicode_name;
			}

			return this->name;
		}
	};

	struct document
	{
		uint32_t             width{};
		uint32_t             height{};
		std::vector<layer>   layers{};
		std::vector<uint8_t> raw_pimg{};
		uint64_t             raw_stamp{};
	};

	extern [[nodiscard]] auto  read(std::span<const uint8_t> bytes) -> document;
	extern [[nodiscard]] auto write(const document& source) -> std::vector<uint8_t>;
}
