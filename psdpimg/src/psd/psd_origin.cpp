#include <bit>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include <psd_origin.hpp>
#include <xmem.hpp>

namespace psdpimg::psd
{
	namespace
	{
		constexpr uint64_t fnv_offset_basis { 0xCBF29CE484222325 };
		constexpr uint64_t fnv_prime        { 0x100000001B3 };
		constexpr uint8_t  payload_magic[4] { 'P', 'D', 'P', 'O' };
		constexpr uint32_t payload_version  { 1 };
		constexpr size_t   payload_header   { 20 };

		static auto mix_block(uint64_t hash, const std::span<const uint8_t> data) noexcept -> uint64_t
		{
			size_t       index {};
			const size_t whole { data.size() - data.size() % 4 };

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
		static inline auto mix_value(uint64_t hash, const T value) noexcept -> uint64_t
		{
			const uint8_t*                 first { reinterpret_cast<const uint8_t*>(&value) };
			const std::span<const uint8_t> bytes { first, sizeof(T) };

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
			const auto&                    name       { item.display_name() };
			const uint8_t*                 first      { reinterpret_cast<const uint8_t*>(name.data()) };
			const std::span<const uint8_t> name_bytes { first, name.size() };

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
		xmem::buffer<uint8_t> writer{};

		const uint32_t version{ std::byteswap(payload_version) };
		const uint64_t stamp  { std::byteswap(source.stamp) };
		const uint32_t length { std::byteswap(static_cast<uint32_t>(source.bytes.size())) };

		writer.resize(payload_header + source.bytes.size());
		writer.write(payload_magic);
		writer.write(reinterpret_cast<const uint8_t*>(&version), sizeof(version));
		writer.write(reinterpret_cast<const uint8_t*>(&stamp), sizeof(stamp));
		writer.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length));
		writer.write(std::span<const uint8_t>{ source.bytes });

		const std::span<uint8_t> written{ writer.written() };

		return { written.begin(), written.end() };
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

		const uint8_t* const ptr{ data.data() };

		const uint32_t version{ std::byteswap(*reinterpret_cast<const uint32_t*>(ptr + 4)) };

		if (version != payload_version)
		{
			return {};
		}

		origin result{};

		result.stamp = std::byteswap(*reinterpret_cast<const uint64_t*>(ptr + 8));

		const size_t size { std::byteswap(*reinterpret_cast<const uint32_t*>(ptr + 16)) };
		const uint8_t* bytes{ ptr + payload_header };

		const bool truncated{ size > data.size() - payload_header };

		if (truncated)
		{
			return {};
		}

		result.bytes.assign(bytes, bytes + size);

		return result;
	}
}
