#include <algorithm>
#include <limits>

#include "common/image.hpp"

namespace psdpimg::common
{
	auto composite(std::span<const composite_layer> layers, const uint32_t width, const uint32_t height) -> image
	{
		image result{};
		result.resize(width, height);

		for (const auto& layer : layers)
		{
			const bool usable
			{
				!layer.pixels.empty() &&
				layer.width != 0 &&
				layer.height != 0 &&
				layer.opacity != 0
			};

			if (!usable)
			{
				continue;
			}

			const size_t row_bytes{ static_cast<size_t>(layer.width) * 4 };

			for (uint32_t y{}; y < layer.height; ++y)
			{
				const int64_t target_y{ static_cast<int64_t>(layer.top) + y };

				if (target_y < 0 || target_y >= height)
				{
					continue;
				}

				const size_t row_offset{ static_cast<size_t>(y) * row_bytes };

				for (uint32_t x{}; x < layer.width; ++x)
				{
					const int64_t target_x{ static_cast<int64_t>(layer.left) + x };

					if (target_x < 0 || target_x >= width)
					{
						continue;
					}

					const size_t source_offset{ row_offset + static_cast<size_t>(x) * 4 };
					const uint32_t source_alpha{ layer.pixels[source_offset + 3] };

					if (source_alpha == 0)
					{
						continue;
					}

					const uint32_t alpha{ source_alpha * layer.opacity / 255 };
					const auto target_x_value{ static_cast<uint32_t>(target_x) };
					const auto target_y_value{ static_cast<uint32_t>(target_y) };
					const auto target_offset{ result.offset(target_x_value, target_y_value) };

					const uint32_t destination_alpha{ result.pixels[target_offset + 3] };

					if (alpha == 255 || destination_alpha == 0)
					{
						result.pixels[target_offset + 0] = layer.pixels[source_offset + 0];
						result.pixels[target_offset + 1] = layer.pixels[source_offset + 1];
						result.pixels[target_offset + 2] = layer.pixels[source_offset + 2];
						result.pixels[target_offset + 3] = static_cast<uint8_t>(alpha);

						continue;
					}

					const uint32_t inverse{ 255 - alpha };
					const uint32_t numerator_alpha{ alpha * 255 + destination_alpha * inverse };
					const uint32_t out_alpha{ numerator_alpha / 255 };

					if (out_alpha == 0)
					{
						continue;
					}

					for (size_t channel{}; channel < 3; ++channel)
					{
						const uint32_t source{ layer.pixels[source_offset + channel] };
						const uint32_t destination{ result.pixels[target_offset + channel] };
						const uint32_t weighted{ source * alpha * 255 };
						const uint32_t under{ destination * destination_alpha * inverse };
						const uint32_t value{ (weighted + under) / numerator_alpha };
						const uint32_t clamped{ std::min<uint32_t>(value, 255) };

						result.pixels[target_offset + channel] = static_cast<uint8_t>(clamped);
					}

					const uint32_t alpha_clamped{ std::min<uint32_t>(out_alpha, 255) };

					result.pixels[target_offset + 3] = static_cast<uint8_t>(alpha_clamped);
				}
			}
		}

		return result;
	}

	auto downscale(const image& source, const uint32_t max_side) -> image
	{
		if (!source.allocated() || max_side == 0)
		{
			return image{};
		}

		const uint32_t longest{ std::max(source.width, source.height) };

		if (longest <= max_side)
		{
			return source;
		}

		const double ratio{ static_cast<double>(max_side) / static_cast<double>(longest) };
		const auto scaled_width{ static_cast<uint32_t>(source.width * ratio) };
		const auto scaled_height{ static_cast<uint32_t>(source.height * ratio) };
		const uint32_t width{ std::max<uint32_t>(1, scaled_width) };
		const uint32_t height{ std::max<uint32_t>(1, scaled_height) };

		image result{};
		result.resize(width, height);

		for (uint32_t y{}; y < height; ++y)
		{
			const auto row_begin{ static_cast<uint64_t>(y) * source.height / height };
			const auto row_end{ static_cast<uint64_t>(y + 1) * source.height / height };
			const auto source_y_begin{ static_cast<uint32_t>(row_begin) };
			const auto source_y_end{ static_cast<uint32_t>(row_end) };

			for (uint32_t x{}; x < width; ++x)
			{
				const auto column_begin{ static_cast<uint64_t>(x) * source.width / width };
				const auto column_end{ static_cast<uint64_t>(x + 1) * source.width / width };
				const auto source_x_begin{ static_cast<uint32_t>(column_begin) };
				const auto source_x_end{ static_cast<uint32_t>(column_end) };

				uint64_t sums[4]{};
				uint64_t count{};

				for (uint32_t sy{ source_y_begin }; sy < std::max(source_y_end, source_y_begin + 1); ++sy)
				{
					for (uint32_t sx{ source_x_begin }; sx < std::max(source_x_end, source_x_begin + 1); ++sx)
					{
						const auto clamp_x{ std::min(sx, source.width - 1) };
						const auto clamp_y{ std::min(sy, source.height - 1) };
						const auto offset{ source.offset(clamp_x, clamp_y) };

						for (size_t channel{}; channel < 4; ++channel)
						{
							sums[channel] += source.pixels[offset + channel];
						}

						++count;
					}
				}

				const auto target{ result.offset(x, y) };
				const auto divisor{ std::max<uint64_t>(count, 1) };

				for (size_t channel{}; channel < 4; ++channel)
				{
					result.pixels[target + channel] = static_cast<uint8_t>(sums[channel] / divisor);
				}
			}
		}

		return result;
	}
}
