#include <concepts>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "psd/psd_origin.hpp"
#include "common/byte_reader.hpp"
#include "common/byte_writer.hpp"

namespace psdpimg::psd
{
	namespace
	{
		constexpr uint64_t fnv_offset_basis{ 0xCBF29CE484222325 };
		constexpr uint64_t fnv_prime{ 0x100000001B3 };
		constexpr uint8_t  payload_magic[4]{ 'P', 'D', 'P', 'O' };
		constexpr uint32_t payload_version{ 1 };
		constexpr size_t   payload_header{ 20 };

		static auto mix_block(uint64_t hash, const std::span<const uint8_t> data) noexcept -> uint64_t
		{
			size_t index{};
			const size_t whole{ data.size() - data.size() % 4 };

			for (; index < whole; index += 4)
			{
				const auto word{ *reinterpret_cast<const uint32_t*>(data.data() + index) };

				hash ^= word;
				hash *= fnv_prime;
			}

			for (; index < data.size(); ++index)
			{
				hash ^= data[index];
				hash *= fnv_prime;
			}

			return hash;
		}

		template<std::integral T>
		static auto mix_value(uint64_t hash, const T value) noexcept -> uint64_t
		{
			const auto* first{ reinterpret_cast<const uint8_t*>(&value) };
			const std::span<const uint8_t> bytes{ first, sizeof(T) };

			return psdpimg::psd::mix_block(hash, bytes);
		}
	}

	auto origin_stamp(const document& source) noexcept -> uint64_t
	{
		uint64_t hash{ fnv_offset_basis };

		hash = psdpimg::psd::mix_value(hash, source.width);
		hash = psdpimg::psd::mix_value(hash, source.height);
		hash = psdpimg::psd::mix_value(hash, static_cast<uint64_t>(source.layers.size()));

		for (const auto& item : source.layers)
		{
			const auto& name{ item.display_name() };
			const auto* first{ reinterpret_cast<const uint8_t*>(name.data()) };
			const std::span<const uint8_t> name_bytes{ first, name.size() };

			hash = psdpimg::psd::mix_value(hash, static_cast<uint32_t>(item.section));
			hash = psdpimg::psd::mix_value(hash, static_cast<uint32_t>(item.has_layer_id ? 1U : 0U));
			hash = psdpimg::psd::mix_value(hash, item.layer_id);
			hash = psdpimg::psd::mix_value(hash, item.opacity);
			hash = psdpimg::psd::mix_value(hash, static_cast<uint32_t>(item.visible ? 1U : 0U));
			hash = psdpimg::psd::mix_value(hash, item.left);
			hash = psdpimg::psd::mix_value(hash, item.top);
			hash = psdpimg::psd::mix_value(hash, item.width());
			hash = psdpimg::psd::mix_value(hash, item.height());
			hash = psdpimg::psd::mix_value(hash, static_cast<uint64_t>(name.size()));
			hash = psdpimg::psd::mix_block(hash, name_bytes);
			hash = psdpimg::psd::mix_value(hash, static_cast<uint64_t>(item.pixels.size()));
			hash = psdpimg::psd::mix_block(hash, item.pixels);
		}

		return hash;
	}

	auto pack_origin(const origin& source) -> std::vector<uint8_t>
	{
		psdpimg::common::byte_writer writer{};

		writer.reserve(payload_header + source.bytes.size());
		writer.write_span(payload_magic);
		writer.write<uint32_t>(payload_version, std::endian::big);
		writer.write<uint64_t>(source.stamp, std::endian::big);
		writer.write<uint32_t>(static_cast<uint32_t>(source.bytes.size()), std::endian::big);
		writer.write_span(source.bytes);

		return writer.take();
	}

	auto unpack_origin(const std::span<const uint8_t> data) -> origin
	{
		const bool too_small{ data.size() < payload_header };

		if (too_small)
		{
			return {};
		}

		const bool bad_magic{ std::memcmp(data.data(), payload_magic, 4) != 0 };

		if (bad_magic)
		{
			return {};
		}

		psdpimg::common::byte_reader reader{ data };

		reader.skip(4);

		const auto version{ reader.read<uint32_t>(std::endian::big) };

		if (version != payload_version)
		{
			return {};
		}

		origin result{};
		result.stamp = reader.read<uint64_t>(std::endian::big);

		const auto size{ static_cast<size_t>(reader.read<uint32_t>(std::endian::big)) };

		const bool truncated{ size > reader.remaining() };

		if (truncated)
		{
			return {};
		}

		result.bytes = reader.read_bytes(size);

		return result;
	}
}
