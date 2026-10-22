#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "common/byte_writer.hpp"
#include "common/image.hpp"
#include "common/unicode.hpp"
#include "psd/psd_document.hpp"
#include "psd/psd_metadata.hpp"
#include "psd/psd_origin.hpp"
#include "psd/psd_rle.hpp"

namespace psdpimg::psd
{
	namespace
	{
		constexpr uint8_t  signature_psd[4]{ '8', 'B', 'P', 'S' };
		constexpr uint8_t  signature_tag[4]{ '8', 'B', 'I', 'M' };
		constexpr uint8_t  flag_hidden{ 0x02 };
		constexpr uint16_t compression_rle{ 1 };
		constexpr uint32_t resource_resolution{ 1005 };
		constexpr uint32_t resource_version{ 1057 };
		constexpr uint32_t resource_xmp{ 1060 };
		constexpr uint32_t resource_origin{ 4000 };
		constexpr int16_t  alpha_channel{ -1 };
		constexpr size_t   channel_count{ 4 };

		static auto align_to(const size_t value, const size_t multiple) noexcept -> size_t
		{
			const size_t remainder{ value % multiple };

			return remainder == 0 ? value : value + multiple - remainder;
		}

		static auto write_pascal(psdpimg::common::byte_writer& writer, const std::string_view text) -> void
		{
			const size_t length{ std::min<size_t>(text.size(), 255) };
			const auto chars{ reinterpret_cast<const uint8_t*>(text.data()) };
			const std::span<const uint8_t> bytes{ chars, length };

			writer.write<uint8_t>(static_cast<uint8_t>(length));
			writer.write_span(bytes);

			const size_t used{ psdpimg::psd::align_to(length + 1, 4) };

			writer.write_zeros(used - length - 1);
		}

		static auto write_unicode(psdpimg::common::byte_writer& writer, const std::string_view text) -> size_t
		{
			const auto units{ psdpimg::common::utf8_to_utf16(text) };
			const size_t payload{ 4 + units.size() * 2 };
			const size_t declared{ psdpimg::psd::align_to(payload, 4) };

			writer.write<uint32_t>(static_cast<uint32_t>(units.size()), std::endian::big);

			for (const auto unit : units)
			{
				writer.write<uint16_t>(unit, std::endian::big);
			}

			writer.write_zeros(declared - payload);

			return declared;
		}

		static auto write_resource(psdpimg::common::byte_writer& writer, const uint32_t id,
			const std::string_view name, const std::span<const uint8_t> payload) -> void
		{
			const size_t name_length{ std::min<size_t>(name.size(), 255) };
			const size_t name_used{ psdpimg::psd::align_to(name_length + 1, 2) };
			const auto name_chars{ reinterpret_cast<const uint8_t*>(name.data()) };
			const std::span<const uint8_t> name_bytes{ name_chars, name_length };

			writer.write_span(signature_tag);
			writer.write<uint16_t>(static_cast<uint16_t>(id), std::endian::big);
			writer.write<uint8_t>(static_cast<uint8_t>(name_length));
			writer.write_span(name_bytes);
			writer.write_zeros(name_used - name_length - 1);
			writer.write<uint32_t>(static_cast<uint32_t>(payload.size()), std::endian::big);
			writer.write_span(payload);

			if (payload.size() % 2 != 0)
			{
				writer.write_zeros(1);
			}
		}

		static auto build_resources(const document& source) -> std::vector<uint8_t>
		{
			psdpimg::common::byte_writer writer{};
			const auto xmp{ psdpimg::psd::xmp_packet() };
			const auto xmp_chars{ reinterpret_cast<const uint8_t*>(xmp.data()) };
			const std::span<const uint8_t> xmp_bytes{ xmp_chars, xmp.size() };

			psdpimg::psd::write_resource(writer, resource_resolution, {}, psdpimg::psd::resolution_info());
			psdpimg::psd::write_resource(writer, resource_xmp, psdpimg::psd::xmp_name(), xmp_bytes);
			psdpimg::psd::write_resource(writer, resource_version, {}, psdpimg::psd::version_info());

			if (!source.raw_pimg.empty())
			{
				const psdpimg::psd::origin packed{ source.raw_pimg, source.raw_stamp };
				const auto payload{ psdpimg::psd::pack_origin(packed) };

				psdpimg::psd::write_resource(writer, resource_origin, {}, payload);
			}

			return writer.take();
		}

		static auto encode_plane(const std::span<const uint8_t> rgba, const uint32_t width,
			const uint32_t height, const size_t channel) -> std::vector<uint8_t>
		{
			std::vector<std::vector<uint8_t>> rows{};
			rows.reserve(height);

			std::vector<uint8_t> plane{};
			plane.reserve(width);

			for (uint32_t y{}; y < height; ++y)
			{
				plane.clear();

				for (uint32_t x{}; x < width; ++x)
				{
					const auto offset{ (static_cast<size_t>(y) * width + x) * 4 + channel };

					plane.push_back(rgba[offset]);
				}

				rows.push_back(psdpimg::psd::pack_bits(plane));
			}

			psdpimg::common::byte_writer writer{};

			writer.write<uint16_t>(compression_rle, std::endian::big);

			for (const auto& row : rows)
			{
				writer.write<uint16_t>(static_cast<uint16_t>(row.size()), std::endian::big);
			}

			for (const auto& row : rows)
			{
				writer.write_span(row);
			}

			return writer.take();
		}

		static auto channel_identifier(const size_t index) noexcept -> int16_t
		{
			if (index == 3)
			{
				return alpha_channel;
			}

			return static_cast<int16_t>(index);
		}

		static auto write_tag(psdpimg::common::byte_writer& writer, const std::string_view key) -> void
		{
			const auto chars{ reinterpret_cast<const uint8_t*>(key.data()) };
			const std::span<const uint8_t> bytes{ chars, 4 };

			writer.write_span(signature_tag);
			writer.write_span(bytes);
		}

		static auto build_record(const layer& source, const std::vector<size_t>& channel_sizes)
			-> std::vector<uint8_t>
		{
			psdpimg::common::byte_writer writer{};

			writer.write<int32_t>(source.top, std::endian::big);
			writer.write<int32_t>(source.left, std::endian::big);
			writer.write<int32_t>(source.bottom, std::endian::big);
			writer.write<int32_t>(source.right, std::endian::big);

			writer.write<uint16_t>(static_cast<uint16_t>(channel_sizes.size()), std::endian::big);

			for (size_t i{}; i < channel_sizes.size(); ++i)
			{
				writer.write<int16_t>(psdpimg::psd::channel_identifier(i), std::endian::big);
				writer.write<uint32_t>(static_cast<uint32_t>(channel_sizes[i]), std::endian::big);
			}

			psdpimg::psd::write_tag(writer, "norm");

			const auto flags{ source.visible ? uint8_t{ 0 } : flag_hidden };

			writer.write<uint8_t>(source.opacity);
			writer.write<uint8_t>(source.clipping);
			writer.write<uint8_t>(flags);
			writer.write<uint8_t>(0);

			psdpimg::common::byte_writer extra{};

			extra.write<uint32_t>(0, std::endian::big);
			extra.write<uint32_t>(0, std::endian::big);
			psdpimg::psd::write_pascal(extra, source.display_name());

			const bool is_group{ source.section != psdpimg::psd::section_t::layer };

			if (is_group)
			{
				psdpimg::psd::write_tag(extra, "lsct");
				extra.write<uint32_t>(12, std::endian::big);
				extra.write<uint32_t>(static_cast<uint32_t>(source.section), std::endian::big);
				psdpimg::psd::write_tag(extra, "pass");
			}

			if (source.has_layer_id && source.layer_id <= 65535)
			{
				psdpimg::psd::write_tag(extra, "lyid");
				extra.write<uint32_t>(4, std::endian::big);
				extra.write<uint32_t>(source.layer_id, std::endian::big);
			}

			psdpimg::psd::write_tag(extra, "luni");

			psdpimg::common::byte_writer unicode{};
			const size_t unicode_size{ psdpimg::psd::write_unicode(unicode, source.display_name()) };

			extra.write<uint32_t>(static_cast<uint32_t>(unicode_size), std::endian::big);
			extra.write_span(unicode.data());

			const auto payload{ extra.take() };

			writer.write<uint32_t>(static_cast<uint32_t>(payload.size()), std::endian::big);
			writer.write_span(payload);

			return writer.take();
		}

		static auto build_merged(const document& source) -> std::vector<uint8_t>
		{
			std::vector<psdpimg::common::composite_layer> layers{};
			std::vector<size_t>                           stack{};

			for (size_t i{ source.layers.size() }; i-- > 0;)
			{
				const auto& item{ source.layers[i] };
				const bool  is_divider{ item.section == psdpimg::psd::section_t::divider };
				const bool is_folder
				{
					item.section == psdpimg::psd::section_t::open_folder ||
					item.section == psdpimg::psd::section_t::closed_folder
				};

				if (is_divider)
				{
					if (!stack.empty())
					{
						stack.pop_back();
					}

					continue;
				}

				if (is_folder)
				{
					stack.push_back(i);

					continue;
				}

				if (!item.has_pixels())
				{
					continue;
				}

				uint32_t opacity{ item.opacity };
				bool     visible{ item.visible };

				for (const auto parent : stack)
				{
					const auto& group{ source.layers[parent] };

					opacity = opacity * group.opacity / 255;
					visible = visible && group.visible;
				}

				if (!visible || opacity == 0)
				{
					continue;
				}

				const psdpimg::common::composite_layer entry
				{
					.pixels = item.pixels,
					.width = static_cast<uint32_t>(item.width()),
					.height = static_cast<uint32_t>(item.height()),
					.left = item.left,
					.top = item.top,
					.opacity = static_cast<uint8_t>(opacity)
				};

				layers.push_back(entry);
			}

			std::ranges::reverse(layers);

			auto merged{ psdpimg::common::composite(layers, source.width, source.height) };

			for (size_t i{}; i < merged.pixels.size(); i += 4)
			{
				if (merged.pixels[i + 3] != 0)
				{
					continue;
				}

				merged.pixels[i + 0] = 255;
				merged.pixels[i + 1] = 255;
				merged.pixels[i + 2] = 255;
			}

			std::vector<std::vector<uint8_t>> rows{};
			rows.reserve(static_cast<size_t>(source.height) * channel_count);

			std::vector<uint8_t> plane{};
			plane.reserve(source.width);

			for (size_t channel{}; channel < channel_count; ++channel)
			{
				for (uint32_t y{}; y < source.height; ++y)
				{
					plane.clear();

					for (uint32_t x{}; x < source.width; ++x)
					{
						const auto offset{ (static_cast<size_t>(y) * source.width + x) * 4 + channel };

						plane.push_back(merged.pixels[offset]);
					}

					rows.push_back(psdpimg::psd::pack_bits(plane));
				}
			}

			psdpimg::common::byte_writer writer{};

			writer.write<uint16_t>(compression_rle, std::endian::big);

			for (const auto& row : rows)
			{
				writer.write<uint16_t>(static_cast<uint16_t>(row.size()), std::endian::big);
			}

			for (const auto& row : rows)
			{
				writer.write_span(row);
			}

			return writer.take();
		}
	}

	auto write(const document& source) -> std::vector<uint8_t>
	{
		std::vector<std::vector<std::vector<uint8_t>>> payloads{};
		payloads.reserve(source.layers.size());

		for (const auto& item : source.layers)
		{
			std::vector<std::vector<uint8_t>> planes{};

			if (item.has_pixels())
			{
				const auto width{ static_cast<uint32_t>(item.width()) };
				const auto height{ static_cast<uint32_t>(item.height()) };

				for (size_t channel{}; channel < channel_count; ++channel)
				{
					planes.push_back(psdpimg::psd::encode_plane(item.pixels, width, height, channel));
				}
			}

			payloads.push_back(std::move(planes));
		}

		psdpimg::common::byte_writer layer_info{};

		layer_info.write<int16_t>(static_cast<int16_t>(source.layers.size()), std::endian::big);

		for (size_t i{}; i < source.layers.size(); ++i)
		{
			std::vector<size_t> sizes{};
			sizes.reserve(payloads[i].size());

			for (const auto& plane : payloads[i])
			{
				sizes.push_back(plane.size());
			}

			layer_info.write_span(psdpimg::psd::build_record(source.layers[i], sizes));
		}

		for (const auto& planes : payloads)
		{
			for (const auto& plane : planes)
			{
				layer_info.write_span(plane);
			}
		}

		if (layer_info.size() % 2 != 0)
		{
			layer_info.write_zeros(1);
		}

		const auto resources{ psdpimg::psd::build_resources(source) };
		const auto merged{ psdpimg::psd::build_merged(source) };
		const auto& layer_block{ layer_info.data() };

		psdpimg::common::byte_writer writer{};

		writer.write_span(signature_psd);
		writer.write<uint16_t>(1, std::endian::big);
		writer.write_zeros(6);
		writer.write<uint16_t>(static_cast<uint16_t>(channel_count), std::endian::big);
		writer.write<uint32_t>(source.height, std::endian::big);
		writer.write<uint32_t>(source.width, std::endian::big);
		writer.write<uint16_t>(8, std::endian::big);
		writer.write<uint16_t>(3, std::endian::big);
		writer.write<uint32_t>(0, std::endian::big);

		writer.write<uint32_t>(static_cast<uint32_t>(resources.size()), std::endian::big);
		writer.write_span(resources);

		writer.write<uint32_t>(static_cast<uint32_t>(layer_block.size() + 8), std::endian::big);
		writer.write<uint32_t>(static_cast<uint32_t>(layer_block.size()), std::endian::big);
		writer.write_span(layer_block);
		writer.write<uint32_t>(0, std::endian::big);

		writer.write_span(merged);

		return writer.take();
	}
}
