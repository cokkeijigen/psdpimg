#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "common/byte_reader.hpp"

namespace psdpimg::common
{
	class byte_writer
	{
		std::vector<uint8_t> m_data{};

	public:
		byte_writer() noexcept = default;

		[[nodiscard]] inline auto size() const noexcept -> size_t
		{
			return this->m_data.size();
		}

		[[nodiscard]] inline auto data() const noexcept -> const std::vector<uint8_t>&
		{
			return this->m_data;
		}

		[[nodiscard]] inline auto take() noexcept -> std::vector<uint8_t>
		{
			return std::move(this->m_data);
		}

		inline auto reserve(const size_t count) -> void
		{
			this->m_data.reserve(count);
		}

		template<psdpimg::common::scalar_t T>
		inline auto write(const T value, const std::endian order = std::endian::native) -> void
		{
			auto* cursor{ this->append<T>() };
			const auto stored{ order == std::endian::native ? value : psdpimg::common::flip(value) };

			*cursor = stored;
		}

		auto     write_span(const std::span<const uint8_t> data) -> void;
		auto    write_zeros(const size_t count) -> void;
		auto write_c_string(const std::string_view text) -> void;

		template<psdpimg::common::scalar_t T>
		inline auto patch(const size_t offset, const T value, const std::endian order = std::endian::native)
			-> void
		{
			auto* cursor{ this->target<T>(offset) };
			const auto stored{ order == std::endian::native ? value : psdpimg::common::flip(value) };

			*cursor = stored;
		}

	private:
		template<psdpimg::common::scalar_t T>
		inline auto append() -> T*
		{
			const auto offset{ this->m_data.size() };

			this->m_data.resize(offset + sizeof(T));

			return reinterpret_cast<T*>(this->m_data.data() + offset);
		}

		template<psdpimg::common::scalar_t T>
		inline auto target(const size_t offset) -> T*
		{
			const bool overflow{ offset + sizeof(T) > this->m_data.size() };

			if (overflow)
			{
				throw std::out_of_range{ "byte_writer::target: offset beyond end" };
			}

			return reinterpret_cast<T*>(this->m_data.data() + offset);
		}
	};
}
