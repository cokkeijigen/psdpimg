#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <xmem.hpp>
#include <psb_format.hpp>

namespace psdpimg::psb
{
	namespace
	{
		static auto adler32(const std::span<const uint8_t> data) noexcept -> uint32_t
		{
			constexpr uint32_t modulus { 65521 };
			uint32_t           a       { 1 };
			uint32_t           b       {};

			for (const auto byte : data)
			{
				a = (a + byte) % modulus;
				b = (b + a) % modulus;
			}

			return (b << 16) | a;
		}

		static inline auto byte_width(const uint64_t value) noexcept -> size_t
		{
			if (value <= 0xFF)
			{
				return { 1 };
			}

			if (value <= 0xFFFF)
			{
				return { 2 };
			}

			if (value <= 0xFFFFFF)
			{
				return { 3 };
			}

			if (value <= 0xFFFFFFFF)
			{
				return { 4 };
			}

			return { 8 };
		}

		struct trie_node
		{
			uint32_t                                 id{};
			uint8_t                                  character{};
			std::vector<std::pair<uint8_t, int32_t>> children{};
		};

		struct trie_arrays
		{
			std::vector<uint32_t> charset{};
			std::vector<uint32_t> tree{};
			std::vector<uint32_t> indexes{};
		};

		class trie_builder
		{
			std::vector<trie_node> m_nodes{};
			std::vector<uint32_t>  m_node_names{};
			std::vector<int32_t>   m_terminals{};
			std::vector<uint32_t>  m_charset{};
			std::vector<uint32_t>  m_tree{};
			std::vector<uint8_t>   m_occupancy{};
			uint32_t               m_first_vacant{};

		public:
			trie_builder()
			{
				this->m_nodes.emplace_back();
				this->m_node_names.push_back(0);
			}

			auto insert(const std::string_view name) -> void
			{
				const auto name_index{ static_cast<uint32_t>(this->m_terminals.size()) };

				int32_t current{ 0 };

				for (const auto raw : name)
				{
					current = this->make_child(current, static_cast<uint8_t>(raw));
				}

				const int32_t terminal{ this->make_child(current, 0) };

				this->m_terminals.push_back(terminal);
				this->m_node_names[static_cast<size_t>(terminal)] = name_index + 1;
			}

			auto build() -> psdpimg::psb::trie_arrays
			{
				this->m_charset.push_back(1);
				this->m_occupancy.push_back(1);
				this->m_first_vacant = 1;

				this->process_optimized(0);

				trie_arrays result{};
				result.charset = this->m_charset;
				result.tree    = this->m_tree;
				result.indexes.reserve(this->m_terminals.size());

				for (const auto terminal : this->m_terminals)
				{
					result.indexes.push_back(this->m_nodes[static_cast<size_t>(terminal)].id);
				}

				return result;
			}

		private:

			[[nodiscard]] auto make_child(const int32_t parent, const uint8_t character) -> int32_t
			{
				for (const auto& child : this->m_nodes[static_cast<size_t>(parent)].children)
				{
					if (child.first == character)
					{
						return child.second;
					}
				}

				trie_node        node{};
				node.character = character;

				const auto index{ static_cast<int32_t>(this->m_nodes.size()) };

				this->m_nodes.push_back(node);
				this->m_node_names.push_back(0);

				auto& children{ this->m_nodes[static_cast<size_t>(parent)].children };

				children.emplace_back(character, index);

				std::ranges::sort
				(
					children,
					[](const auto& left, const auto& right) -> bool
					{
						return left.first < right.first;
					}
				);

				return index;
			}

			auto set_tree(const uint32_t index, const uint32_t content) -> void
			{
				if (this->m_tree.size() <= index)
				{
					this->m_tree.resize(static_cast<size_t>(index) + 1, 0);
				}

				this->m_tree[index] = content;
			}

			auto set_charset(const uint32_t index, const uint32_t content) -> void
			{
				if (this->m_charset.size() <= index)
				{
					this->m_charset.resize(static_cast<size_t>(index) + 1, 0);
				}

				this->m_charset[index] = content;
			}

			auto expand_occupancy(const uint32_t target) -> void
			{
				while (this->m_occupancy.size() <= target)
				{
					this->m_occupancy.push_back(0);
				}
			}

			auto mark_occupied(const uint32_t index) -> void
			{
				if (index >= this->m_occupancy.size())
				{
					this->expand_occupancy(index);
				}

				this->m_occupancy[index] = 1;
			}

			auto advance_first_vacant() noexcept -> void
			{
				while (this->m_first_vacant < this->m_occupancy.size() &&
					  this->m_occupancy[this->m_first_vacant] != 0)
				{
					++this->m_first_vacant;
				}
			}

			[[nodiscard]] auto probe_free_window(const uint32_t minimum_slot,
				const std::vector<uint32_t>& range, bool& need_extending) const -> uint32_t
			{
				need_extending = false;

				const auto limit{ static_cast<uint32_t>(this->m_occupancy.size()) };

				for (uint32_t candidate{ minimum_slot }; candidate < limit; ++candidate)
				{
					bool all_free{ true };

					for (const auto offset : range)
					{
						const uint32_t target{ candidate + offset };

						if (target >= limit)
						{
							need_extending = true;

							break;
						}

						const bool taken{ this->m_occupancy[target] != 0 };

						if (taken)
						{
							all_free = false;

							break;
						}
					}

					if (all_free)
					{
						return candidate;
					}
				}

				need_extending = true;

				return std::max(limit, minimum_slot);
			}

			auto process_optimized(const uint32_t index) -> void
			{
				auto& node{ this->m_nodes[index] };

				if (node.children.empty())
				{
					return;
				}

				const auto            minimum { static_cast<uint32_t>(node.children.front().first) };
				std::vector<uint32_t> offsets {};

				offsets.reserve(node.children.size());

				for (const auto& child : node.children)
				{
					offsets.push_back(static_cast<uint32_t>(child.first) - minimum);
				}

				const uint32_t minimum_slot{ std::max(this->m_first_vacant, minimum + 1) };
				bool           need_extending{};

				const uint32_t begin{ this->probe_free_window(minimum_slot, offsets, need_extending) };

				if (need_extending)
				{
					this->expand_occupancy(begin + offsets.back());
				}

				for (size_t i{}; i < node.children.size(); ++i)
				{
					const int32_t  child_index{ node.children[i].second };
					auto&          child      { this->m_nodes[static_cast<size_t>(child_index)] };
					const uint32_t position   { begin + offsets[i] };

					child.id = position;

					this->set_tree(position, node.id);
					this->mark_occupied(position);

					if (child.character != 0)
					{
						continue;
					}

					const uint32_t stored{ this->m_node_names[static_cast<size_t>(child_index)] };

					if (stored != 0)
					{
						this->set_charset(position, stored - 1);
					}
				}

				this->set_charset(node.id, begin - minimum);
				this->advance_first_vacant();

				for (const auto& child : node.children)
				{
					const auto child_index { static_cast<uint32_t>(child.second) };
					const bool terminal    { this->m_nodes[child_index].character == 0 };

					if (terminal)
					{
						continue;
					}

					this->process_optimized(child_index);
				}
			}
		};

		static auto encode_array(const std::vector<uint32_t>& values) -> std::vector<uint8_t>
		{
			xmem::buffer<uint8_t> writer{};

			const size_t count_width{ psdpimg::psb::byte_width(values.size()) };
			uint32_t     maximum{};

			for (const auto value : values)
			{
				maximum = std::max(maximum, value);
			}

			const auto entry_width{ values.empty() ? size_t{ 1 } : psdpimg::psb::byte_width(maximum) };

			const uint8_t array_tag{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::array_n1) };
			const uint8_t tag      { static_cast<uint8_t>(array_tag + count_width - 1) };

			writer.write(tag);

			for (size_t i{}; i < count_width; ++i)
			{
				const auto slice{ static_cast<uint8_t>((values.size() >> (8 * i)) & 0xFF) };

				writer.write(slice);
			}

			const uint8_t number_tag{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::number_n8) };
			const uint8_t entry_tag { static_cast<uint8_t>(number_tag + entry_width) };

			writer.write(entry_tag);

			for (const auto value : values)
			{
				for (size_t i{}; i < entry_width; ++i)
				{
					const auto slice{ static_cast<uint8_t>((value >> (8 * i)) & 0xFF) };

					writer.write(slice);
				}
			}

			const std::span<uint8_t> result{ writer.written() };

			return { result.begin(), result.end() };
		}

		class psb_compiler
		{
			const psdpimg::psb::archive&      m_source;
			std::vector<std::string>          m_names{};
			std::vector<std::string>          m_strings{};
			std::vector<std::vector<uint8_t>> m_chunks{};
			std::vector<uint32_t>             m_chunk_lengths{};

		public:
			explicit psb_compiler(const archive& source) noexcept :
				m_source{ source }
			{
			}

			auto compile() -> std::vector<uint8_t>
			{
				this->collect(this->m_source.root);

				const psdpimg::psb::trie_arrays trie         { this->build_trie() };
				const std::vector<uint8_t>     names_charset{ psdpimg::psb::encode_array(trie.charset) };
				const std::vector<uint8_t>     names_tree   { psdpimg::psb::encode_array(trie.tree) };
				const std::vector<uint8_t>     names_indexes{ psdpimg::psb::encode_array(trie.indexes) };

				const std::vector<uint8_t> entries{ this->encode_value(this->m_source.root) };

				std::vector<uint32_t> string_offsets {};
				std::string           string_data    {};

				for (const auto& text : this->m_strings)
				{
					string_offsets.push_back(static_cast<uint32_t>(string_data.size()));
					string_data.append(text);
					string_data.push_back('\0');
				}

				std::vector<uint32_t> chunk_offsets {};
				uint32_t              chunk_total   {};

				for (const auto& blob : this->m_chunks)
				{
					chunk_offsets.push_back(chunk_total);
					chunk_total += static_cast<uint32_t>(blob.size());
				}

				const std::vector<uint8_t> string_array      { psdpimg::psb::encode_array(string_offsets) };
				const std::vector<uint8_t> chunk_offset_array{ psdpimg::psb::encode_array(chunk_offsets) };
				const std::vector<uint8_t> chunk_length_array{ psdpimg::psb::encode_array(this->m_chunk_lengths) };

				const uint16_t version  { this->m_source.version };
				const uint32_t head_size{ psdpimg::psb::header_length(version) };

				const size_t   charset_size      { names_charset.size() };
				const size_t   tree_size         { names_tree.size() };
				const size_t   indexes_size      { names_indexes.size() };
				const uint32_t names_size        { static_cast<uint32_t>(charset_size + tree_size + indexes_size) };
				const uint32_t entries_size      { static_cast<uint32_t>(entries.size()) };
				const uint32_t strings_size      { static_cast<uint32_t>(string_array.size()) };
				const uint32_t strings_data_size { static_cast<uint32_t>(string_data.size()) };
				const uint32_t chunk_offsets_size{ static_cast<uint32_t>(chunk_offset_array.size()) };
				const uint32_t chunk_lengths_size{ static_cast<uint32_t>(chunk_length_array.size()) };

				const uint32_t offset_names         { head_size };
				const uint32_t offset_entries       { offset_names + names_size };
				const uint32_t offset_strings       { offset_entries + entries_size };
				const uint32_t offset_strings_data  { offset_strings + strings_size };
				const uint32_t offset_chunk_offsets { offset_strings_data + strings_data_size };
				const uint32_t offset_chunk_lengths { offset_chunk_offsets + chunk_offsets_size };
				const uint32_t offset_chunk_data    { offset_chunk_lengths + chunk_lengths_size };

				xmem::buffer<uint8_t> writer{};

				writer.resize(offset_chunk_data + chunk_total + 16);

				writer.write(psdpimg::psb::magic_psb);
				writer.write(reinterpret_cast<const uint8_t*>(&version), sizeof(version));
				writer.memset(writer.count(), uint8_t{}, 2);
				writer.write(reinterpret_cast<const uint8_t*>(&head_size), sizeof(head_size));
				writer.write(reinterpret_cast<const uint8_t*>(&offset_names), sizeof(offset_names));
				writer.write(reinterpret_cast<const uint8_t*>(&offset_strings), sizeof(offset_strings));
				writer.write(reinterpret_cast<const uint8_t*>(&offset_strings_data), sizeof(offset_strings_data));
				writer.write(reinterpret_cast<const uint8_t*>(&offset_chunk_offsets), sizeof(offset_chunk_offsets));
				writer.write(reinterpret_cast<const uint8_t*>(&offset_chunk_lengths), sizeof(offset_chunk_lengths));
				writer.write(reinterpret_cast<const uint8_t*>(&offset_chunk_data), sizeof(offset_chunk_data));
				writer.write(reinterpret_cast<const uint8_t*>(&offset_entries), sizeof(offset_entries));

				const size_t checksum_position{ writer.count() };

				writer.memset(writer.count(), uint8_t{}, 4);

				if (version > 3)
				{
					writer.memset(writer.count(), uint8_t{}, 12);
				}

				this->write_checksum(writer, checksum_position, version);

				writer.write(std::span<const uint8_t>{ names_charset });
				writer.write(std::span<const uint8_t>{ names_tree });
				writer.write(std::span<const uint8_t>{ names_indexes });
				writer.write(std::span<const uint8_t>{ entries });
				writer.write(std::span<const uint8_t>{ string_array });

				for (const auto character : string_data)
				{
					writer.write(static_cast<uint8_t>(character));
				}

				writer.write(std::span<const uint8_t>{ chunk_offset_array });
				writer.write(std::span<const uint8_t>{ chunk_length_array });

				for (const auto& blob : this->m_chunks)
				{
					writer.write(std::span<const uint8_t>{ blob });
				}

				const std::span<uint8_t> result{ writer.written() };

				return { result.begin(), result.end() };
			}

		private:

			static auto write_checksum(xmem::buffer<uint8_t>& writer, const size_t position, const uint16_t version) -> void
			{
				const std::span<uint8_t> bytes{ writer.written() };
				const size_t             begin{ 8 };
				const size_t             end  { position };
				const auto               first { bytes.begin() + static_cast<ptrdiff_t>(begin) };
				const auto               last  { bytes.begin() + static_cast<ptrdiff_t>(end) };

				std::vector<uint8_t> covered{ first, last };

				if (version > 3)
				{
					const size_t extra_begin { position + 4 };
					const auto   extra_first { bytes.begin() + static_cast<ptrdiff_t>(extra_begin) };
					const auto   extra_last  { bytes.begin() + static_cast<ptrdiff_t>(extra_begin + 12) };

					covered.insert(covered.end(), extra_first, extra_last);
				}

				const uint32_t checksum{ psdpimg::psb::adler32(covered) };

				writer.write(position, reinterpret_cast<const uint8_t*>(&checksum), sizeof(checksum));
			}

			auto build_trie() -> psdpimg::psb::trie_arrays
			{
				trie_builder builder{};

				for (const auto& name : this->m_names)
				{
					builder.insert(name);
				}

				return builder.build();
			}

			auto name_index(const std::string_view key) -> uint32_t
			{
				for (size_t i{}; i < this->m_names.size(); ++i)
				{
					if (this->m_names[i] == key)
					{
						return static_cast<uint32_t>(i);
					}
				}

				this->m_names.emplace_back(key);

				return static_cast<uint32_t>(this->m_names.size() - 1);
			}

			auto string_index(const std::string& text) -> uint32_t
			{
				for (size_t i{}; i < this->m_strings.size(); ++i)
				{
					if (this->m_strings[i] == text)
					{
						return static_cast<uint32_t>(i);
					}
				}

				this->m_strings.push_back(text);

				return static_cast<uint32_t>(this->m_strings.size() - 1);
			}

			auto chunk_index(const std::vector<uint8_t>& blob) -> uint32_t
			{
				for (size_t i{}; i < this->m_chunks.size(); ++i)
				{
					if (this->m_chunks[i] == blob)
					{
						return static_cast<uint32_t>(i);
					}
				}

				this->m_chunks.push_back(blob);
				this->m_chunk_lengths.push_back(static_cast<uint32_t>(blob.size()));

				return static_cast<uint32_t>(this->m_chunks.size() - 1);
			}

			auto collect(const psdpimg::psb::value& node) -> void
			{
				switch (node.kind())
				{
				case psdpimg::psb::value_kind_t::string:
				{
					this->string_index(node.as_string());

					return;
				}
				case psdpimg::psb::value_kind_t::resource:
				{
					this->chunk_index(node.as_resource());

					return;
				}
				case psdpimg::psb::value_kind_t::list:
				{
					for (const auto& item : node.items())
					{
						this->collect(item);
					}

					return;
				}
				case psdpimg::psb::value_kind_t::dictionary:
				{
					for (size_t i{}; i < node.keys().size(); ++i)
					{
						this->name_index(node.keys()[i]);
						this->collect(node.items()[i]);
					}

					return;
				}
				default:
				{
					return;
				}
				};
			}

			auto encode_value(const psdpimg::psb::value& node) -> std::vector<uint8_t>
			{
				xmem::buffer<uint8_t> writer{};

				switch (node.kind())
				{
				case psdpimg::psb::value_kind_t::null:
				{
					const psdpimg::psb::obj_type_t tag
					{ 
						psdpimg::psb::obj_type_t::null
					};
					writer.write(static_cast<uint8_t>(tag));
					break;
				}
				case psdpimg::psb::value_kind_t::boolean:
				{
					const psdpimg::psb::obj_type_t tag
					{
						node.as_boolean() ?
						psdpimg::psb::obj_type_t::true_value :
						psdpimg::psb::obj_type_t::false_value
					};

					writer.write(static_cast<uint8_t>(tag));

					break;
				}
				case psdpimg::psb::value_kind_t::integer:
				{
					psdpimg::psb::psb_compiler::encode_integer(writer, node.as_integer());

					break;
				}
				case psdpimg::psb::value_kind_t::real:
				{
					psdpimg::psb::psb_compiler::encode_real(writer, node.as_real());

					break;
				}
				case psdpimg::psb::value_kind_t::string:
				{
					const uint32_t index{ this->string_index(node.as_string()) };
					const size_t   width{ psdpimg::psb::byte_width(index)      };
					const uint8_t string_tag{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::string_n1) };
					const uint8_t        tag{ static_cast<uint8_t>(string_tag + width - 1) };

					writer.write(tag);

					for (size_t i{}; i < width; ++i)
					{
						const auto slice{ static_cast<uint8_t>((index >> (8 * i)) & 0xFF) };

						writer.write(slice);
					}

					break;
				}
				case psdpimg::psb::value_kind_t::resource:
				{
					const uint32_t index { this->chunk_index(node.as_resource()) };
					const size_t   width { psdpimg::psb::byte_width(index) };
					const uint8_t resource_tag{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::resource_n1) };
					const uint8_t tag         { static_cast<uint8_t>(resource_tag + width - 1) };

					writer.write(tag);

					for (size_t i{}; i < width; ++i)
					{
						const auto slice{ static_cast<uint8_t>((index >> (8 * i)) & 0xFF) };

						writer.write(slice);
					}

					break;
				}
				case psdpimg::psb::value_kind_t::list:
				{
					const psdpimg::psb::obj_type_t tag{ psdpimg::psb::obj_type_t::list };
					writer.write(static_cast<uint8_t>(tag));

					this->encode_container(writer, node.items(), {}, false, {});

					break;
				}
				case psdpimg::psb::value_kind_t::dictionary:
				{
					std::vector<uint32_t> names{};
					names.reserve(node.keys().size());

					for (const auto& key : node.keys())
					{
						names.push_back(this->name_index(key));
					}

					std::vector<size_t> order{};
					order.resize(names.size());

					for (size_t i{}; i < order.size(); ++i)
					{
						order[i] = i;
					}

					std::ranges::stable_sort
					(
						order,
						[&names](const size_t left, const size_t right) -> bool
						{
							return names[left] < names[right];
						}
					);

					const auto tag{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::objects) };

					writer.write(tag);
					this->encode_container(writer, node.items(), names, true, order);

					break;
				}
				};

				const std::span<uint8_t> result{ writer.written() };

				return { result.begin(), result.end() };
			}

			auto encode_container(xmem::buffer<uint8_t>& writer, const std::vector<psdpimg::psb::value>& items,
				const std::vector<uint32_t>& names, const bool with_names,
				const std::vector<size_t>& order) -> void
			{
				std::vector<uint32_t>             emitted{};
				std::vector<std::vector<uint8_t>> encoded{};
				std::vector<uint32_t>             offsets{};

				uint32_t cursor{};

				for (size_t i{}; i < items.size(); ++i)
				{
					const size_t index{ order.empty() ? i : order[i] };

					if (with_names)
					{
						emitted.push_back(names[index]);
					}

					offsets.push_back(cursor);
					encoded.push_back(this->encode_value(items[index]));
					cursor += static_cast<uint32_t>(encoded.back().size());
				}

				if (with_names)
				{
					const std::vector<uint8_t> names_array{ psdpimg::psb::encode_array(emitted) };

					writer.write(std::span<const uint8_t>{ names_array });
				}

				const std::vector<uint8_t> offset_array{ psdpimg::psb::encode_array(offsets) };

				writer.write(std::span<const uint8_t>{ offset_array });

				for (const auto& bytes : encoded)
				{
					writer.write(std::span<const uint8_t>{ bytes });
				}
			}

			static auto signed_width(const int64_t number) noexcept -> size_t
			{
				const uint64_t magnitude
				{
					number < 0 ?
					static_cast<uint64_t>(-(number + 1)) + 1 :
					static_cast<uint64_t>(number)
				};

				for (size_t width{ 1 }; width < 8; ++width)
				{
					const uint64_t negative_limit { uint64_t{ 1 } << (width * 8 - 1) };
					const uint64_t positive_limit { negative_limit - 1 };
					const bool fits
					{
						number < 0 ? magnitude <= negative_limit : magnitude <= positive_limit
					};

					if (fits)
					{
						return width;
					}
				}

				return { 8 };
			}

			static inline auto encode_integer(xmem::buffer<uint8_t>& writer, const int64_t number) -> void
			{
				if (number == 0)
				{
					writer.write(static_cast<uint8_t>(psdpimg::psb::obj_type_t::number_n0));

					return;
				}

				const uint64_t bits { static_cast<uint64_t>(number) };
				const size_t   width{ psdpimg::psb::psb_compiler::signed_width(number) };

				const uint8_t number_tag{ static_cast<uint8_t>(psdpimg::psb::obj_type_t::number_n0) };
				const uint8_t tag       { static_cast<uint8_t>(number_tag + width) };

				writer.write(tag);

				for (size_t i{}; i < width; ++i)
				{
					const auto slice{ static_cast<uint8_t>((bits >> (8 * i)) & 0xFF) };

					writer.write(slice);
				}
			}

			static auto encode_real(xmem::buffer<uint8_t>& writer, const double number) -> void
			{
				const auto as_float { static_cast<float>(number) };
				const bool exact    { static_cast<double>(as_float) == number };

				if (exact)
				{
					writer.write(static_cast<uint8_t>(psdpimg::psb::obj_type_t::float32));

					uint32_t bits{};
					std::memcpy(&bits, &as_float, sizeof(bits));

					for (size_t i{}; i < 4; ++i)
					{
						const auto slice{ static_cast<uint8_t>((bits >> (8 * i)) & 0xFF) };

						writer.write(slice);
					}

					return;
				}

				writer.write(static_cast<uint8_t>(psdpimg::psb::obj_type_t::float64));
				writer.write(reinterpret_cast<const uint8_t*>(&number), sizeof(number));
			}
		};
	}

	auto write(const archive& source) -> std::vector<uint8_t>
	{
		psb_compiler compiler{ source };

		return compiler.compile();
	}
}
