#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <xmem.hpp>
#include <image.hpp>
#include <xstr.hpp>
#include <psd_view.hpp>
#include <psd_document.hpp>
#include <psd_metadata.hpp>
#include <psd_origin.hpp>
#include <psd_rle.hpp>

namespace psdpimg::psd
{
	namespace
	{
		constexpr uint8_t  signature_tag[4]   { '8', 'B', 'I', 'M' };
		constexpr uint8_t  flag_hidden        { 0x02 };
		constexpr uint16_t compression_rle    { 1 };
		constexpr uint32_t resource_resolution{ 1005 };
		constexpr uint32_t resource_version   { 1057 };
		constexpr uint32_t resource_xmp       { 1060 };
		constexpr uint32_t resource_origin    { 4000 };
		constexpr int16_t  alpha_channel      { -1 };
		constexpr size_t   channel_count      { 4 };

		static auto write_pascal(xmem::buffer<uint8_t>& writer, const std::string_view text) -> void
		{
			const size_t                   length { std::min<size_t>(text.size(), 255) };
			const auto                     chars  { reinterpret_cast<const uint8_t*>(text.data()) };
			const std::span<const uint8_t> bytes  { chars, length };

			writer.write(static_cast<uint8_t>(length));
			writer.write(bytes);

			size_t used{ length + 1 };
			{
				const size_t remainder{ used % 4 };

				used = { remainder == 0 ? used : used + 4 - remainder };
			}

			writer.memset(writer.count(), uint8_t{}, used - length - 1);
		}

		static auto write_unicode(xmem::buffer<uint8_t>& writer, const std::string_view text) -> size_t
		{
			const std::wstring units{ xstr::cvt::to_utf16(text, 65001) };
			const size_t       payload{ 4 + units.size() * 2 };

			size_t declared{ payload };
			{
				const size_t remainder{ declared % 4 };

				declared = { remainder == 0 ? declared : declared + 4 - remainder };
			}

			const uint32_t count{ std::byteswap(static_cast<uint32_t>(units.size())) };

			writer.write(reinterpret_cast<const uint8_t*>(&count), sizeof(count));

			for (const auto unit : units)
			{
				const uint16_t value{ std::byteswap(static_cast<uint16_t>(unit)) };

				writer.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
			}

			writer.memset(writer.count(), uint8_t{}, declared - payload);

			return declared;
		}

		static auto write_resource(xmem::buffer<uint8_t>& writer, const uint32_t id,
			const std::string_view name, const std::span<const uint8_t> payload) -> void
		{
			const size_t                   name_length { std::min<size_t>(name.size(), 255) };
			const auto                     name_chars  { reinterpret_cast<const uint8_t*>(name.data()) };
			const std::span<const uint8_t> name_bytes  { name_chars, name_length };
			const uint16_t                 identifier  { std::byteswap(static_cast<uint16_t>(id)) };
			const uint32_t                 length      { std::byteswap(static_cast<uint32_t>(payload.size())) };

			size_t name_used{ name_length + 1 };
			{
				const size_t remainder{ name_used % 2 };

				name_used = { remainder == 0 ? name_used : name_used + 2 - remainder };
			}

			writer.write(signature_tag);
			writer.write(reinterpret_cast<const uint8_t*>(&identifier), sizeof(identifier));
			writer.write(static_cast<uint8_t>(name_length));
			writer.write(name_bytes);
			writer.memset(writer.count(), uint8_t{}, name_used - name_length - 1);
			writer.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length));
			writer.write(payload);

			if (payload.size() % 2 != 0)
			{
				writer.memset(writer.count(), uint8_t{}, 1);
			}
		}

		static auto build_resources(const psdpimg::psd::document& source) -> std::vector<uint8_t>
		{
			xmem::buffer<uint8_t>          writer    {};
			const std::string_view         xmp      { psdpimg::psd::xmp_packet() };
			const auto                     xmp_chars{ reinterpret_cast<const uint8_t*>(xmp.data()) };
			const std::span<const uint8_t> xmp_bytes{ xmp_chars, xmp.size() };

			const std::span<const uint8_t> resolution  { psdpimg::psd::resolution_info() };
			const std::span<const uint8_t> version_data{ psdpimg::psd::version_info() };
			const std::string_view         name        { psdpimg::psd::xmp_name() };

			psdpimg::psd::write_resource(writer, resource_resolution, {}, resolution);
			psdpimg::psd::write_resource(writer, resource_xmp, name, xmp_bytes);
			psdpimg::psd::write_resource(writer, resource_version, {}, version_data);

			if (!source.raw_pimg.empty())
			{
				const psdpimg::psd::origin packed  { source.raw_pimg, source.raw_stamp };
				const std::vector<uint8_t> payload{ psdpimg::psd::pack_origin(packed) };

				psdpimg::psd::write_resource(writer, resource_origin, {}, payload);
			}

			const std::span<uint8_t> result{ writer.written() };

			return { result.begin(), result.end() };
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

			xmem::buffer<uint8_t> writer{};

			const uint16_t compression{ std::byteswap(compression_rle) };

			writer.write(reinterpret_cast<const uint8_t*>(&compression), sizeof(compression));

			for (const auto& row : rows)
			{
				const uint16_t length{ std::byteswap(static_cast<uint16_t>(row.size())) };

				writer.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length));
			}

			for (const auto& row : rows)
			{
				writer.write(std::span<const uint8_t>{ row });
			}

			const std::span<uint8_t> result{ writer.written() };

			return { result.begin(), result.end() };
		}

		static inline auto channel_identifier(const size_t index) noexcept -> int16_t
		{
			if (index == 3)
			{
				return alpha_channel;
			}

			return static_cast<int16_t>(index);
		}

		static inline auto write_tag(xmem::buffer<uint8_t>& writer, const std::string_view key) -> void
		{
			const auto                     chars { reinterpret_cast<const uint8_t*>(key.data()) };
			const std::span<const uint8_t> bytes { chars, 4 };

			writer.write(signature_tag);
			writer.write(bytes);
		}

		static auto build_record(const psdpimg::psd::layer& source, const std::vector<size_t>& channel_sizes)
			-> std::vector<uint8_t>
		{
			xmem::buffer<uint8_t> writer{};

			const int32_t top   { std::byteswap(source.top) };
			const int32_t left  { std::byteswap(source.left) };
			const int32_t bottom{ std::byteswap(source.bottom) };
			const int32_t right { std::byteswap(source.right) };

			writer.write(reinterpret_cast<const uint8_t*>(&top), sizeof(top));
			writer.write(reinterpret_cast<const uint8_t*>(&left), sizeof(left));
			writer.write(reinterpret_cast<const uint8_t*>(&bottom), sizeof(bottom));
			writer.write(reinterpret_cast<const uint8_t*>(&right), sizeof(right));

			const uint16_t count{ std::byteswap(static_cast<uint16_t>(channel_sizes.size())) };

			writer.write(reinterpret_cast<const uint8_t*>(&count), sizeof(count));

			for (size_t i{}; i < channel_sizes.size(); ++i)
			{
				const int16_t  identifier{ std::byteswap(psdpimg::psd::channel_identifier(i)) };
				const uint32_t capacity  { std::byteswap(static_cast<uint32_t>(channel_sizes[i])) };

				writer.write(reinterpret_cast<const uint8_t*>(&identifier), sizeof(identifier));
				writer.write(reinterpret_cast<const uint8_t*>(&capacity), sizeof(capacity));
			}

			psdpimg::psd::write_tag(writer, "norm");

			const auto flags{ source.visible ? uint8_t{ 0 } : flag_hidden };

			writer.write(static_cast<uint8_t>(source.opacity));
			writer.write(static_cast<uint8_t>(source.clipping));
			writer.write(flags);
			writer.write(uint8_t{ 0 });

			xmem::buffer<uint8_t> extra{};

			extra.memset(extra.count(), uint8_t{}, 8);
			psdpimg::psd::write_pascal(extra, source.display_name());

			const bool is_group{ source.section != psdpimg::psd::section_t::layer };

			if (is_group)
			{
				const uint32_t payload_size{ std::byteswap(uint32_t{ 12 }) };
				const uint32_t kind        { std::byteswap(static_cast<uint32_t>(source.section)) };

				psdpimg::psd::write_tag(extra, "lsct");
				extra.write(reinterpret_cast<const uint8_t*>(&payload_size), sizeof(payload_size));
				extra.write(reinterpret_cast<const uint8_t*>(&kind), sizeof(kind));
				psdpimg::psd::write_tag(extra, "pass");
			}

			if (source.has_layer_id && source.layer_id <= 65535)
			{
				const uint32_t payload_size{ std::byteswap(uint32_t{ 4 }) };
				const uint32_t identifier  { std::byteswap(static_cast<uint32_t>(source.layer_id)) };

				psdpimg::psd::write_tag(extra, "lyid");
				extra.write(reinterpret_cast<const uint8_t*>(&payload_size), sizeof(payload_size));
				extra.write(reinterpret_cast<const uint8_t*>(&identifier), sizeof(identifier));
			}

			psdpimg::psd::write_tag(extra, "luni");

			xmem::buffer<uint8_t> unicode{};
			const size_t          unicode_size{ psdpimg::psd::write_unicode(unicode, source.display_name()) };
			const uint32_t        unicode_length{ std::byteswap(static_cast<uint32_t>(unicode_size)) };

			extra.write(reinterpret_cast<const uint8_t*>(&unicode_length), sizeof(unicode_length));
			extra.write(unicode.written());

			const std::span<uint8_t> payload      { extra.written() };
			const uint32_t           payload_length{ std::byteswap(static_cast<uint32_t>(payload.size())) };

			writer.write(reinterpret_cast<const uint8_t*>(&payload_length), sizeof(payload_length));
			writer.write(payload);

			const std::span<uint8_t> record{ writer.written() };

			return { record.begin(), record.end() };
		}

		static auto build_merged(const psdpimg::psd::document& source) -> std::vector<uint8_t>
		{
			std::vector<psdpimg::common::composite_layer> layers {};
			std::vector<size_t>                           stack  {};

			for (size_t i{ source.layers.size() }; i-- > 0;)
			{
				const auto& item       { source.layers[i] };
				const bool  is_divider { item.section == psdpimg::psd::section_t::divider };
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

				uint32_t opacity { item.opacity };
				bool     visible { item.visible };

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

			psdpimg::common::image merged{ psdpimg::common::composite(layers, source.width, source.height) };

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

			xmem::buffer<uint8_t> writer{};

			const uint16_t compression{ std::byteswap(compression_rle) };

			writer.write(reinterpret_cast<const uint8_t*>(&compression), sizeof(compression));

			for (const auto& row : rows)
			{
				const uint16_t length{ std::byteswap(static_cast<uint16_t>(row.size())) };

				writer.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length));
			}

			for (const auto& row : rows)
			{
				writer.write(std::span<const uint8_t>{ row });
			}

			const std::span<uint8_t> result{ writer.written() };

			return { result.begin(), result.end() };
		}
	}

	auto write(const psdpimg::psd::document& source) -> std::vector<uint8_t>
	{
		std::vector<std::vector<std::vector<uint8_t>>> payloads{};
		payloads.reserve(source.layers.size());

		for (const auto& item : source.layers)
		{
			std::vector<std::vector<uint8_t>> planes{};

			if (item.has_pixels())
			{
				const auto width  { static_cast<uint32_t>(item.width()) };
				const auto height { static_cast<uint32_t>(item.height()) };

				for (size_t channel{}; channel < channel_count; ++channel)
				{
					planes.push_back(psdpimg::psd::encode_plane(item.pixels, width, height, channel));
				}
			}

			payloads.push_back(std::move(planes));
		}

		xmem::buffer<uint8_t> layer_info{};

		const int16_t layer_count{ std::byteswap(static_cast<int16_t>(source.layers.size())) };

		layer_info.write(reinterpret_cast<const uint8_t*>(&layer_count), sizeof(layer_count));

		for (size_t i{}; i < source.layers.size(); ++i)
		{
			std::vector<size_t> sizes{};
			sizes.reserve(payloads[i].size());

			for (const auto& plane : payloads[i])
			{
				sizes.push_back(plane.size());
			}

			layer_info.write(std::span<const uint8_t>{ psdpimg::psd::build_record(source.layers[i], sizes) });
		}

		for (const auto& planes : payloads)
		{
			for (const auto& plane : planes)
			{
				layer_info.write(std::span<const uint8_t>{ plane });
			}
		}

		if (layer_info.count() % 2 != 0)
		{
			layer_info.memset(layer_info.count(), uint8_t{}, 1);
		}

		const std::vector<uint8_t> resources{ psdpimg::psd::build_resources(source) };
		const std::vector<uint8_t> merged   { psdpimg::psd::build_merged(source) };
		const std::span<uint8_t> layer_block { layer_info.written() };

		xmem::buffer<uint8_t> writer{};

		const uint16_t signature_version{ std::byteswap(uint16_t{ 1 }) };
		const uint16_t channels         { std::byteswap(static_cast<uint16_t>(channel_count)) };
		const uint32_t height           { std::byteswap(source.height) };
		const uint32_t width            { std::byteswap(source.width) };
		const uint16_t depth            { std::byteswap(uint16_t{ 8 }) };
		const uint16_t color_mode       { std::byteswap(uint16_t{ 3 }) };
		const uint32_t resources_size   { std::byteswap(static_cast<uint32_t>(resources.size())) };
		const uint32_t block_length     { std::byteswap(static_cast<uint32_t>(layer_block.size() + 8)) };
		const uint32_t block_size       { std::byteswap(static_cast<uint32_t>(layer_block.size())) };

		writer.write(signature_psd);
		writer.write(reinterpret_cast<const uint8_t*>(&signature_version), sizeof(signature_version));
		writer.memset(writer.count(), uint8_t{}, 6);
		writer.write(reinterpret_cast<const uint8_t*>(&channels), sizeof(channels));
		writer.write(reinterpret_cast<const uint8_t*>(&height), sizeof(height));
		writer.write(reinterpret_cast<const uint8_t*>(&width), sizeof(width));
		writer.write(reinterpret_cast<const uint8_t*>(&depth), sizeof(depth));
		writer.write(reinterpret_cast<const uint8_t*>(&color_mode), sizeof(color_mode));
		writer.memset(writer.count(), uint8_t{}, 4);

		writer.write(reinterpret_cast<const uint8_t*>(&resources_size), sizeof(resources_size));
		writer.write(std::span<const uint8_t>{ resources });

		writer.write(reinterpret_cast<const uint8_t*>(&block_length), sizeof(block_length));
		writer.write(reinterpret_cast<const uint8_t*>(&block_size), sizeof(block_size));
		writer.write(layer_block);
		writer.memset(writer.count(), uint8_t{}, 4);

		writer.write(std::span<const uint8_t>{ merged });

		const std::span<uint8_t> result{ writer.written() };

		return { result.begin(), result.end() };
	}
}
