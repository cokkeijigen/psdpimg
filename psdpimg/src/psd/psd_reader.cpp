#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <xstr.hpp>
#include <psd_view.hpp>
#include <psd_document.hpp>
#include <psd_origin.hpp>
#include <psd_rle.hpp>

namespace psdpimg::psd
{
	namespace
	{
		constexpr uint8_t  signature_tag[4]   { '8', 'B', 'I', 'M' };
		constexpr uint8_t  signature_tag_64[4]{ '8', 'B', '6', '4' };
		constexpr uint16_t compression_raw    { 0 };
		constexpr uint16_t compression_rle    { 1 };
		constexpr uint8_t  flag_hidden        { 0x02 };
		constexpr uint8_t  opaque             { 255 };
		constexpr int16_t  alpha_channel      { -1 };
		constexpr uint32_t resource_origin    { 4000 };

		struct channel_info
		{
			int16_t  id{};
			uint32_t length{};
		};

		struct layer_record
		{
			psdpimg::psd::layer       layer{};
			std::vector<channel_info> channels{};
		};

		static inline auto apply_tag(psdpimg::psd::layer& target, const psdpimg::psd::tag& item) -> void
		{
			const bool short_payload{ item.data.size() < 4 };

			if (short_payload)
			{
				return;
			}

			const uint8_t* const bytes{ item.data.data() };
			const uint32_t    number{ std::byteswap(*reinterpret_cast<const uint32_t*>(bytes)) };

			if (item.key == "lyid")
			{
				target.layer_id     = number;
				target.has_layer_id = true;

				return;
			}

			if (item.key == "lsct")
			{
				const bool known{ number <= static_cast<uint32_t>(psdpimg::psd::section_t::divider) };

				const psdpimg::psd::section_t resolved
				{
					known ?
					static_cast<psdpimg::psd::section_t>(number) : psdpimg::psd::section_t::layer
				};

				target.section = resolved;

				return;
			}

			if (item.key == "luni")
			{
				const size_t available { (item.data.size() - 4) / 2 };
				const auto   count     { static_cast<size_t>(number) };
				const size_t limit     { std::min(count, available) };

				std::vector<uint16_t> units{};
				units.reserve(limit);

				for (size_t i{}; i < limit; ++i)
				{
					const size_t offset{ 4 + i * 2 };

					const auto high { static_cast<uint16_t>(item.data[offset]) << 8 };
					const auto unit { static_cast<uint16_t>(high | item.data[offset + 1]) };

					units.push_back(unit);
				}

				std::wstring wide{};

				for (const auto unit : units)
				{
					wide.push_back(static_cast<wchar_t>(unit));
				}

				target.unicode_name = xstr::cvt::to_utf8(wide);
			}
		}

		static auto read_extra(psdpimg::psd::layer& target, const uint8_t*& cursor, const uint8_t* const extra_end) -> void
		{
			const uint32_t global_mask { std::byteswap(*reinterpret_cast<const uint32_t*>(cursor)) };
			const uint32_t first_block { std::byteswap(*reinterpret_cast<const uint32_t*>(cursor + 4)) };

			cursor += 8 + global_mask + first_block;

			std::string_view name{};
			{
				const uint8_t* const begin { cursor };
				const uint8_t        length{ *cursor };
				size_t               used  { static_cast<size_t>(length) + 1 };

				{
					const size_t remainder{ used % 4 };

					used = { remainder == 0 ? used : used + 4 - remainder };
				}

				++cursor;

				const char* const text{ reinterpret_cast<const char*>(cursor) };

				cursor = begin + used;
				name   = { text, length };
			}

			target.name         = name;
			target.unicode_name = name;

			while (cursor + 12 <= extra_end)
			{
				const bool known_tag
				{
					std::memcmp(cursor, signature_tag, 4) == 0 ||
					std::memcmp(cursor, signature_tag_64, 4) == 0
				};

				if (!known_tag)
				{
					const uint32_t unknown_length{ std::byteswap(*reinterpret_cast<const uint32_t*>(cursor)) };

					cursor += 4 + unknown_length;

					continue;
				}

				const char* const    key_chars{ reinterpret_cast<const char*>(cursor + 4) };
				const uint32_t       length   { std::byteswap(*reinterpret_cast<const uint32_t*>(cursor + 8)) };
				const uint8_t* const payload  { cursor + 12 };

				cursor += 12 + length;

				if (length % 2 != 0)
				{
					++cursor;
				}

				psdpimg::psd::tag item
				{
					.key { key_chars, 4 },
					.data{ payload, payload + length }
				};

				psdpimg::psd::apply_tag(target, item);
				target.tags.push_back(std::move(item));
			}

			cursor = extra_end;
		}

		static inline auto strip_placeholder(psdpimg::psd::layer& target) noexcept -> void
		{
			constexpr std::string_view marker{ "#lyid#-1" };

			std::string_view name{ target.unicode_name };

			if (!name.ends_with(marker))
			{
				return;
			}

			target.placeholder = true;
			target.unicode_name.erase(target.unicode_name.size() - marker.size());
			target.name = target.unicode_name;
		}

		static auto read_record(const uint8_t*& cursor, layer_record& record) -> void
		{
			psdpimg::psd::layer& target{ record.layer };

			target.top    = std::byteswap(*reinterpret_cast<const int32_t*>(cursor));
			target.left   = std::byteswap(*reinterpret_cast<const int32_t*>(cursor + 4));
			target.bottom = std::byteswap(*reinterpret_cast<const int32_t*>(cursor + 8));
			target.right  = std::byteswap(*reinterpret_cast<const int32_t*>(cursor + 12));

			const uint16_t channel_count{ std::byteswap(*reinterpret_cast<const uint16_t*>(cursor + 16)) };

			cursor += 18;

			record.channels.reserve(channel_count);

			for (uint16_t i{}; i < channel_count; ++i)
			{
				const channel_info info
				{
					.id     = std::byteswap(*reinterpret_cast<const int16_t*>(cursor)),
					.length = std::byteswap(*reinterpret_cast<const uint32_t*>(cursor + 2))
				};

				cursor += 6;

				record.channels.push_back(info);
			}

			cursor += 8;

			const uint8_t opacity { cursor[0] };
			const uint8_t clipping{ cursor[1] };
			const uint8_t flags   { cursor[2] };

			cursor += 4;

			target.opacity  = opacity;
			target.clipping = clipping;
			target.visible  = (flags & flag_hidden) == 0;

			const uint32_t extra_length{ std::byteswap(*reinterpret_cast<const uint32_t*>(cursor)) };

			cursor += 4;

			psdpimg::psd::read_extra(target, cursor, cursor + extra_length);
			psdpimg::psd::strip_placeholder(target);
		}

		static inline auto channel_slot(const int16_t id) noexcept -> int32_t
		{
			if (id == alpha_channel)
			{
				return { 3 };
			}

			const bool is_color{ id >= 0 && id < 3 };

			return int32_t
			{
				is_color ? id : -1
			};
		}

		static auto read_plane(const uint8_t*& cursor, const uint32_t width, const uint32_t height, std::vector<uint8_t>& target) -> void
		{
			const uint16_t compression{ std::byteswap(*reinterpret_cast<const uint16_t*>(cursor)) };

			cursor += 2;

			if (compression == compression_raw)
			{
				const size_t size{ static_cast<size_t>(width) * height };

				target.assign(cursor, cursor + size);

				cursor += size;

				return;
			}

			if (compression != compression_rle)
			{
				throw std::runtime_error{ "psd::read_plane: unsupported compression" };
			}

			std::vector<uint16_t> counts{};
			counts.reserve(height);

			for (uint32_t y{}; y < height; ++y)
			{
				counts.push_back(std::byteswap(*reinterpret_cast<const uint16_t*>(cursor)));

				cursor += 2;
			}

			target.assign(static_cast<size_t>(width) * height, 0);

			for (uint32_t y{}; y < height; ++y)
			{
				const std::span<const uint8_t> packed{ cursor, counts[y] };
				const std::vector<uint8_t>     row   { psdpimg::psd::unpack_bits(packed, width) };
				const auto                     column{ static_cast<size_t>(y) * width };
				const std::vector<uint8_t>::iterator destination
				{
					target.begin() + static_cast<ptrdiff_t>(column)
				};

				cursor += counts[y];

				std::ranges::copy(row, destination);
			}
		}

		static auto read_layer_data(const uint8_t*& cursor, layer_record& record) -> void
		{
			const auto   width      { static_cast<uint32_t>(std::max<int32_t>(record.layer.width(), 0)) };
			const auto   height     { static_cast<uint32_t>(std::max<int32_t>(record.layer.height(), 0)) };
			const bool   drawable   { width != 0 && height != 0 };
			const size_t plane_size { static_cast<size_t>(width) * height };

			if (drawable)
			{
				record.layer.pixels.assign(plane_size * 4, 0);

				for (size_t i{}; i < plane_size; ++i)
				{
					record.layer.pixels[i * 4 + 3] = opaque;
				}
			}

			std::vector<uint8_t> plane{};

			for (const auto& info : record.channels)
			{
				const uint8_t* const begin { cursor };
				const bool           usable{ drawable && info.length > 2 };

				if (usable)
				{
					psdpimg::psd::read_plane(cursor, width, height, plane);

					const int32_t slot{ psdpimg::psd::channel_slot(info.id) };

					if (slot >= 0 && plane.size() == plane_size)
					{
						for (size_t i{}; i < plane_size; ++i)
						{
							record.layer.pixels[i * 4 + static_cast<size_t>(slot)] = plane[i];
						}
					}
				}

				cursor = begin + info.length;
			}
		}

		static auto read_resources(const uint8_t*& cursor, document& target, const uint8_t* const end) -> void
		{
			while (cursor + 12 <= end)
			{
				const bool known{ std::memcmp(cursor, signature_tag, 4) == 0 };

				if (!known)
				{
					break;
				}

				const uint16_t id         { std::byteswap(*reinterpret_cast<const uint16_t*>(cursor + 4)) };
				const uint8_t  name_length{ cursor[6] };

				size_t name_used{ static_cast<size_t>(name_length) + 1 };
				{
					const size_t remainder{ name_used % 2 };

					name_used = { remainder == 0 ? name_used : name_used + 2 - remainder };
				}

				cursor += 7 + name_used - 1;

				const size_t size{ std::byteswap(*reinterpret_cast<const uint32_t*>(cursor)) };

				cursor += 4;

				if (id == resource_origin)
				{
					const psdpimg::psd::origin packed
					{
						psdpimg::psd::unpack_origin(std::span<const uint8_t>{ cursor, size })
					};

					target.raw_pimg  = packed.bytes;
					target.raw_stamp = packed.stamp;
				}

				cursor += size;

				if (size % 2 != 0)
				{
					++cursor;
				}
			}

			cursor = end;
		}

		static inline auto read_layer_info(const uint8_t*& cursor, document& target, const uint8_t* const section_end) -> void
		{
			if (cursor + 2 > section_end)
			{
				return;
			}

			const int16_t raw_count{ std::byteswap(*reinterpret_cast<const int16_t*>(cursor)) };

			cursor += 2;

			const int32_t magnitude
			{
				raw_count < 0 ?
				-static_cast<int32_t>(raw_count) : raw_count
			};

			const auto count{ static_cast<size_t>(magnitude) };

			std::vector<layer_record> records{};
			records.resize(count);

			for (auto& record : records)
			{
				psdpimg::psd::read_record(cursor, record);
			}

			for (auto& record : records)
			{
				psdpimg::psd::read_layer_data(cursor, record);
			}

			for (auto& record : records)
			{
				target.layers.push_back(std::move(record.layer));
			}
		}
	}

	auto read(const std::span<const uint8_t> bytes) -> document
	{
		const psdpimg::psd::view source{ bytes };

		if (!source.is_valid())
		{
			throw std::runtime_error{ "psd::read: not a PSD document" };
		}

		const psdpimg::psd::header* const info   { source.header() };
		const uint16_t                    version{ std::byteswap(info->version) };

		if (version != 1)
		{
			throw std::runtime_error{ "psd::read: only version 1 is supported" };
		}

		const uint16_t depth{ std::byteswap(info->depth) };

		if (depth != 8)
		{
			throw std::runtime_error{ "psd::read: only 8 bit documents are supported" };
		}

		document result{};

		result.height = std::byteswap(info->height);
		result.width  = std::byteswap(info->width);

		const std::span<const uint8_t> resources{ source.resources() };

		const uint8_t*       cursor      { resources.data() };
		const uint8_t* const resource_end{ resources.data() + resources.size() };

		psdpimg::psd::read_resources(cursor, result, resource_end);

		const std::span<const uint8_t> block{ source.layer_block() };
		const bool has_layer_info{ block.size() >= 4 };

		if (has_layer_info)
		{
			const uint32_t layer_length{ std::byteswap(*reinterpret_cast<const uint32_t*>(block.data())) };

			const uint8_t*       layer_cursor{ block.data() + 4 };
			const uint8_t* const layer_end   { layer_cursor + layer_length };

			psdpimg::psd::read_layer_info(layer_cursor, result, layer_end);
		}

		return result;
	}
}
