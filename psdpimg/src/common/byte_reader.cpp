#include <stdexcept>

#include "common/byte_reader.hpp"

namespace psdpimg::common
{
	auto byte_reader::seek(const size_t offset) -> void
	{
		const bool overflow{ offset > this->m_size };

		if (overflow)
		{
			throw std::out_of_range{ "byte_reader::seek: offset beyond end" };
		}

		this->m_position = offset;
	}

	auto byte_reader::skip(const size_t count) -> void
	{
		const bool overflow{ count > this->m_size - this->m_position };

		if (overflow)
		{
			throw std::out_of_range{ "byte_reader::skip: count beyond end" };
		}

		this->m_position += count;
	}

	auto byte_reader::advance(const size_t count) -> const uint8_t*
	{
		const bool overflow{ count > this->m_size - this->m_position };

		if (overflow)
		{
			throw std::out_of_range{ "byte_reader::advance: count beyond end" };
		}

		const auto*         cursor{ this->m_base + this->m_position };
		this->m_position += count;

		return cursor;
	}

	auto byte_reader::read_span(const size_t count) -> std::span<const uint8_t>
	{
		const auto* cursor{ this->advance(count) };

		return std::span<const uint8_t>{ cursor, count };
	}

	auto byte_reader::read_bytes(const size_t count) -> std::vector<uint8_t>
	{
		const auto view{ this->read_span(count) };

		return std::vector<uint8_t>{ view.begin(), view.end() };
	}

	auto byte_reader::c_string(const size_t offset) const -> std::string
	{
		const bool overflow{ offset >= this->m_size };

		if (overflow)
		{
			throw std::out_of_range{ "byte_reader::c_string: offset beyond end" };
		}

		const auto* cursor{ this->m_base + offset };
		std::string text{};

		for (auto index{ offset }; index < this->m_size; ++index)
		{
			const auto ch{ *cursor++ };

			if (ch == 0)
			{
				return text;
			}

			text.push_back(static_cast<char>(ch));
		}

		throw std::out_of_range{ "byte_reader::c_string: missing terminator" };
	}
}
