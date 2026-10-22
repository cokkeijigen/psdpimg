#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace psdpimg::common
{
	template<class T>
	concept scalar_t = std::integral<T> || std::floating_point<T>;

	template<psdpimg::common::scalar_t T>
	[[nodiscard]] inline auto flip(const T value) noexcept -> T
	{
		if constexpr (std::integral<T>)
		{
			return std::byteswap(value);
		}
		else
		{
			using bits_t = std::conditional_t<sizeof(T) == sizeof(uint32_t), uint32_t, uint64_t>;

			return std::bit_cast<T>(std::byteswap(std::bit_cast<bits_t>(value)));
		}
	}

	class byte_reader
	{
		const uint8_t* m_base{};
		size_t         m_size{};
		size_t         m_position{};

	public:
		byte_reader() noexcept = default;

		explicit byte_reader(std::span<const uint8_t> data) noexcept :
			m_base{ data.data() }, m_size{ data.size() }, m_position{}
		{
		}

		[[nodiscard]] inline auto size() const noexcept -> size_t
		{
			return this->m_size;
		}

		[[nodiscard]] inline auto position() const noexcept -> size_t
		{
			return this->m_position;
		}

		[[nodiscard]] inline auto remaining() const noexcept -> size_t
		{
			return this->m_size - this->m_position;
		}

		auto seek(const size_t offset) -> void;
		auto skip(const size_t count) -> void;

		template<psdpimg::common::scalar_t T>
		[[nodiscard]] inline auto read(const std::endian order = std::endian::native) -> T
		{
			const auto value{ *reinterpret_cast<const T*>(this->advance(sizeof(T))) };

			if (order == std::endian::native)
			{
				return value;
			}

			return psdpimg::common::flip(value);
		}

		[[nodiscard]] auto  read_span(const size_t count) -> std::span<const uint8_t>;
		[[nodiscard]] auto read_bytes(const size_t count) -> std::vector<uint8_t>;
		[[nodiscard]] auto   c_string(const size_t offset) const -> std::string;

	private:
		auto advance(const size_t count) -> const uint8_t*;
	};
}
