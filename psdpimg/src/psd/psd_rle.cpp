#include <stdexcept>

#include "psd/psd_rle.hpp"

namespace psdpimg::psd
{
	namespace
	{
		constexpr size_t literal_limit{ 128 };
		constexpr size_t repeat_threshold{ 3 };
	}

	auto pack_bits(const std::span<const uint8_t> source) -> std::vector<uint8_t>
	{
		std::vector<uint8_t> output{};
		output.reserve(source.size() / 2 + 16);

		const size_t total{ source.size() };
		size_t index{};

		while (index < total)
		{
			size_t run_end{ index + 1 };

			while (run_end < total && run_end - index < literal_limit && source[run_end] == source[index])
			{
				++run_end;
			}

			const bool is_run{ run_end - index >= repeat_threshold };

			if (is_run)
			{
				output.push_back(static_cast<uint8_t>(257 - (run_end - index)));
				output.push_back(source[index]);
				index = run_end;

				continue;
			}

			const size_t literal_begin{ index };

			while (index < total && index - literal_begin < literal_limit)
			{
				size_t probe{ index + 1 };

				while (probe < total && probe - index < repeat_threshold && source[probe] == source[index])
				{
					++probe;
				}

				const bool hit_run{ probe - index >= repeat_threshold };

				if (hit_run)
				{
					break;
				}

				++index;
			}

			output.push_back(static_cast<uint8_t>(index - literal_begin - 1));
			{
				const auto first{ source.begin() + static_cast<ptrdiff_t>(literal_begin) };
				const auto last{ source.begin() + static_cast<ptrdiff_t>(index) };

				output.insert(output.end(), first, last);
			}
		}

		return output;
	}

	auto unpack_bits(const std::span<const uint8_t> source, const size_t expected) -> std::vector<uint8_t>
	{
		std::vector<uint8_t> output{};
		output.reserve(expected);

		size_t index{};

		while (index < source.size())
		{
			const auto control{ source[index] };
			++index;

			if (control < literal_limit)
			{
				const size_t count{ static_cast<size_t>(control) + 1 };

				if (index + count > source.size())
				{
					throw std::runtime_error{ "psd::unpack_bits: literal overruns the stream" };
				}

				{
					const auto first{ source.begin() + static_cast<ptrdiff_t>(index) };
					const auto last{ source.begin() + static_cast<ptrdiff_t>(index + count) };

					output.insert(output.end(), first, last);
				}

				index += count;

				continue;
			}

			if (control == 128)
			{
				continue;
			}

			if (index >= source.size())
			{
				throw std::runtime_error{ "psd::unpack_bits: repeat overruns the stream" };
			}

			const size_t count{ 257 - static_cast<size_t>(control) };

			output.insert(output.end(), count, source[index]);
			++index;
		}

		if (output.size() != expected)
		{
			throw std::runtime_error{ "psd::unpack_bits: decoded size mismatch" };
		}

		return output;
	}
}
