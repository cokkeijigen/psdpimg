#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <psb_view.hpp>
#include <psb_format.hpp>

namespace psdpimg::psb
{
	namespace
	{
		struct number_array
		{
			std::span<const uint8_t> bytes{};
			size_t                   entry_bytes{};

			[[nodiscard]] inline auto size() const noexcept -> size_t
			{
				return size_t
				{
					this->entry_bytes == 0 ? 0 :
					this->bytes.size() / this->entry_bytes
				};
			}

			[[nodiscard]] inline auto at(const size_t index) const noexcept -> uint32_t
			{
				const uint8_t* item{ this->bytes.data() + index * this->entry_bytes };

				uint32_t value{};

				for (size_t i{}; i < this->entry_bytes; ++i)
				{
					value |= static_cast<uint32_t>(item[i]) << (8 * i);
				}

				return value;
			}
		};

		class psb_parser
		{
			std::span<const uint8_t>   m_bytes{};
			uint32_t                   m_string_data_offset{};
			uint32_t                   m_chunk_data_offset{};
			psdpimg::psb::number_array m_string_offsets{};
			psdpimg::psb::number_array m_chunk_offsets{};
			psdpimg::psb::number_array m_chunk_lengths{};
			std::vector<std::string>   m_names{};

		public:
			explicit psb_parser(std::span<const uint8_t> bytes) noexcept :
				m_bytes{ bytes }
			{
			}

			auto parse() -> psdpimg::psb::archive
			{
				const psdpimg::psb::view source{ this->m_bytes };

				if (!source.is_valid())
				{
					throw std::runtime_error{ "psb_parser::parse: not a PSB container" };
				}

				const psdpimg::psb::header* const info{ source.header() };

				archive result{};

				result.version = info->version;

				if (info->header_length != psdpimg::psb::header_length(result.version))
				{
					throw std::runtime_error{ "psb_parser::parse: unexpected header length" };
				}

				const std::vector<std::string> names
				{
					result.version == 1 ?
					this->read_plain_names(info->names) :
					this->read_trie_names(info->names)
				};

				this->m_string_data_offset = info->strings_data;
				this->m_string_offsets     = this->read_array(info->strings);
				this->m_names              = names;
				this->m_chunk_offsets      = this->read_array(info->chunk_offsets);
				this->m_chunk_lengths      = this->read_array(info->chunk_lengths);
				this->m_chunk_data_offset  = info->chunk_data;

				const uint8_t* cursor{ this->m_bytes.data() + info->entries };

				result.root = this->read_value(cursor);

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
					const uint8_t byte{ this->peek_at(offset + static_cast<uint32_t>(i)) };

					value |= static_cast<uint64_t>(byte) << (8 * i);
				}

				return value;
			}

			auto count_bytes_at(const uint32_t offset) const -> size_t
			{
				const uint8_t type_byte{ this->peek_at(offset) };
				const bool is_array
				{
					type_byte >= static_cast<uint8_t>(psdpimg::psb::obj_type_t::array_n1) &&
					type_byte <= static_cast<uint8_t>(psdpimg::psb::obj_type_t::array_n8)
				};

				if (!is_array)
				{
					return {};
				}

				const uint8_t anchor{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::array_n1) };

				return static_cast<size_t>(type_byte - anchor) + 1;
			}

			auto array_end(const uint32_t offset) const -> uint32_t
			{
				const size_t width{ this->count_bytes_at(offset) };

				if (width == 0)
				{
					return offset;
				}

				const uint64_t count{ this->peek_le(offset + 1, width) };
				const uint32_t entry_offset{ offset + 1 + static_cast<uint32_t>(width) };
				const size_t   entry_byte{ static_cast<size_t>(this->peek_at(entry_offset)) };
				const size_t   entry_base{ static_cast<size_t>(psdpimg::psb::obj_type_t::number_n8) };
				const size_t   entry_bytes{ entry_byte >= entry_base ? entry_byte - entry_base : 0 };
				const uint32_t payload{ static_cast<uint32_t>(count * entry_bytes) };

				return entry_offset + 1 + payload;
			}

			auto read_uint_le(const uint8_t*& cursor, const size_t bytes) -> uint64_t
			{
				uint64_t value{};

				for (size_t i{}; i < bytes; ++i)
				{
					value |= static_cast<uint64_t>(cursor[i]) << (8 * i);
				}

				cursor += bytes;

				return value;
			}

			auto read_array(const uint32_t offset) -> psdpimg::psb::number_array
			{
				const size_t width{ this->count_bytes_at(offset) };

				if (width == 0)
				{
					return {};
				}

				const uint8_t* cursor{ this->m_bytes.data() + offset + 1 };

				const uint64_t count{ this->read_uint_le(cursor, width) };
				const size_t   entry_byte{ static_cast<size_t>(*cursor) };
				const size_t   entry_base{ static_cast<size_t>(psdpimg::psb::obj_type_t::number_n8) };
				const size_t   entry_bytes{ entry_byte >= entry_base ? entry_byte - entry_base : 0 };

				++cursor;

				const std::span<const uint8_t> payload{ cursor, static_cast<size_t>(count) * entry_bytes };

				return { payload, entry_bytes };
			}

			auto read_trie_names(const uint32_t offset) -> std::vector<std::string>
			{
				const uint32_t charset_offset{ offset };
				const uint32_t tree_offset   { this->array_end(charset_offset) };
				const uint32_t index_offset  { this->array_end(tree_offset) };

				const psdpimg::psb::number_array charset{ this->read_array(charset_offset) };
				const psdpimg::psb::number_array tree   { this->read_array(tree_offset) };
				const psdpimg::psb::number_array indexes{ this->read_array(index_offset) };

				std::vector<std::string> names{};
				names.reserve(indexes.size());

				for (size_t i{}; i < indexes.size(); ++i)
				{
					const uint32_t start        { indexes.at(i) };
					const bool     root_overflow{ start >= tree.size() };

					if (root_overflow)
					{
						throw std::runtime_error{ "psb_parser::read_trie_names: index out of range" };
					}

					std::string text  {};
					uint32_t    cursor{ tree.at(start) };

					while (cursor != 0)
					{
						const bool tree_overflow{ cursor >= tree.size() };

						if (tree_overflow)
						{
							throw std::runtime_error{ "psb_parser::read_trie_names: index out of range" };
						}

						const uint32_t parent          { tree.at(cursor) };
						const bool     charset_overflow{ parent >= charset.size() };

						if (charset_overflow)
						{
							throw std::runtime_error{ "psb_parser::read_trie_names: parent out of range" };
						}

						const uint8_t key{ static_cast<uint8_t>(cursor - charset.at(parent)) };

						text.push_back(static_cast<char>(key));
						cursor = parent;
					}

					std::ranges::reverse(text);
					names.push_back(std::move(text));
				}

				return names;
			}

			auto read_plain_names(const uint32_t offset) -> std::vector<std::string>
			{
				const psdpimg::psb::number_array indexes{ this->read_array(offset) };

				std::vector<std::string> names{};
				names.reserve(indexes.size());

				for (size_t i{}; i < indexes.size(); ++i)
				{
					const uint32_t index{ indexes.at(i) };

					const uint8_t* const first{ this->m_bytes.data() + offset + index };
					const uint8_t* const last { this->m_bytes.data() + this->m_bytes.size() };
					const uint8_t* const stop { std::find(first, last, uint8_t{}) };

					if (stop == last)
					{
						throw std::runtime_error{ "psb_parser::read_plain_names: missing terminator" };
					}

					const char* const text{ reinterpret_cast<const char*>(first) };

					names.emplace_back(text, static_cast<size_t>(stop - first));
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

				const auto start{ static_cast<size_t>(this->m_string_data_offset) + this->m_string_offsets.at(index) };
				const uint8_t* const first{ this->m_bytes.data() + start };
				const uint8_t* const last { this->m_bytes.data() + this->m_bytes.size() };
				const uint8_t* const stop { std::find(first, last, uint8_t{}) };

				if (stop == last)
				{
					throw std::runtime_error{ "psb_parser::read_string: missing terminator" };
				}

				const char* const text{ reinterpret_cast<const char*>(first) };

				return { text, static_cast<size_t>(stop - first) };
			}

			auto read_chunk(const uint32_t index) const -> std::vector<uint8_t>
			{
				const bool out_of_range
				{
					index >= this->m_chunk_offsets.size() ||
					index >= this->m_chunk_lengths.size()
				};

				if (out_of_range)
				{
					throw std::runtime_error{ "psb_parser::read_chunk: index out of range" };
				}

				const auto base   { static_cast<size_t>(this->m_chunk_data_offset) };
				const auto start  { base + this->m_chunk_offsets.at(index) };
				const auto length { static_cast<size_t>(this->m_chunk_lengths.at(index)) };

				const bool overflow{ start + length > this->m_bytes.size() };

				if (overflow)
				{
					throw std::runtime_error{ "psb_parser::read_chunk: payload exceeds file size" };
				}

				const uint8_t* const first{ this->m_bytes.data() + start };

				return { first, first + length };
			}

			auto read_value(const uint8_t*& cursor) -> psdpimg::psb::value
			{
				const uint8_t raw_type{ *cursor };

				++cursor;

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
					raw_type >= static_cast<uint8_t>(psdpimg::psb::obj_type_t::number_n0) &&
					raw_type <= static_cast<uint8_t>(psdpimg::psb::obj_type_t::number_n8)
				};

				if (is_number)
				{
					const uint8_t  anchor{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::number_n0) };
					const size_t   bytes { static_cast<size_t>(raw_type - anchor) };
					const uint64_t bits  { this->read_uint_le(cursor, bytes) };
					const bool     narrow{ bytes != 0 && bytes < 8 };
					const uint64_t marker{ narrow ? uint64_t{ 1 } << (bytes * 8 - 1) : 0 };

					return psdpimg::psb::value::make_integer(static_cast<int64_t>((bits ^ marker) - marker));
				}

				const bool is_string
				{
					raw_type >= static_cast<uint8_t>(psdpimg::psb::obj_type_t::string_n1) &&
					raw_type <= static_cast<uint8_t>(psdpimg::psb::obj_type_t::string_n4)
				};

				if (is_string)
				{
					const uint8_t  anchor{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::string_n1) };
					const size_t   bytes { static_cast<size_t>(raw_type - anchor) + 1 };
					const uint32_t index { static_cast<uint32_t>(this->read_uint_le(cursor, bytes)) };

					return psdpimg::psb::value::make_string(this->read_string(index));
				}

				const bool is_resource
				{
					raw_type >= static_cast<uint8_t>(psdpimg::psb::obj_type_t::resource_n1) &&
					raw_type <= static_cast<uint8_t>(psdpimg::psb::obj_type_t::resource_n4)
				};

				if (is_resource)
				{
					const uint8_t  anchor{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::resource_n1) };
					const size_t   bytes { static_cast<size_t>(raw_type - anchor) + 1 };
					const uint32_t index { static_cast<uint32_t>(this->read_uint_le(cursor, bytes)) };

					return psdpimg::psb::value::make_resource(index, this->read_chunk(index));
				}

				const bool is_extra
				{
					raw_type >= static_cast<uint8_t>(psdpimg::psb::obj_type_t::extra_chunk_n1) &&
					raw_type <= static_cast<uint8_t>(psdpimg::psb::obj_type_t::extra_chunk_n4)
				};

				if (is_extra)
				{
					const uint8_t  anchor{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::extra_chunk_n1) };
					const size_t   bytes { static_cast<size_t>(raw_type - anchor) + 1 };
					const int64_t  number{ static_cast<int64_t>(this->read_uint_le(cursor, bytes)) };

					return psdpimg::psb::value::make_integer(number);
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::float0))
				{
					return psdpimg::psb::value::make_real(0.0);
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::float32))
				{
					const float number{ *reinterpret_cast<const float*>(cursor) };

					cursor += sizeof(number);

					return psdpimg::psb::value::make_real(static_cast<double>(number));
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::float64))
				{
					const double number{ *reinterpret_cast<const double*>(cursor) };

					cursor += sizeof(number);

					return psdpimg::psb::value::make_real(number);
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::list))
				{
					return this->read_list(cursor);
				}

				if (raw_type == static_cast<uint8_t>(psdpimg::psb::obj_type_t::objects))
				{
					return this->read_objects(cursor);
				}

				throw std::runtime_error{ "psb_parser::read_value: unsupported object type" };
			}

			auto read_list(const uint8_t*& cursor) -> psdpimg::psb::value
			{
				const uint32_t                   position{ static_cast<uint32_t>(cursor - this->m_bytes.data()) };
				const psdpimg::psb::number_array offsets { this->read_array(position) };

				const uint8_t* const base{ this->m_bytes.data() + this->array_end(position) };

				value result{ psdpimg::psb::value::make_list() };

				for (size_t i{}; i < offsets.size(); ++i)
				{
					const uint8_t* element{ base + offsets.at(i) };

					result.push_back(this->read_value(element));
				}

				return result;
			}

			auto read_objects(const uint8_t*& cursor) -> psdpimg::psb::value
			{
				const uint32_t                   name_position{ static_cast<uint32_t>(cursor - this->m_bytes.data()) };
				const psdpimg::psb::number_array name_indexes { this->read_array(name_position) };
				const uint32_t                   offset_base  { this->array_end(name_position) };
				const psdpimg::psb::number_array offsets      { this->read_array(offset_base) };

				const uint8_t* const base{ this->m_bytes.data() + this->array_end(offset_base) };

				value result{ psdpimg::psb::value::make_dictionary() };

				for (size_t i{}; i < name_indexes.size() && i < offsets.size(); ++i)
				{
					const uint32_t name_index   { name_indexes.at(i) };
					const bool     name_overflow{ name_index >= this->m_names.size() };

					if (name_overflow)
					{
						continue;
					}

					const uint8_t* element{ base + offsets.at(i) };

					result.set(this->m_names[name_index], this->read_value(element));
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
