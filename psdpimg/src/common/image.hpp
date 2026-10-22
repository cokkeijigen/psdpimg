#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psdpimg::common
{
	struct image
	{
		uint32_t             width{};
		uint32_t             height{};
		std::vector<uint8_t> pixels{};

		[[nodiscard]] inline auto empty() const noexcept -> bool
		{
			return this->width == 0 || this->height == 0;
		}

		[[nodiscard]] inline auto allocated() const noexcept -> bool
		{
			const size_t expected{ static_cast<size_t>(this->width) * this->height * 4 };

			return expected != 0 && this->pixels.size() == expected;
		}

		[[nodiscard]] inline auto offset(const uint32_t x, const uint32_t y) const noexcept -> size_t
		{
			return (static_cast<size_t>(y) * this->width + x) * 4;
		}

		[[nodiscard]] inline auto span() const noexcept -> std::span<const uint8_t>
		{
			return this->pixels;
		}

		[[nodiscard]] inline auto span() noexcept -> std::span<uint8_t>
		{
			return this->pixels;
		}

		inline auto resize(const uint32_t _width, const uint32_t _height) -> void
		{
			this->width  = _width;
			this->height = _height;
			this->pixels.assign(static_cast<size_t>(_width) * _height * 4, 0);
		}

		inline auto fill(const uint8_t red, const uint8_t green, const uint8_t blue, const uint8_t alpha) noexcept -> void
		{
			for (size_t i{}; i + 3 < this->pixels.size(); i += 4)
			{
				this->pixels[i + 0] = red;
				this->pixels[i + 1] = green;
				this->pixels[i + 2] = blue;
				this->pixels[i + 3] = alpha;
			}
		}
	};

	struct composite_layer
	{
		std::span<const uint8_t> pixels{};
		uint32_t                 width{};
		uint32_t                 height{};
		int32_t                  left{};
		int32_t                  top{};
		uint8_t                  opacity{ 255 };
	};

	extern auto composite(std::span<const composite_layer> layers, uint32_t width, uint32_t height) -> image;
	extern auto downscale(const image& source, uint32_t max_side) -> image;
}
