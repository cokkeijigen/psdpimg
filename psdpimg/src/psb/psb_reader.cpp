#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/byte_reader.hpp"
#include "psb/psb_format.hpp"

namespace psdpimg::psb
{
	namespace
	{
		static auto sign_extend(const uint64_t bits, const size_t bytes) noexcept -> int64_t
		{
			if (bytes == 0 || bytes >= 8)
			{
				return static_cast<int64_t>(bits);
			}

			const uint64_t marker{ uint64_t{ 1 } << (bytes * 8 - 1) };
			const uint64_t extended{ (bits ^ marker) - marker };

			return static_cast<int64_t>(extended);
		}

		static auto in_range(const uint8_t value, const obj_type_t first, const obj_type_t last) noexcept -> bool
		{
			const auto low{ static_cast<uint8_t>(first) };
			const auto high{ static_cast<uint8_t>(last) };
			const bool inside{ value >= low && value <= high };

			return inside;
		}

		static auto offset_from(const uint8_t value, const obj_type_t base) noexcept -> size_t
		{
			const auto anchor{ static_cast<uint8_t>(base) };

			return static_cast<size_t>(value - anchor);
		}

		class psb_parser
		{
			std::span<const uint8_t> m_bytes{};
			uint32_t                 m_string_data_offset{};
			uint32_t                 m_chunk_data_offset{};
			std::vector<uint32_t>    m_string_offsets{};
			std::vector<uint32_t>    m_chunk_offsets{};
			std::vector<uint32_t>    m_chunk_lengths{};
			std::vector<std::string> m_names{};

		public:
			explicit psb_parser(std::span<const uint8_t> bytes) noexcept :
				m_bytes{ bytes }
			{
			}

			auto parse() -> archive
			{
				psdpimg::common::byte_reader reader{ this->m_bytes };

				const auto signature{ reader.read_span(4) };

				if (std::memcmp(signature.data(), magic_psb, 4) != 0)
				{
					throw std::runtime_error{ "psb_parser::parse: not a PSB container" };
				}

				archive result{};
				result.version = reader.read<uint16_t>();
				reader.skip(2);

				const auto declared_header{ reader.read<uint32_t>() };
				const auto offset_names{ reader.read<uint32_t>() };
				const auto offset_strings{ reader.read<uint32_t>() };
				const auto offset_strings_data{ reader.read<uint32_t>() };
				const auto offset_chunk_offsets{ reader.read<uint32_t>() };
				const auto offset_chunk_lengths{ reader.read<uint32_t>() };
				const auto offset_chunk_data{ reader.read<uint32_t>() };
				const auto offset_entries{ reader.read<uint32_t>() };

				if (declared_header != psdpimg::psb::header_length(result.version))
				{
					throw std::runtime_error{ "psb_parser::parse: unexpected header length" };
				}

				const auto names
				{
					result.version == 1 ?
					this->read_plain_names(reader, offset_names) :
					this->read_trie_names(reader, offset_names)
				};

				this->m_string_data_offset = offset_strings_data;
				this->m_string_offsets = this->read_array(reader, offset_strings);
				this->m_names = names;
				this->m_chunk_offsets = this->read_array(reader, offset_chunk_offsets);
				this->m_chunk_lengths = this->read_array(reader, offset_chunk_lengths);
				this->m_chunk_data_offset = offset_chunk_data;

				reader.seek(offset_entries);
				result.root = this->read_value(reader);

				return result;
			}

		private:
			auto peek_at(const uint32_t offset) const -> uint8_t
			{
				const bool out_of_range{ offset >= this->m_bytes.size() };

				if (out_of_range)
				{
					throw std::out_of_range{ "psb_parser::peek_at: offset beyond end" };
				}

				return this->m_bytes[offset];
			}

			auto peek_le(const uint32_t offset, const size_t count) const -> uint64_t
			{
				uint64_t value{};

				for (size_t i{}; i < count; ++i)
				{
					const auto index{ static_cast<uint32_t>(i) };
					const auto byte{ this->peek_at(offset + index) };

					value |= static_cast<uint64_t>(byte) << (8 * i);
				}

				return value;
			}

			auto count_bytes_at(const uint32_t offset) const -> size_t
			{
				const auto type_byte{ this->peek_at(offset) };
				const bool is_array
				{
					type_byte >= static_cast<uint8_t>(psdpimg::psb::obj_type_t::array_n1) &&
					type_byte <= static_cast<uint8_t>(psdpimg::psb::obj_type_t::array_n8)
				};

				if (!is_array)
				{
					return {};
				}

				return psdpimg::psb::offset_from(type_byte, psdpimg::psb::obj_type_t::array_n1) + 1;
			}

			auto array_end(const uint32_t offset) const -> uint32_t
			{
				const auto width{ this->count_bytes_at(offset) };

				if (width == 0)
				{
					return offset;
				}

				const auto count{ this->peek_le(offset + 1, width) };
				const auto entry_offset{ offset + 1 + static_cast<uint32_t>(width) };
				const auto entry_byte{ static_cast<size_t>(this->peek_at(entry_offset)) };
				const auto entry_base{ static_cast<size_t>(psdpimg::psb::obj_type_t::number_n8) };
				const auto entry_bytes{ entry_byte >= entry_base ? entry_byte - entry_base : 0 };
				const auto payload{ static_cast<uint32_t>(count * entry_bytes) };

				return entry_offset + 1 + payload;
			}

			auto read_array(psdpimg::common::byte_reader& reader, const uint32_t offset) -> std::vector<uint32_t>
			{
				const auto width{ this->count_bytes_at(offset) };

				if (width == 0)
				{
					return {};
				}

				reader.seek(offset);
				reader.skip(1);

				const auto count{ static_cast<uint64_t>(this->read_uint_le(reader, width)) };
				const auto entry_byte{ static_cast<size_t>(reader.read<uint8_t>()) };
				const auto entry_base{ static_cast<size_t>(psdpimg::psb::obj_type_t::number_n8) };
				const auto entry_bytes{ entry_byte >= entry_base ? entry_byte - entry_base : 0 };

				std::vector<uint32_t> values{};
				values.reserve(static_cast<size_t>(count));

				for (uint64_t i{}; i < count; ++i)
				{
					values.push_back(static_cast<uint32_t>(this->read_uint_le(reader, entry_bytes)));
				}

				return values;
			}

			auto read_uint_le(psdpimg::common::byte_reader& reader, const size_t bytes) -> uint64_t
			{
				const auto view{ reader.read_span(bytes) };

				uint64_t value{};

				for (size_t i{}; i < bytes; ++i)
				{
					value |= static_cast<uint64_t>(view[i]) << (8 * i);
				}

				return value;
			}

			auto read_trie_names(psdpimg::common::byte_reader& reader, const uint32_t offset)
				-> std::vector<std::string>
			{
				const auto charset_offset{ offset };
				const auto tree_offset{ this->array_end(charset_offset) };
				const auto index_offset{ this->array_end(tree_offset) };

				const auto charset{ this->read_array(reader, charset_offset) };
				const auto tree{ this->read_array(reader, tree_offset) };
				const auto indexes{ this->read_array(reader, index_offset) };

				std::vector<std::string> names{};
				names.reserve(indexes.size());

				for (const auto start : indexes)
				{
					const bool root_overflow{ start >= tree.size() };

					if (root_overflow)
					{
						throw std::runtime_error{ "psb_parser::read_trie_names: index out of range" };
					}

					std::string text{};
					uint32_t    cursor{ tree[start] };

					while (cursor != 0)
					{
						const bool tree_overflow{ cursor >= tree.size() };

						if (tree_overflow)
						{
							throw std::runtime_error{ "psb_parser::read_trie_names: index out of range" };
						}

						const auto parent{ tree[cursor] };
						const bool charset_overflow{ parent >= charset.size() };

						if (charset_overflow)
						{
							throw std::runtime_error{ "psb_parser::read_trie_names: parent out of range" };
						}

						text.push_back(static_cast<char>(static_cast<uint8_t>(cursor - charset[parent])));
						cursor = parent;
					}

					std::ranges::reverse(text);
					names.push_back(std::move(text));
				}

				return names;
			}

			auto read_plain_names(psdpimg::common::byte_reader& reader, const uint32_t offset)
				-> std::vector<std::string>
			{
				const auto indexes{ this->read_array(reader, offset) };

				std::vector<std::string> names{};
				names.reserve(indexes.size());

				for (const auto index : indexes)
				{
					names.push_back(reader.c_string(offset + index));
				}

				return names;
			}

			auto read_string(const uint32_t index) const -> std::string
			{
				const bool out_of_range{ index >= this->m_string_offsets.size() };

				if (out_of_range)
				{
					throw std::runtime_error{ "psb_parser::read_string: index out of range" };
				}

				const auto base{ static_cast<size_t>(this->m_string_data_offset) };
				const auto start{ base + this->m_string_offsets[index] };

				std::string text{};

				for (auto i{ start }; i < this->m_bytes.size(); ++i)
				{
					const auto ch{ this->m_bytes[i] };

					if (ch == 0)
					{
						return text;
					}

					text.push_back(static_cast<char>(ch));
				}

				throw std::runtime_error{ "psb_parser::read_string: missing terminator" };
			}

			auto read_chunk(const uint32_t index) const -> std::vector<uint8_t>
			{
				const bool bad_offset{ index >= this->m_chunk_offsets.size() };
				const bool bad_length{ index >= this->m_chunk_lengths.size() };
				const bool out_of_range{ bad_offset || bad_length };

				if (out_of_range)
				{
					throw std::runtime_error{ "psb_parser::read_chunk: index out of range" };
				}

				const auto base{ static_cast<size_t>(this->m_chunk_data_offset) };
				const auto start{ base + this->m_chunk_offsets[index] };
				const auto length{ static_cast<size_t>(this->m_chunk_lengths[index]) };

				const bool overflow{ start + length > this->m_bytes.size() };

				if (overflow)
				{
					throw std::runtime_error{ "psb_parser::read_chunk: payload exceeds file size" };
				}

				const auto first{ this->m_bytes.begin() + static_cast<ptrdiff_t>(start) };
				const auto last{ this->m_bytes.begin() + static_cast<ptrdiff_t>(start + length) };

				return std::vector<uint8_t>{ first, last };
			}

			auto read_value(psdpimg::common::byte_reader& reader) -> value
			{
				const auto raw_type{ reader.read<uint8_t>() };

				if (raw_type <= static_cast<uint8_t>(psdpimg::psb::obj_type_t::true_value))
				{
					const bool is_true{ raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::true_value) };

					const bool is_empty
					{
						raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::null) ||
						raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::none)
					};

					if (is_empty)
					{
						return psdpimg::psb::value::make_null();
					}

					return psdpimg::psb::value::make_boolean(is_true);
				}

				const bool is_number
				{
					psdpimg::psb::in_range
					(
						raw_type,
						psdpimg::psb::obj_type_t::number_n0,
						psdpimg::psb::obj_type_t::number_n8
					)
				};

				if (is_number)
				{
					const auto bytes{ psdpimg::psb::offset_from(raw_type, psdpimg::psb::obj_type_t::number_n0) };
					const auto bits{ this->read_uint_le(reader, bytes) };

					return psdpimg::psb::value::make_integer(psdpimg::psb::sign_extend(bits, bytes));
				}

				const bool is_string
				{
					psdpimg::psb::in_range
					(
						raw_type,
						psdpimg::psb::obj_type_t::string_n1,
						psdpimg::psb::obj_type_t::string_n4
					)
				};

				if (is_string)
				{
					const auto bytes{ psdpimg::psb::offset_from(raw_type, psdpimg::psb::obj_type_t::string_n1) + 1 };
					const auto index{ static_cast<uint32_t>(this->read_uint_le(reader, bytes)) };

					return psdpimg::psb::value::make_string(this->read_string(index));
				}

				const bool is_resource
				{
					psdpimg::psb::in_range
					(
						raw_type,
						psdpimg::psb::obj_type_t::resource_n1,
						psdpimg::psb::obj_type_t::resource_n4
					)
				};

				if (is_resource)
				{
					const auto bytes{ psdpimg::psb::offset_from(raw_type, psdpimg::psb::obj_type_t::resource_n1) + 1 };
					const auto index{ static_cast<uint32_t>(this->read_uint_le(reader, bytes)) };

					return psdpimg::psb::value::make_resource(index, this->read_chunk(index));
				}

				const bool is_extra
				{
					psdpimg::psb::in_range
					(
						raw_type,
						psdpimg::psb::obj_type_t::extra_chunk_n1,
						psdpimg::psb::obj_type_t::extra_chunk_n4
					)
				};

				if (is_extra)
				{
					const auto base{ psdpimg::psb::obj_type_t::extra_chunk_n1 };
					const auto bytes{ psdpimg::psb::offset_from(raw_type, base) + 1 };
					const auto number{ static_cast<int64_t>(this->read_uint_le(reader, bytes)) };

					return psdpimg::psb::value::make_integer(number);
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::float0))
				{
					return psdpimg::psb::value::make_real(0.0);
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::float32))
				{
					return psdpimg::psb::value::make_real(static_cast<double>(reader.read<float>()));
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::float64))
				{
					return psdpimg::psb::value::make_real(reader.read<double>());
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::list))
				{
					return this->read_list(reader);
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::objects))
				{
					return this->read_objects(reader);
				}

				throw std::runtime_error{ "psb_parser::read_value: unsupported object type" };
			}

			auto read_list(psdpimg::common::byte_reader& reader) -> value
			{
				const auto offsets{ this->read_array(reader, static_cast<uint32_t>(reader.position())) };
				const auto base{ reader.position() };

				value result{ psdpimg::psb::value::make_list() };

				for (const auto offset : offsets)
				{
					reader.seek(base + offset);
					result.push_back(this->read_value(reader));
				}

				return result;
			}

			auto read_objects(psdpimg::common::byte_reader& reader) -> value
			{
				const auto name_indexes{ this->read_array(reader, static_cast<uint32_t>(reader.position())) };
				const auto offsets{ this->read_array(reader, static_cast<uint32_t>(reader.position())) };
				const auto base{ reader.position() };

				value result{ psdpimg::psb::value::make_dictionary() };

				for (size_t i{}; i < name_indexes.size() && i < offsets.size(); ++i)
				{
					const auto name_index{ name_indexes[i] };
					const bool name_overflow{ name_index >= this->m_names.size() };

					if (name_overflow)
					{
						continue;
					}

					reader.seek(base + offsets[i]);
					result.set(this->m_names[name_index], this->read_value(reader));
				}

				return result;
			}
		};
	}

	auto header_length(const uint16_t version) noexcept -> uint32_t
	{
		if (version < 3)
		{
			return { 40 };
		}

		if (version == 3)
		{
			return { 44 };
		}

		return { 56 };
	}

	auto read(const std::span<const uint8_t> bytes) -> archive
	{
		psb_parser parser{ bytes };

		return parser.parse();
	}
}
