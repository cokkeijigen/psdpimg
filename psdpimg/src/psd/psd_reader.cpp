#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common/byte_reader.hpp"
#include "common/unicode.hpp"
#include "psd/psd_document.hpp"
#include "psd/psd_origin.hpp"
#include "psd/psd_rle.hpp"

namespace psdpimg::psd
{
	namespace
	{
		constexpr uint8_t  signature_psd[4]{ '8', 'B', 'P', 'S' };
		constexpr uint8_t  signature_tag[4]{ '8', 'B', 'I', 'M' };
		constexpr uint8_t  signature_tag_64[4]{ '8', 'B', '6', '4' };
		constexpr uint16_t compression_raw{ 0 };
		constexpr uint16_t compression_rle{ 1 };
		constexpr uint8_t  flag_hidden{ 0x02 };
		constexpr uint8_t  opaque{ 255 };
		constexpr int16_t  alpha_channel{ -1 };
		constexpr uint32_t resource_origin{ 4000 };

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

		static auto align_to(const size_t value, const size_t multiple) noexcept -> size_t
		{
			const size_t remainder{ value % multiple };

			return remainder == 0 ? value : value + multiple - remainder;
		}

		static auto read_u32(const std::span<const uint8_t> data) noexcept -> uint32_t
		{
			return (static_cast<uint32_t>(data[0]) << 24) |
				   (static_cast<uint32_t>(data[1]) << 16) |
				   (static_cast<uint32_t>(data[2]) << 8) |
				   static_cast<uint32_t>(data[3]);
		}

		static auto read_pascal(psdpimg::common::byte_reader& reader) -> std::string
		{
			const size_t begin{ reader.position() };
			const auto length{ reader.read<uint8_t>() };
			const auto text{ reader.read_span(length) };

			reader.seek(begin + psdpimg::psd::align_to(static_cast<size_t>(length) + 1, 4));

			return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
		}

		static auto apply_tag(layer& target, const tag& item) -> void
		{
			const bool short_payload{ item.data.size() < 4 };

			if (short_payload)
			{
				return;
			}

			const auto number{ psdpimg::psd::read_u32(item.data) };

			if (item.key == "lyid")
			{
				target.layer_id     = number;
				target.has_layer_id = true;

				return;
			}

			if (item.key == "lsct")
			{
				const bool known{ number <= static_cast<uint32_t>(psdpimg::psd::section_t::divider) };

				target.section = known ? static_cast<section_t>(number) : psdpimg::psd::section_t::layer;

				return;
			}

			if (item.key == "luni")
			{
				const size_t available{ (item.data.size() - 4) / 2 };
				const auto count{ static_cast<size_t>(number) };
				const size_t limit{ std::min(count, available) };

				std::vector<uint16_t> units{};
				units.reserve(limit);

				for (size_t i{}; i < limit; ++i)
				{
					const size_t offset{ 4 + i * 2 };

					units.push_back(static_cast<uint16_t>((item.data[offset] << 8) | item.data[offset + 1]));
				}

				target.unicode_name = psdpimg::common::utf16_to_utf8(units);
			}
		}

		static auto read_extra(layer& target, psdpimg::common::byte_reader& reader, const size_t extra_end) -> void
		{
			reader.skip(reader.read<uint32_t>(std::endian::big));
			reader.skip(reader.read<uint32_t>(std::endian::big));

			target.name         = psdpimg::psd::read_pascal(reader);
			target.unicode_name = target.name;

			while (reader.position() + 12 <= extra_end)
			{
				const auto signature{ reader.read_span(4) };
				const auto* first{ signature.data() };
				const bool is_tag{ std::memcmp(first, signature_tag, 4) == 0 };
				const bool is_tag_64{ std::memcmp(first, signature_tag_64, 4) == 0 };

				if (!is_tag && !is_tag_64)
				{
					reader.seek(reader.position() - 4);
					reader.seek(reader.position() + reader.read<uint32_t>(std::endian::big));

					continue;
				}

				const auto key_bytes{ reader.read_span(4) };
				const auto length{ reader.read<uint32_t>(std::endian::big) };
				const auto payload{ reader.read_span(length) };

				if (length % 2 != 0)
				{
					reader.skip(1);
				}

				tag item{};
				item.key.assign(reinterpret_cast<const char*>(key_bytes.data()), key_bytes.size());
				item.data.assign(payload.begin(), payload.end());

				psdpimg::psd::apply_tag(target, item);
				target.tags.push_back(std::move(item));
			}

			reader.seek(extra_end);
		}

		static auto strip_placeholder(layer& target) noexcept -> void
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

		static auto read_record(psdpimg::common::byte_reader& reader, layer_record& record) -> void
		{
			layer& target{ record.layer };

			target.top    = reader.read<int32_t>(std::endian::big);
			target.left   = reader.read<int32_t>(std::endian::big);
			target.bottom = reader.read<int32_t>(std::endian::big);
			target.right  = reader.read<int32_t>(std::endian::big);

			const auto channel_count{ reader.read<uint16_t>(std::endian::big) };

			record.channels.reserve(channel_count);

			for (uint16_t i{}; i < channel_count; ++i)
			{
				const channel_info info
				{
					.id = reader.read<int16_t>(std::endian::big),
					.length = reader.read<uint32_t>(std::endian::big)
				};

				record.channels.push_back(info);
			}

			reader.skip(8);

			const auto opacity{ reader.read<uint8_t>() };
			const auto clipping{ reader.read<uint8_t>() };
			const auto flags{ reader.read<uint8_t>() };

			reader.skip(1);

			target.opacity  = opacity;
			target.clipping = clipping;
			target.visible  = (flags & flag_hidden) == 0;

			const auto extra_length{ reader.read<uint32_t>(std::endian::big) };

			psdpimg::psd::read_extra(target, reader, reader.position() + extra_length);
			psdpimg::psd::strip_placeholder(target);
		}

		static auto channel_slot(const int16_t id) noexcept -> int32_t
		{
			if (id == alpha_channel)
			{
				return { 3 };
			}

			const bool is_color{ id >= 0 && id < 3 };

			return is_color ? static_cast<int32_t>(id) : -1;
		}

		static auto read_plane(psdpimg::common::byte_reader& reader, const uint32_t width,
			const uint32_t height, std::vector<uint8_t>& target) -> void
		{
			const auto compression{ reader.read<uint16_t>(std::endian::big) };

			if (compression == compression_raw)
			{
				const auto data{ reader.read_span(static_cast<size_t>(width) * height) };

				target.assign(data.begin(), data.end());

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
				counts.push_back(reader.read<uint16_t>(std::endian::big));
			}

			target.assign(static_cast<size_t>(width) * height, 0);

			for (uint32_t y{}; y < height; ++y)
			{
				const auto packed{ reader.read_span(counts[y]) };
				const auto row{ psdpimg::psd::unpack_bits(packed, width) };
				const auto column{ static_cast<size_t>(y) * width };
				const auto destination{ target.begin() + static_cast<ptrdiff_t>(column) };

				std::ranges::copy(row, destination);
			}
		}

		static auto read_layer_data(psdpimg::common::byte_reader& reader, layer_record& record) -> void
		{
			const auto width{ static_cast<uint32_t>(std::max<int32_t>(record.layer.width(), 0)) };
			const auto height{ static_cast<uint32_t>(std::max<int32_t>(record.layer.height(), 0)) };
			const bool drawable{ width != 0 && height != 0 };
			const size_t plane_size{ static_cast<size_t>(width) * height };

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
				const size_t begin{ reader.position() };
				const bool usable{ drawable && info.length > 2 };

				if (usable)
				{
					psdpimg::psd::read_plane(reader, width, height, plane);

					const int32_t slot{ psdpimg::psd::channel_slot(info.id) };

					if (slot >= 0 && plane.size() == plane_size)
					{
						for (size_t i{}; i < plane_size; ++i)
						{
							record.layer.pixels[i * 4 + static_cast<size_t>(slot)] = plane[i];
						}
					}
				}

				reader.seek(begin + info.length);
			}
		}

		static auto read_resources(psdpimg::common::byte_reader& reader, document& target, const size_t end) -> void
		{
			while (reader.position() + 12 <= end)
			{
				const auto signature{ reader.read_span(4) };
				const bool known{ std::memcmp(signature.data(), signature_tag, 4) == 0 };

				if (!known)
				{
					break;
				}

				const auto id{ reader.read<uint16_t>(std::endian::big) };
				const auto name_length{ reader.read<uint8_t>() };
				const size_t name_used{ psdpimg::psd::align_to(static_cast<size_t>(name_length) + 1, 2) };

				reader.skip(name_used - 1);

				const auto size{ static_cast<size_t>(reader.read<uint32_t>(std::endian::big)) };

				if (id == resource_origin)
				{
					const auto payload{ reader.read_span(size) };
					const auto packed{ psdpimg::psd::unpack_origin(payload) };

					target.raw_pimg  = packed.bytes;
					target.raw_stamp = packed.stamp;
				}
				else
				{
					reader.skip(size);
				}

				if (size % 2 != 0)
				{
					reader.skip(1);
				}
			}

			reader.seek(end);
		}

		static auto read_layer_info(psdpimg::common::byte_reader& reader, document& target,
			const size_t section_end) -> void
		{
			if (reader.position() + 2 > section_end)
			{
				return;
			}

			const auto raw_count{ reader.read<int16_t>(std::endian::big) };
			const auto magnitude
			{
				raw_count < 0 ?
				-static_cast<int32_t>(raw_count) :
				raw_count
			};

			const auto count{ static_cast<size_t>(magnitude) };

			std::vector<layer_record> records{};
			records.resize(count);

			for (auto& record : records)
			{
				psdpimg::psd::read_record(reader, record);
			}

			for (auto& record : records)
			{
				psdpimg::psd::read_layer_data(reader, record);
			}

			for (auto& record : records)
			{
				target.layers.push_back(std::move(record.layer));
			}
		}
	}

	auto read(const std::span<const uint8_t> bytes) -> document
	{
		psdpimg::common::byte_reader reader{ bytes };

		const auto signature{ reader.read_span(4) };

		if (std::memcmp(signature.data(), signature_psd, 4) != 0)
		{
			throw std::runtime_error{ "psd::read: not a PSD document" };
		}

		const auto version{ reader.read<uint16_t>(std::endian::big) };
		const bool bad_version{ version != 1 };

		if (bad_version)
		{
			throw std::runtime_error{ "psd::read: only version 1 is supported" };
		}

		reader.skip(6);
		reader.skip(2);

		document result{};

		result.height = reader.read<uint32_t>(std::endian::big);
		result.width  = reader.read<uint32_t>(std::endian::big);

		const auto depth{ reader.read<uint16_t>(std::endian::big) };
		const bool bad_depth{ depth != 8 };

		reader.skip(2);

		if (bad_depth)
		{
			throw std::runtime_error{ "psd::read: only 8 bit documents are supported" };
		}

		reader.skip(reader.read<uint32_t>(std::endian::big));

		const auto resource_length{ reader.read<uint32_t>(std::endian::big) };
		const size_t resource_end{ reader.position() + resource_length };

		psdpimg::psd::read_resources(reader, result, resource_end);
		reader.seek(resource_end);

		const auto block_length{ reader.read<uint32_t>(std::endian::big) };
		const size_t block_end{ reader.position() + block_length };
		const bool has_layer_info{ block_length >= 4 };

		if (has_layer_info)
		{
			const auto layer_length{ reader.read<uint32_t>(std::endian::big) };
			const size_t layer_end{ reader.position() + layer_length };

			psdpimg::psd::read_layer_info(reader, result, layer_end);
			reader.seek(layer_end);
		}

		reader.seek(block_end);

		return result;
	}
}
