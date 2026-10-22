#include "common/unicode.hpp"

namespace psdpimg::common
{
	auto utf8_to_utf16(const std::string_view text) -> std::vector<uint16_t>
	{
		std::vector<uint16_t> result{};
		result.reserve(text.size());

		size_t index{};

		while (index < text.size())
		{
			const auto lead{ static_cast<uint8_t>(text[index]) };
			uint32_t code{};
			size_t   extra{};

			if (lead < 0x80)
			{
				code = lead;
			}
			else if ((lead & 0xE0) == 0xC0)
			{
				code  = lead & 0x1F;
				extra = 1;
			}
			else if ((lead & 0xF0) == 0xE0)
			{
				code  = lead & 0x0F;
				extra = 2;
			}
			else if ((lead & 0xF8) == 0xF0)
			{
				code  = lead & 0x07;
				extra = 3;
			}
			else
			{
				code = 0xFFFD;
			}

			const bool truncated{ extra != 0 && index + extra >= text.size() };

			if (truncated)
			{
				code  = 0xFFFD;
				extra = 0;
			}

			for (size_t i{ 1 }; i <= extra; ++i)
			{
				const auto continuation{ static_cast<uint8_t>(text[index + i]) };

				code = (code << 6) | (continuation & 0x3F);
			}

			index += extra + 1;

			if (code >= 0x10000 && code <= 0x10FFFF)
			{
				const auto shifted{ code - 0x10000 };

				result.push_back(static_cast<uint16_t>(0xD800 + (shifted >> 10)));
				result.push_back(static_cast<uint16_t>(0xDC00 + (shifted & 0x3FF)));

				continue;
			}

			result.push_back(static_cast<uint16_t>(code > 0xFFFF ? 0xFFFD : code));
		}

		return result;
	}

	auto utf16_to_utf8(std::span<const uint16_t> units) -> std::string
	{
		std::string result{};
		result.reserve(units.size() * 3);

		size_t index{};

		while (index < units.size())
		{
			uint32_t code{ units[index] };
			++index;

			const bool is_high{ code >= 0xD800 && code <= 0xDBFF };
			const bool has_pair{ is_high && index < units.size() };

			if (has_pair)
			{
				const uint32_t low{ units[index] };
				const bool     is_low{ low >= 0xDC00 && low <= 0xDFFF };

				if (is_low)
				{
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
					++index;
				}
			}

			if (code < 0x80)
			{
				result.push_back(static_cast<char>(code));

				continue;
			}

			if (code < 0x800)
			{
				result.push_back(static_cast<char>(0xC0 | (code >> 6)));
				result.push_back(static_cast<char>(0x80 | (code & 0x3F)));

				continue;
			}

			if (code < 0x10000)
			{
				result.push_back(static_cast<char>(0xE0 | (code >> 12)));
				result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | (code & 0x3F)));

				continue;
			}

			result.push_back(static_cast<char>(0xF0 | (code >> 18)));
			result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}

		return result;
	}
}
