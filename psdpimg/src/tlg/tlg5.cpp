#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "tlg/tlg5.hpp"
#include "common/byte_reader.hpp"
#include "common/byte_writer.hpp"

namespace psdpimg::tlg
{
	namespace
	{
		constexpr uint8_t signature_tlg5[11]{ 'T', 'L', 'G', '5', '.', '0', 0, 'r', 'a', 'w', 0x1A };
		constexpr uint8_t signature_tlg6[11]{ 'T', 'L', 'G', '6', '.', '0', 0, 'r', 'a', 'w', 0x1A };
		constexpr uint8_t signature_sds[11]{ 'T', 'L', 'G', '0', '.', '0', 0, 's', 'd', 's', 0x1A };

		constexpr uint32_t window_size{ 4096 };
		constexpr uint32_t window_mask{ window_size - 1 };
		constexpr uint32_t block_rows{ 4 };
		constexpr uint32_t max_match{ 273 };
		constexpr uint32_t min_match{ 3 };

		constexpr std::array<uint8_t, 4> channel_offsets{ 2, 1, 0, 3 };

		class lzss_decoder
		{
			std::array<uint8_t, window_size> m_text{};
			uint32_t                         m_cursor{};

		public:
			auto decode(const std::span<const uint8_t> source, std::span<uint8_t> target) -> void
			{
				uint32_t flags{};
				size_t   input{};
				size_t   output{};

				while (input < source.size())
				{
					flags >>= 1;

					if ((flags & 0x100) == 0)
					{
						flags = static_cast<uint32_t>(source[input++]) | 0xFF00;
					}

					if ((flags & 1) != 0)
					{
						const auto low{ static_cast<uint32_t>(source[input]) };
						const auto high{ static_cast<uint32_t>(source[input + 1] & 0x0F) << 8 };
						const auto position{ low | high };
						uint32_t length{ static_cast<uint32_t>((source[input + 1] & 0xF0) >> 4) };
						input += 2;
						length += min_match;

						if (length == 18)
						{
							length += source[input++];
						}

						auto copy_position{ position };

						while (length-- > 0)
						{
							const auto byte{ this->m_text[copy_position] };
							copy_position = (copy_position + 1) & window_mask;

							target[output++] =             byte;
							this->m_text[this->m_cursor] = byte;
							this->m_cursor = (this->m_cursor + 1) & window_mask;
						}
					}
					else
					{
						const auto byte{ source[input++] };

						target[output++] =             byte;
						this->m_text[this->m_cursor] = byte;
						this->m_cursor = (this->m_cursor + 1) & window_mask;
					}
				}
			}
		};

		class lzss_encoder
		{
			std::array<uint8_t, window_size> m_text{};
			std::array<int32_t, window_size> m_head{};
			std::array<int32_t, window_size> m_chain{};
			uint32_t                         m_cursor{};
			uint32_t                         m_written{};

		public:
			lzss_encoder()
			{
				this->m_head.fill(-1);
				this->m_chain.fill(-1);
			}

			[[nodiscard]] auto encode(const std::span<const uint8_t> source) -> std::vector<uint8_t>
			{
				std::vector<uint8_t> output{};
				output.reserve(source.size() / 2 + 16);

				size_t position{};

				while (position < source.size())
				{
					const auto flags_position{ output.size() };
					output.push_back(0);

					uint8_t flags{};

					for (uint32_t slot{}; slot < 8 && position < source.size(); ++slot)
					{
						const auto match{ this->find_match(source, position) };

						if (match.length >= min_match)
						{
							flags = static_cast<uint8_t>(flags | (1U << slot));
							this->emit_match(source, position, match, output);
							position += match.length;
						}
						else
						{
							this->emit_literal(source[position], output);
							++position;
						}
					}

					output[flags_position] = flags;
				}

				return output;
			}

			auto snapshot() const -> std::vector<uint8_t>
			{
				const size_t table_bytes{ static_cast<size_t>(window_size) * sizeof(int32_t) };

				std::vector<uint8_t> state(static_cast<size_t>(window_size) + table_bytes * 2 + 8);
				auto* cursor{ state.data() };

				std::memcpy(cursor, this->m_text.data(), window_size);
				cursor += window_size;
				std::memcpy(cursor, this->m_head.data(), table_bytes);
				cursor += table_bytes;
				std::memcpy(cursor, this->m_chain.data(), table_bytes);
				cursor += table_bytes;
				std::memcpy(cursor, &this->m_cursor, sizeof(this->m_cursor));
				cursor += sizeof(this->m_cursor);
				std::memcpy(cursor, &this->m_written, sizeof(this->m_written));

				return state;
			}

			auto restore(const std::vector<uint8_t>& state) -> void
			{
				const size_t table_bytes{ static_cast<size_t>(window_size) * sizeof(int32_t) };
				const auto* cursor{ state.data() };

				std::memcpy(this->m_text.data(), cursor, window_size);
				cursor += window_size;
				std::memcpy(this->m_head.data(), cursor, table_bytes);
				cursor += table_bytes;
				std::memcpy(this->m_chain.data(), cursor, table_bytes);
				cursor += table_bytes;
				std::memcpy(&this->m_cursor, cursor, sizeof(this->m_cursor));
				cursor += sizeof(this->m_cursor);
				std::memcpy(&this->m_written, cursor, sizeof(this->m_written));
			}

		private:
			struct match_result
			{
				uint32_t distance{};
				uint32_t length{};
			};

			static auto hash_of(const uint8_t first, const uint8_t second, const uint8_t third) noexcept -> size_t
			{
				return static_cast<size_t>(((static_cast<uint32_t>(first) * 251U) ^
												 (static_cast<uint32_t>(second) * 31U) ^
												 static_cast<uint32_t>(third)) & window_mask);
			}

			[[nodiscard]] auto find_match(const std::span<const uint8_t> source, const size_t position) const
				-> match_result
			{
				if (position + min_match > source.size())
				{
					return {};
				}

				const auto first{ source[position] };
				const auto second{ source[position + 1] };
				const auto third{ source[position + 2] };
				const auto key{ psdpimg::tlg::lzss_encoder::hash_of(first, second, third) };
				int32_t candidate{ this->m_head[key] };
				const auto available{ static_cast<uint32_t>(source.size() - position) };

				match_result best{};
				const auto limit{ static_cast<uint32_t>(std::min<size_t>(max_match, available)) };

				for (int32_t attempts{}; candidate >= 0 && attempts < 24; ++attempts)
				{
					const auto anchored{ static_cast<uint32_t>(candidate) };
					const auto distance{ static_cast<uint32_t>((this->m_cursor - anchored) & window_mask) };

					if (distance == 0)
					{
						break;
					}

					uint32_t length{};

					while (length < limit)
					{
						const auto index{ (static_cast<uint32_t>(candidate) + length) & window_mask };
						const auto expected
						{
							length < distance ? this->m_text[index] : source[position + length - distance]
						};

						if (expected != source[position + length])
						{
							break;
						}

						++length;
					}

					if (length > best.length)
					{
						best.distance = distance;
						best.length   = length;
					}

					if (best.length >= limit)
					{
						break;
					}

					candidate = this->m_chain[static_cast<size_t>(candidate)];
				}

				return best;
			}

			auto emit_literal(const uint8_t byte, std::vector<uint8_t>& output) -> void
			{
				output.push_back(byte);
				this->push(byte);
			}

			auto emit_match(const std::span<const uint8_t> source, const size_t position,
				const match_result& match, std::vector<uint8_t>& output) -> void
			{
				const auto match_position{ this->m_cursor - match.distance };
				const auto ring_position{ static_cast<uint32_t>(match_position & window_mask) };

				uint32_t nibble{ match.length - min_match };

				if (match.length >= 18)
				{
					nibble = 15;
				}

				output.push_back(static_cast<uint8_t>(ring_position & 0xFF));
				output.push_back(static_cast<uint8_t>(((nibble & 0x0F) << 4) | ((ring_position >> 8) & 0x0F)));

				if (match.length >= 18)
				{
					output.push_back(static_cast<uint8_t>(match.length - 18));
				}

				for (uint32_t i{}; i < match.length; ++i)
				{
					this->push(source[position + i]);
				}
			}

			auto push(const uint8_t byte) -> void
			{
				this->m_text[this->m_cursor] = byte;
				++this->m_written;

				if (this->m_written >= 3)
				{
					const auto target{ (this->m_cursor - 2) & window_mask };
					const auto first{ this->m_text[target] };
					const auto second{ this->m_text[(target + 1) & window_mask] };
					const auto key{ psdpimg::tlg::lzss_encoder::hash_of(first, second, byte) };

					this->m_chain[target] = this->m_head[key];
					this->m_head[key] = static_cast<int32_t>(target);
				}

				this->m_cursor = (this->m_cursor + 1) & window_mask;
			}
		};

		static auto compose_row(std::span<uint8_t> target, const std::vector<uint8_t>& upper,
			const std::array<std::span<const uint8_t>, 4>& planes, const uint32_t channel_count,
			const uint32_t width, const bool has_upper) noexcept -> void
		{
			std::array<uint8_t, 4> accumulators{};

			for (uint32_t x{}; x < width; ++x)
			{
				const auto green{ planes[1][x] };

				accumulators[0] = static_cast<uint8_t>(accumulators[0] + planes[0][x] + green);
				accumulators[1] = static_cast<uint8_t>(accumulators[1] + green);
				accumulators[2] = static_cast<uint8_t>(accumulators[2] + planes[2][x] + green);

				if (channel_count > 3)
				{
					accumulators[3] = static_cast<uint8_t>(accumulators[3] + planes[3][x]);
				}

				const auto base{ static_cast<size_t>(x) * 4 };

				for (size_t channel{}; channel < 4; ++channel)
				{
					const auto is_alpha{ channel == 3 };
					const auto slot{ base + channel_offsets[channel] };

					if (is_alpha && channel_count <= 3)
					{
						target[slot] = 0xFF;

						continue;
					}

					const auto fill{ has_upper ? upper[slot] : static_cast<uint8_t>(0) };

					target[slot] = static_cast<uint8_t>(accumulators[channel] + fill);
				}
			}
		}
	}

	auto is_tlg(const std::span<const uint8_t> blob) noexcept -> bool
	{
		if (blob.size() < 15)
		{
			return false;
		}

		const bool is_tlg5{ std::memcmp(blob.data(), signature_tlg5, 11) == 0 };
		const bool is_tlg6{ std::memcmp(blob.data(), signature_tlg6, 11) == 0 };
		const bool is_raw{ is_tlg5 || is_tlg6 };
		const bool is_wrapped{ std::memcmp(blob.data(), signature_sds, 11) == 0 };

		return is_raw || is_wrapped;
	}

	auto decode(const std::span<const uint8_t> blob) -> psdpimg::common::image
	{
		if (!psdpimg::tlg::is_tlg(blob))
		{
			throw std::runtime_error{ "tlg::decode: resource is not a TLG image" };
		}

		std::span<const uint8_t> inner{ blob };

		if (std::memcmp(blob.data(), signature_sds, 11) == 0)
		{
			const auto* header{ blob.data() + 11 };
			const auto raw_length{ *reinterpret_cast<const uint32_t*>(header) };

			const bool overflow{ static_cast<size_t>(15) + raw_length > blob.size() };

			if (overflow)
			{
				throw std::runtime_error{ "tlg::decode: wrapper length exceeds resource" };
			}

			inner = blob.subspan(15, raw_length);
		}

		if (std::memcmp(inner.data(), signature_tlg5, 11) != 0)
		{
			throw std::runtime_error{ "tlg::decode: only TLG5 is supported" };
		}

		psdpimg::common::byte_reader reader{ inner };
		reader.skip(11);

		const auto colors{ static_cast<uint32_t>(reader.read<uint8_t>()) };
		const auto width{ reader.read<uint32_t>() };
		const auto height{ reader.read<uint32_t>() };
		const auto rows_per_block{ reader.read<uint32_t>() };

		const bool bad_colors{ colors != 3 && colors != 4 };
		const bool bad_size{ width == 0 || height == 0 || rows_per_block == 0 };

		if (bad_colors)
		{
			throw std::runtime_error{ "tlg::decode: unsupported color count" };
		}

		if (bad_size)
		{
			throw std::runtime_error{ "tlg::decode: invalid dimensions" };
		}

		const auto block_count{ (height - 1) / rows_per_block + 1 };
		reader.skip(static_cast<size_t>(block_count) * 4);

		psdpimg::common::image picture{};
		picture.resize(width, height);

		lzss_decoder decoder{};
		std::vector<uint8_t> plane_storage(static_cast<size_t>(rows_per_block) * width * colors);
		std::array<std::span<const uint8_t>, 4> planes{};
		std::array<std::span<uint8_t>, 4>       writable_planes{};
		std::vector<uint8_t> upper_row(static_cast<size_t>(width) * 4);
		std::vector<uint8_t> current_row(static_cast<size_t>(width) * 4);
		bool has_upper{};

		for (uint32_t block_y{}; block_y < height; block_y += rows_per_block)
		{
			const auto rows{ std::min(rows_per_block, height - block_y) };
			const auto plane_size{ static_cast<size_t>(rows) * width };

			for (uint32_t channel{}; channel < colors; ++channel)
			{
				const auto mode{ reader.read<uint8_t>() };
				const auto length{ reader.read<uint32_t>() };
				const auto payload{ reader.read_span(length) };
				auto* const base{ plane_storage.data() + plane_size * channel };

				writable_planes[channel] = std::span<uint8_t>{ base, plane_size };

				if (mode == 0)
				{
					decoder.decode(payload, writable_planes[channel]);
				}
				else
				{
					if (payload.size() != plane_size)
					{
						throw std::runtime_error{ "tlg::decode: raw plane size mismatch" };
					}

					std::memcpy(base, payload.data(), plane_size);
				}

				planes[channel] = std::span<const uint8_t>{ base, plane_size };
			}

			const size_t row_bytes{ static_cast<size_t>(width) * 4 };

			for (uint32_t row{}; row < rows; ++row)
			{
				std::array<std::span<const uint8_t>, 4> row_planes{};

				for (uint32_t channel{}; channel < colors; ++channel)
				{
					row_planes[channel] = planes[channel].subspan(static_cast<size_t>(row) * width, width);
				}

				psdpimg::tlg::compose_row(current_row, upper_row, row_planes, colors, width, has_upper);

				const auto destination{ picture.offset(0, block_y + row) };

				std::memcpy(picture.pixels.data() + destination, current_row.data(), row_bytes);
				std::memcpy(upper_row.data(), current_row.data(), row_bytes);

				has_upper = true;
			}
		}

		return picture;
	}

	auto encode(const psdpimg::common::image& picture) -> std::vector<uint8_t>
	{
		if (!picture.allocated())
		{
			throw std::runtime_error{ "tlg::encode: needs a full RGBA image" };
		}

		psdpimg::common::byte_writer output{};
		output.write_span(signature_tlg5);
		output.write<uint8_t>(4);
		output.write<uint32_t>(picture.width);
		output.write<uint32_t>(picture.height);
		output.write<uint32_t>(block_rows);

		const auto block_count{ (picture.height - 1) / block_rows + 1 };
		const auto table_position{ output.size() };

		output.write_zeros(static_cast<size_t>(block_count) * 4);

		lzss_encoder                        encoder{};
		std::array<std::vector<uint8_t>, 4> planes{};
		const size_t row_bytes{ static_cast<size_t>(picture.width) * 4 };

		std::vector<uint8_t> upper_row(row_bytes);

		for (uint32_t block_y{}; block_y < picture.height; block_y += block_rows)
		{
			const auto rows{ std::min(block_rows, picture.height - block_y) };
			const auto plane_size{ static_cast<size_t>(rows) * picture.width };

			for (auto& plane : planes)
			{
				plane.assign(plane_size, 0);
			}

			for (uint32_t row{}; row < rows; ++row)
			{
				const auto y{ block_y + row };
				const auto source_offset{ picture.offset(0, y) };
				std::array<uint8_t, 4> accumulators{};

				const bool is_first_row{ y == 0 };

				if (!is_first_row)
				{
					const auto previous{ picture.offset(0, y - 1) };

					std::memcpy(upper_row.data(), picture.pixels.data() + previous, row_bytes);
				}

				for (uint32_t x{}; x < picture.width; ++x)
				{
					const auto base{ static_cast<size_t>(x) * 4 };
					std::array<uint8_t, 4> deltas{};

					for (uint32_t channel{}; channel < 4; ++channel)
					{
						const auto pixel_offset{ source_offset + base + channel_offsets[channel] };
						const auto upper_offset{ base + channel_offsets[channel] };
						const auto upper_value
						{
							is_first_row ?
							static_cast<uint8_t>(0) :
							upper_row[upper_offset]
						};
						const auto goal{ static_cast<uint8_t>(picture.pixels[pixel_offset] - upper_value) };

						deltas[channel]       = static_cast<uint8_t>(goal - accumulators[channel]);
						accumulators[channel] = goal;
					}

					const auto green{ deltas[1] };
					const auto row_offset{ static_cast<size_t>(row) * picture.width + x };

					planes[0][row_offset] = static_cast<uint8_t>(deltas[0] - green);
					planes[1][row_offset] = green;
					planes[2][row_offset] = static_cast<uint8_t>(deltas[2] - green);
					planes[3][row_offset] = deltas[3];
				}
			}

			for (uint32_t channel{}; channel < 4; ++channel)
			{
				const auto state{ encoder.snapshot() };
				auto payload{ encoder.encode(planes[channel]) };

				if (payload.size() < plane_size)
				{
					output.write<uint8_t>(0);
				}
				else
				{
					encoder.restore(state);
					payload = planes[channel];
					output.write<uint8_t>(1);
				}

				output.write<uint32_t>(static_cast<uint32_t>(payload.size()));
				output.write_span(payload);
			}

			const auto slot{ table_position + static_cast<size_t>(block_y / block_rows) * 4 };

			output.patch<uint32_t>(slot, static_cast<uint32_t>(output.size()));
		}

		return output.take();
	}
}
