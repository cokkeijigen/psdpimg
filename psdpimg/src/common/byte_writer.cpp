#include "common/byte_writer.hpp"

namespace psdpimg::common
{
	auto byte_writer::write_span(const std::span<const uint8_t> data) -> void
	{
		this->m_data.insert(this->m_data.end(), data.begin(), data.end());
	}

	auto byte_writer::write_zeros(const size_t count) -> void
	{
		this->m_data.insert(this->m_data.end(), count, uint8_t{});
	}

	auto byte_writer::write_c_string(const std::string_view text) -> void
	{
		const auto* const begin{ reinterpret_cast<const uint8_t*>(text.data()) };

		this->m_data.insert(this->m_data.end(), begin, begin + text.size());
		this->m_data.push_back(0);
	}
}
