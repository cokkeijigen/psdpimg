#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "pimg/pimg_convert.hpp"
#include "common/image.hpp"
#include "psb/psb_format.hpp"
#include "psb/psb_value.hpp"
#include "psd/psd_origin.hpp"
#include "tlg/tlg5.hpp"

namespace psdpimg::pimg
{
	namespace
	{
		constexpr int64_t          image_layer_type{ 0 };
		constexpr int64_t          group_layer_type{ 2 };
		constexpr int64_t          element_type{ 13 };
		constexpr int64_t          max_layer_id{ 65535 };
		constexpr int64_t          layer_id_minus_one{ -1 };
		constexpr int64_t          default_opacity{ 255 };
		constexpr uint16_t         target_psb_version{ 3 };
		constexpr std::string_view layer_id_marker{ "#lyid#" };
		constexpr std::string_view placeholder_marker{ "#lyid#-1" };
		constexpr uint8_t          shade_alpha{ 128 };
		constexpr std::string_view group_end_name{ "</Layer group>" };
		constexpr size_t           not_found{ std::numeric_limits<size_t>::max() };

		struct layer_spec
		{
			int64_t     layer_id{ layer_id_minus_one };
			int64_t     layer_type{ image_layer_type };
			int64_t     left{};
			int64_t     top{};
			int64_t     width{};
			int64_t     height{};
			int64_t     opacity{ default_opacity };
			bool        visible{ true };
			bool        has_group{};
			int64_t     group_layer_id{};
			bool        has_same{};
			int64_t     same_image{};
			std::string name{};
		};

		enum class record_kind_t : uint8_t
		{
			image,
			group_begin,
			group_end,
		};

		struct record_ref
		{
			record_kind_t kind{ psdpimg::pimg::record_kind_t::image };
			size_t        index{};
		};

		struct node
		{
			bool               group{};
			size_t             index{};
			size_t             order{};
			bool               root{};
			std::vector<node*> children{};
		};

		static auto resolve_order(node* item) -> size_t
		{
			if (!item->group || item->children.empty())
			{
				item->order = item->index;

				return item->order;
			}

			size_t best{ item->index };

			for (auto* const child : item->children)
			{
				best = std::min(best, psdpimg::pimg::resolve_order(child));
			}

			item->order = best;

			return best;
		}

		static auto index_order(const node* left, const node* right) noexcept -> bool
		{
			if (left->order != right->order)
			{
				return left->order < right->order;
			}

			return left->index < right->index;
		}

		static auto integer_of(const psdpimg::psb::value* item, const int64_t fallback) noexcept -> int64_t
		{
			if (item == nullptr)
			{
				return fallback;
			}

			if (item->kind() == psdpimg::psb::value_kind_t::integer)
			{
				return item->as_integer();
			}

			if (item->kind() == psdpimg::psb::value_kind_t::real)
			{
				return static_cast<int64_t>(item->as_real());
			}

			return fallback;
		}

		static auto string_of(const psdpimg::psb::value* item) -> std::string
		{
			const bool usable{ item != nullptr && item->kind() == psdpimg::psb::value_kind_t::string };

			return usable ? item->as_string() : std::string{};
		}

		static auto read_specs(const psdpimg::psb::value& layers) -> std::vector<layer_spec>
		{
			std::vector<layer_spec> result{};
			result.reserve(layers.items().size());

			for (const auto& item : layers.items())
			{
				if (item.kind() != psdpimg::psb::value_kind_t::dictionary)
				{
					continue;
				}

				layer_spec spec{};
				spec.layer_id   = psdpimg::pimg::integer_of(item.find("layer_id"), layer_id_minus_one);
				spec.layer_type = psdpimg::pimg::integer_of(item.find("layer_type"), image_layer_type);
				spec.left       = psdpimg::pimg::integer_of(item.find("left"), 0);
				spec.top        = psdpimg::pimg::integer_of(item.find("top"), 0);
				spec.width      = psdpimg::pimg::integer_of(item.find("width"), 0);
				spec.height     = psdpimg::pimg::integer_of(item.find("height"), 0);
				spec.opacity    = psdpimg::pimg::integer_of(item.find("opacity"), default_opacity);
				spec.visible    = psdpimg::pimg::integer_of(item.find("visible"), 1) != 0;
				spec.name       = psdpimg::pimg::string_of(item.find("name"));

				const auto* group{ item.find("group_layer_id") };
				const auto* same{ item.find("same_image") };

				spec.has_group      = group != nullptr;
				spec.group_layer_id = psdpimg::pimg::integer_of(group, 0);
				spec.has_same       = same != nullptr;
				spec.same_image     = psdpimg::pimg::integer_of(same, 0);

				result.push_back(std::move(spec));
			}

			return result;
		}

		static auto collect_resources(const psdpimg::psb::value& root)
			-> std::map<int64_t, const psdpimg::psb::value*>
		{
			std::map<int64_t, const psdpimg::psb::value*> result{};
			const auto& keys{ root.keys() };
			const auto& items{ root.items() };

			for (size_t i{}; i < keys.size() && i < items.size(); ++i)
			{
				if (items[i].kind() != psdpimg::psb::value_kind_t::resource)
				{
					continue;
				}

				const auto& key{ keys[i] };
				const auto dot{ key.find('.') };
				const bool usable{ dot != std::string::npos && dot != 0 };

				if (!usable)
				{
					continue;
				}

				int64_t identifier{};
				const auto* const begin{ key.data() };
				const auto* const end{ key.data() + dot };
				const auto parsed{ std::from_chars(begin, end, identifier) };
				const bool valid{ parsed.ec == std::errc{} && parsed.ptr == end };

				if (!valid)
				{
					continue;
				}

				result[identifier] = &items[i];
			}

			return result;
		}

		static auto order_siblings(node* item) -> void
		{
			std::ranges::sort(item->children, psdpimg::pimg::index_order);

			for (auto* const child : item->children)
			{
				if (child->group)
				{
					psdpimg::pimg::order_siblings(child);
				}
			}
		}

		class tree_builder
		{
			const std::vector<layer_spec>&     m_specs;
			std::vector<std::unique_ptr<node>> m_pool{};
			std::vector<node*>                 m_groups{};
			std::vector<node*>                 m_roots{};

		public:
			explicit tree_builder(const std::vector<layer_spec>& specs) noexcept :
				m_specs{ specs }
			{
			}

			auto build() -> std::vector<node*>
			{
				this->collect();

				for (auto* const item : this->m_roots)
				{
					psdpimg::pimg::resolve_order(item);
				}

				std::ranges::sort(this->m_roots, psdpimg::pimg::index_order);

				for (auto* const item : this->m_roots)
				{
					psdpimg::pimg::order_siblings(item);
				}

				return this->m_roots;
			}

		private:
			auto collect() -> void
			{
				for (size_t i{}; i < this->m_specs.size(); ++i)
				{
					const bool is_group{ this->m_specs[i].layer_type == group_layer_type };

					if (is_group)
					{
						auto* const group{ this->create_group(i, nullptr) };

						if (group != nullptr)
						{
							this->m_roots.push_back(group);
						}

						continue;
					}

					auto* const image{ this->make_node(false, i) };

					if (!this->m_specs[i].has_group)
					{
						image->root = true;
						this->m_roots.push_back(image);

						continue;
					}

					const auto parent{ this->find_group(this->m_specs[i].group_layer_id) };

					if (parent == not_found)
					{
						image->root = true;
						this->m_roots.push_back(image);

						continue;
					}

					auto* const created{ this->create_group(parent, image) };

					if (created != nullptr)
					{
						this->m_roots.push_back(created);
					}
				}
			}

			auto make_node(const bool is_group, const size_t index) -> node*
			{
				auto storage{ std::make_unique<node>() };

				storage->group = is_group;
				storage->index = index;

				this->m_pool.push_back(std::move(storage));

				return this->m_pool.back().get();
			}

			auto find_group(const int64_t identifier) const noexcept -> size_t
			{
				for (size_t i{}; i < this->m_specs.size(); ++i)
				{
					const bool is_group{ this->m_specs[i].layer_type == group_layer_type };
					const bool match{ this->m_specs[i].layer_id == identifier };

					if (is_group && match)
					{
						return i;
					}
				}

				return not_found;
			}

			auto create_group(const size_t index, node* child) -> node*
			{
				const auto identifier{ this->m_specs[index].layer_id };

				for (auto* const existing : this->m_groups)
				{
					if (this->m_specs[existing->index].layer_id != identifier)
					{
						continue;
					}

					if (child != nullptr)
					{
						existing->children.push_back(child);
					}

					return nullptr;
				}

				auto* const group{ this->make_node(true, index) };

				if (child != nullptr)
				{
					group->children.push_back(child);
				}

				this->m_groups.push_back(group);

				if (!this->m_specs[index].has_group)
				{
					return group;
				}

				const auto parent_identifier{ this->m_specs[index].group_layer_id };

				for (auto* const candidate : this->m_groups)
				{
					if (this->m_specs[candidate->index].layer_id == parent_identifier)
					{
						candidate->children.push_back(group);

						return nullptr;
					}
				}

				const auto parent{ this->find_group(parent_identifier) };

				if (parent == not_found)
				{
					return group;
				}

				return this->create_group(parent, group);
			}
		};

		static auto flatten(const node& item, std::vector<record_ref>& target) -> void
		{
			if (!item.group)
			{
				target.push_back({ psdpimg::pimg::record_kind_t::image, item.index });

				return;
			}

			target.push_back({ psdpimg::pimg::record_kind_t::group_begin, item.index });

			for (const auto* const child : item.children)
			{
				psdpimg::pimg::flatten(*child, target);
			}

			target.push_back({ psdpimg::pimg::record_kind_t::group_end, item.index });
		}

		class image_cache
		{
			const std::map<int64_t, const psdpimg::psb::value*>& m_resources;
			std::map<int64_t, psdpimg::common::image>            m_images{};

		public:
			explicit image_cache(const std::map<int64_t, const psdpimg::psb::value*>& resources) noexcept :
				m_resources{ resources }
			{
			}

			auto lookup(const layer_spec& spec) -> const psdpimg::common::image*
			{
				int64_t key{ spec.layer_id };

				if (this->m_resources.find(key) == this->m_resources.end() && spec.has_same)
				{
					key = spec.same_image;
				}

				const auto entry{ this->m_resources.find(key) };

				if (entry == this->m_resources.end())
				{
					return nullptr;
				}

				const auto cached{ this->m_images.find(key) };

				if (cached != this->m_images.end())
				{
					return &cached->second;
				}

				const auto& blob{ entry->second->as_resource() };

				if (!psdpimg::tlg::is_tlg(blob))
				{
					throw std::runtime_error{ "image_cache::lookup: resource is not a TLG image" };
				}

				const auto inserted{ this->m_images.emplace(key, psdpimg::tlg::decode(blob)) };

				return &inserted.first->second;
			}
		};

		static auto is_encodable_id(const int64_t identifier) noexcept -> bool
		{
			return identifier >= 0 && identifier <= max_layer_id;
		}

		static auto make_layer(const record_ref& item, const std::vector<layer_spec>& specs, image_cache& cache)
			-> psdpimg::psd::layer
		{
			psdpimg::psd::layer result{};
			const auto&         spec{ specs[item.index] };

			if (item.kind == psdpimg::pimg::record_kind_t::group_end)
			{
				result.section      = psdpimg::psd::section_t::divider;
				result.name         = std::string{ group_end_name };
				result.unicode_name = result.name;

				return result;
			}

			result.opacity = static_cast<uint8_t>(std::clamp<int64_t>(spec.opacity, 0, 255));
			result.visible = spec.visible;

			if (item.kind == psdpimg::pimg::record_kind_t::group_begin)
			{
				result.section      = psdpimg::psd::section_t::closed_folder;
				result.name         = spec.name;
				result.unicode_name = spec.name;

				if (psdpimg::pimg::is_encodable_id(spec.layer_id))
				{
					result.has_layer_id = true;
					result.layer_id     = static_cast<uint32_t>(spec.layer_id);
				}

				return result;
			}

			result.name = spec.name;

			if (psdpimg::pimg::is_encodable_id(spec.layer_id))
			{
				result.has_layer_id = true;
				result.layer_id     = static_cast<uint32_t>(spec.layer_id);
			}
			else if (spec.layer_id == layer_id_minus_one)
			{
				result.placeholder = true;
			}

			result.unicode_name = result.name;
			result.left         = static_cast<int32_t>(spec.left);
			result.top          = static_cast<int32_t>(spec.top);

			const auto* const picture{ cache.lookup(spec) };

			if (picture != nullptr)
			{
				result.right  = result.left + static_cast<int32_t>(picture->width);
				result.bottom = result.top + static_cast<int32_t>(picture->height);
				result.pixels = picture->pixels;

				return result;
			}

			const bool sized{ spec.width > 0 && spec.height > 0 };

			if (!sized)
			{
				return result;
			}

			const auto width{ static_cast<uint32_t>(spec.width) };
			const auto height{ static_cast<uint32_t>(spec.height) };

			result.right  = result.left + static_cast<int32_t>(width);
			result.bottom = result.top + static_cast<int32_t>(height);
			result.pixels.assign(static_cast<size_t>(width) * height * 4, 0);

			if (result.placeholder)
			{
				for (size_t i{ 3 }; i < result.pixels.size(); i += 4)
				{
					result.pixels[i] = shade_alpha;
				}
			}

			return result;
		}

		struct span_box
		{
			int32_t x0{};
			int32_t x1{};
			int32_t y0{};
			int32_t y1{};
		};

		static auto alpha_spans(const psdpimg::psd::layer& item) -> std::vector<span_box>
		{
			std::vector<span_box> boxes{};

			const int32_t width{ item.right - item.left };
			const int32_t height{ item.bottom - item.top };

			if (width <= 0 || height <= 0)
			{
				return boxes;
			}

			const size_t stride{ static_cast<size_t>(width) };
			const size_t row_bytes{ stride * 4 };

			if (item.pixels.size() < stride * static_cast<size_t>(height) * 4)
			{
				return boxes;
			}

			std::vector<span_box> open{};

			for (int32_t y{}; y < height; ++y)
			{
				const auto* const row{ item.pixels.data() + static_cast<size_t>(y) * row_bytes };
				std::vector<std::pair<int32_t, int32_t>> spans{};

				for (int32_t x{}; x < width;)
				{
					if (row[static_cast<size_t>(x) * 4 + 3] == 0)
					{
						++x;

						continue;
					}

					const int32_t from{ x };

					while (x < width && row[static_cast<size_t>(x) * 4 + 3] != 0)
					{
						++x;
					}

					spans.push_back({ from, x - 1 });
				}

				std::vector<span_box> carried{};

				for (auto& item_box : open)
				{
					const auto match{ std::ranges::find(spans, std::pair{ item_box.x0, item_box.x1 }) };

					if (match == spans.end())
					{
						boxes.push_back(item_box);

						continue;
					}

					item_box.y1 = y;
					carried.push_back(item_box);
					spans.erase(match);
				}

				for (const auto& entry : spans)
				{
					carried.push_back({ entry.first, entry.second, y, y });
				}

				open = std::move(carried);
			}

			boxes.insert(boxes.end(), open.begin(), open.end());

			return boxes;
		}

		static auto is_uniform(const std::vector<span_box>& boxes) noexcept -> bool
		{
			if (boxes.empty())
			{
				return false;
			}

			const auto& first{ boxes.front() };

			return std::ranges::all_of(boxes, [&first](const span_box& item) noexcept -> bool
			{
				return (item.x1 - item.x0) == (first.x1 - first.x0) && (item.y1 - item.y0) == (first.y1 - first.y0);
			});
		}

		static auto is_solid(const psdpimg::psd::layer& item) noexcept -> bool
		{
			uint8_t seen[3]{};
			bool    first{};

			for (size_t i{}; i < item.pixels.size(); i += 4)
			{
				if (item.pixels[i + 3] == 0)
				{
					continue;
				}

				if (!first)
				{
					seen[0] = item.pixels[i];
					seen[1] = item.pixels[i + 1];
					seen[2] = item.pixels[i + 2];
					first = true;

					continue;
				}

				if (item.pixels[i] != seen[0] || item.pixels[i + 1] != seen[1] ||
					item.pixels[i + 2] != seen[2])
				{
					return false;
				}
			}

			return first;
		}

		static auto is_marker(const psdpimg::psd::layer& item) noexcept -> bool
		{
			if (item.section != psdpimg::psd::section_t::layer)
			{
				return false;
			}

			if (item.right <= item.left || item.bottom <= item.top)
			{
				return false;
			}

			return psdpimg::pimg::is_solid(item);
		}

		static auto is_placeholder(const psdpimg::psd::layer& item) noexcept -> bool
		{
			return item.placeholder;
		}

		static auto fill_shade(psdpimg::psd::layer& item) -> void
		{
			constexpr uint8_t shade{ 128 };

			const size_t width{ static_cast<size_t>(std::max(item.right - item.left, 0)) };
			const size_t height{ static_cast<size_t>(std::max(item.bottom - item.top, 0)) };

			item.pixels.assign(width * height * 4, 0);

			for (size_t i{ 3 }; i < item.pixels.size(); i += 4)
			{
				item.pixels[i] = shade;
			}
		}

		static auto has_content(const psdpimg::psd::layer& item) noexcept -> bool
		{
			for (size_t i{ 3 }; i < item.pixels.size(); i += 4)
			{
				if (item.pixels[i] != 0)
				{
					return true;
				}
			}

			return false;
		}

		static auto shade_placeholders(std::vector<psdpimg::psd::layer>& layers) -> void
		{
			for (auto& item : layers)
			{
				if (psdpimg::pimg::is_placeholder(item) && !psdpimg::pimg::has_content(item))
				{
					psdpimg::pimg::fill_shade(item);
				}
			}
		}

		struct group_state
		{
			size_t              slot{};
			std::vector<size_t>  members{};
			bool                pure{ true };
		};

		static auto merge_placeholders(std::vector<psdpimg::psd::layer>& layers) -> void
		{
			std::vector<psdpimg::psd::layer> result{};
			result.reserve(layers.size());

			std::vector<psdpimg::pimg::group_state> openers{};

			for (size_t i{}; i < layers.size(); ++i)
			{
				const auto section{ layers[i].section };

				if (section == psdpimg::psd::section_t::divider)
				{
					result.push_back(layers[i]);
					openers.push_back({ .slot = result.size() - 1, .members = {}, .pure = true });

					continue;
				}

				if (section == psdpimg::psd::section_t::layer && psdpimg::pimg::is_placeholder(layers[i]))
				{
					result.push_back(layers[i]);

					if (!openers.empty())
					{
						openers.back().members.push_back(i);
					}

					continue;
				}

				if (section == psdpimg::psd::section_t::closed_folder && !openers.empty())
				{
					auto& group{ openers.back() };

					if (!group.members.empty() && group.pure)
					{
						result.resize(group.slot);

						int32_t left{ layers[group.members.front()].left };
						int32_t top{ layers[group.members.front()].top };
						int32_t right{ left };
						int32_t bottom{ top };

						for (const auto index : group.members)
						{
							left   = std::min(left, layers[index].left);
							top    = std::min(top, layers[index].top);
							right  = std::max(right, std::max(layers[index].left, layers[index].right));
							bottom = std::max(bottom, std::max(layers[index].top, layers[index].bottom));
						}

						psdpimg::psd::layer merged{ layers[i] };

						merged.section = psdpimg::psd::section_t::layer;
						merged.left    = left;
						merged.top     = top;
						merged.right   = right;
						merged.bottom  = bottom;
						merged.pixels.assign(static_cast<size_t>(right - left) *
											 static_cast<size_t>(bottom - top) * 4, 0);

						constexpr uint8_t shade{ 128 };
						const size_t      stride{ static_cast<size_t>(right - left) };

						for (const auto index : group.members)
						{
							const auto&  source{ layers[index] };
							const int32_t last_x{ std::max(source.left, source.right) };
							const int32_t last_y{ std::max(source.top, source.bottom) };
							const size_t  inner{ static_cast<size_t>(last_x - source.left) };
							const bool   has_pixels{ source.pixels.size() >= inner * static_cast<size_t>(last_y - source.top) * 4 };

							for (int32_t y{ source.top }; y < last_y; ++y)
							{
								const size_t row{ static_cast<size_t>(y - top) * stride };

								for (int32_t x{ source.left }; x < last_x; ++x)
								{
									const size_t index_out{ (row + static_cast<size_t>(x - left)) * 4 };
									const size_t index_in
									{
										(static_cast<size_t>(y - source.top) * inner + static_cast<size_t>(x - source.left)) * 4
									};

									if (has_pixels && source.pixels[index_in + 3] != 0)
									{
										merged.pixels[index_out]     = source.pixels[index_in];
										merged.pixels[index_out + 1] = source.pixels[index_in + 1];
										merged.pixels[index_out + 2] = source.pixels[index_in + 2];
										merged.pixels[index_out + 3] = source.pixels[index_in + 3];

										continue;
									}

									merged.pixels[index_out + 3] = shade;
								}
							}
						}

						result.push_back(std::move(merged));

						openers.pop_back();

						continue;
					}

					result.push_back(layers[i]);
					openers.pop_back();

					continue;
				}

				if (!openers.empty())
				{
					openers.back().pure = false;
				}

				result.push_back(layers[i]);
			}

			layers = std::move(result);
		}

		static auto is_space(const char item) noexcept -> bool
		{
			constexpr std::string_view blanks{ " \t\r\n\v\f" };

			return blanks.find(item) != std::string_view::npos;
		}

		static auto trim_name(std::string& text) -> void
		{
			std::string_view view{ text };

			while (!view.empty() && psdpimg::pimg::is_space(view.front()))
			{
				view.remove_prefix(1);
			}

			while (!view.empty() && psdpimg::pimg::is_space(view.back()))
			{
				view.remove_suffix(1);
			}

			const bool unchanged{ view.size() == text.size() };

			if (!unchanged)
			{
				text.assign(view);
			}
		}

		static auto matches(const psdpimg::pimg::name_filter& filter, const std::string& text) -> bool
		{
			switch (filter.kind)
			{
			case psdpimg::pimg::filter_kind::prefix:
			{
				return text.starts_with(filter.text);
			}
			case psdpimg::pimg::filter_kind::suffix:
			{
				return text.ends_with(filter.text);
			}
			case psdpimg::pimg::filter_kind::contains:
			{
				return text.find(filter.text) != std::string::npos;
			}
			case psdpimg::pimg::filter_kind::equal:
			{
				return text == filter.text;
			}
			case psdpimg::pimg::filter_kind::pattern:
			{
				return false;
			}
			}

			return false;
		}

		static auto compile_patterns(const std::vector<psdpimg::pimg::name_filter>& filters) -> std::vector<std::regex>
		{
			std::vector<std::regex> result{};

			for (const auto& item : filters)
			{
				if (item.kind == psdpimg::pimg::filter_kind::pattern)
				{
					result.emplace_back(item.text);
				}
			}

			return result;
		}

		static auto matches_any(const std::vector<psdpimg::pimg::name_filter>& filters,
		                        const std::vector<std::regex>&                 patterns,
		                        const std::string&                             text) -> bool
		{
			const auto matched{ [&text](const psdpimg::pimg::name_filter& item) noexcept -> bool
			{
				return psdpimg::pimg::matches(item, text);
			} };

			if (std::ranges::any_of(filters, matched))
			{
				return true;
			}

			const auto searched{ [&text](const std::regex& item) noexcept -> bool
			{
				return std::regex_search(text, item);
			} };

			return std::ranges::any_of(patterns, searched);
		}

		struct pixel_box
		{
			uint32_t left{};
			uint32_t top{};
			uint32_t right{};
			uint32_t bottom{};
			bool     solid{};
		};

		static auto alpha_box(const psdpimg::common::image& picture) -> pixel_box
		{
			pixel_box result{};

			for (uint32_t y{}; y < picture.height; ++y)
			{
				for (uint32_t x{}; x < picture.width; ++x)
				{
					const auto offset{ picture.offset(x, y) };
					const bool opaque{ picture.pixels[offset + 3] != 0 };

					if (!opaque)
					{
						continue;
					}

					if (!result.solid)
					{
						result = pixel_box{ x, y, x + 1, y + 1, true };

						continue;
					}

					result.left   = std::min(result.left, x);
					result.top    = std::min(result.top, y);
					result.right  = std::max(result.right, x + 1);
					result.bottom = std::max(result.bottom, y + 1);
				}
			}

			return result;
		}

		static auto split_shaded(std::span<const psdpimg::psd::layer> layers) -> std::vector<psdpimg::psd::layer>
		{
			constexpr size_t max_boxes{ 64 };

			std::vector<psdpimg::psd::layer> result{};
			result.reserve(layers.size());

			for (const auto& item : layers)
			{
				const std::string_view label{ item.display_name() };
				const bool            marked{ label.starts_with("##") };
				const bool            plain{ marked && item.section == psdpimg::psd::section_t::layer &&
									 item.has_pixels() };

				auto boxes{ plain ? psdpimg::pimg::alpha_spans(item) : std::vector<span_box>{} };

				const bool splittable
				{
					plain &&
					boxes.size() > 1 &&
					boxes.size() <= max_boxes &&
					psdpimg::pimg::is_solid(item)
				};

				if (!splittable)
				{
					result.push_back(item);

					continue;
				}

				std::ranges::sort(boxes, [](const span_box& left, const span_box& right) noexcept -> bool
				{
					return left.y0 != right.y0 ? left.y0 < right.y0 : left.x0 < right.x0;
				});

				psdpimg::psd::layer opener{};

				opener.section      = psdpimg::psd::section_t::divider;
				opener.name         = std::string{ group_end_name };
				opener.unicode_name = opener.name;

				result.push_back(opener);

				for (size_t k{}; k < boxes.size(); ++k)
				{
					psdpimg::psd::layer member{};

					member.section      = psdpimg::psd::section_t::layer;
					member.name         = std::to_string(k);
					member.unicode_name = member.name;
					member.left         = item.left + boxes[k].x0;
					member.top          = item.top + boxes[k].y0;
					member.right        = item.left + boxes[k].x1 + 1;
					member.bottom       = item.top + boxes[k].y1 + 1;
					member.pixels.assign(static_cast<size_t>(member.right - member.left) *
										 static_cast<size_t>(member.bottom - member.top) * 4, 0);

					for (size_t i{ 3 }; i < member.pixels.size(); i += 4)
					{
						member.pixels[i] = shade_alpha;
					}

					result.push_back(member);
				}

				psdpimg::psd::layer folder{ item };

				folder.section = psdpimg::psd::section_t::closed_folder;
				folder.pixels.clear();
				folder.left    = 0;
				folder.top     = 0;
				folder.right   = 0;
				folder.bottom  = 0;

				result.push_back(folder);
			}

			return result;
		}

		static auto crop_image(const psdpimg::common::image& source, const pixel_box& box) -> psdpimg::common::image
		{
			const auto width{ box.right - box.left };
			const auto height{ box.bottom - box.top };

			psdpimg::common::image result{};

			result.resize(width, height);

			for (uint32_t y{}; y < height; ++y)
			{
				const auto* const line{ source.pixels.data() + source.offset(box.left, box.top + y) };
				auto* const       target{ result.pixels.data() + result.offset(0, y) };

				std::memcpy(target, line, static_cast<size_t>(width) * 4);
			}

			return result;
		}
	}

	auto to_psd(const std::span<const uint8_t> bytes) -> psdpimg::psd::document
	{
		const bool is_document{ bytes.size() >= 4 && bytes[0] == '8' && bytes[1] == 'B' &&
							   bytes[2] == 'P' && bytes[3] == 'S' };

		if (is_document)
		{
			auto result{ psdpimg::psd::read(bytes) };

			psdpimg::pimg::shade_placeholders(result.layers);
			psdpimg::pimg::merge_placeholders(result.layers);

			return result;
		}

		const auto archive{ psdpimg::psb::read(bytes) };
		const auto& root{ archive.root };
		const auto* const layers{ root.find("layers") };
		const bool usable{ layers != nullptr && layers->kind() == psdpimg::psb::value_kind_t::list };

		if (!usable)
		{
			throw std::runtime_error{ "pimg::to_psd: container has no layer table" };
		}

		const auto specs{ psdpimg::pimg::read_specs(*layers) };
		const auto resources{ psdpimg::pimg::collect_resources(root) };

		tree_builder builder{ specs };

		std::vector<record_ref> flat{};

		for (const auto* const item : builder.build())
		{
			psdpimg::pimg::flatten(*item, flat);
		}

		std::ranges::reverse(flat);

		const auto width_value{ psdpimg::pimg::integer_of(root.find("width"), 0) };
		const auto height_value{ psdpimg::pimg::integer_of(root.find("height"), 0) };

		psdpimg::psd::document result
		{
			.width = static_cast<uint32_t>(std::max<int64_t>(width_value, 0)),
			.height = static_cast<uint32_t>(std::max<int64_t>(height_value, 0))
		};

		result.layers.reserve(flat.size());

		image_cache cache{ resources };

		for (const auto& item : flat)
		{
			result.layers.push_back(psdpimg::pimg::make_layer(item, specs, cache));
		}

		psdpimg::pimg::shade_placeholders(result.layers);
		psdpimg::pimg::merge_placeholders(result.layers);

		result.raw_pimg.assign(bytes.begin(), bytes.end());
		result.raw_stamp = psdpimg::psd::origin_stamp(result);

		return result;
	}

	auto from_psd(const psdpimg::psd::document& source,
				 const psdpimg::pimg::import_options& options) -> std::vector<uint8_t>
	{
		const bool carries_origin{ !source.raw_pimg.empty() };
		const bool reusable{ carries_origin && !options.active() };
		const bool untouched{ reusable && psdpimg::psd::origin_stamp(source) == source.raw_stamp };

		if (untouched)
		{
			return source.raw_pimg;
		}

		struct resolved_name
		{
			std::string name{};
			int64_t     id{};
			bool        divider{};
		};

		struct group_record
		{
			std::string name{};
			int64_t     id{};
			int64_t     left{};
			int64_t     top{};
			int64_t     width{};
			int64_t     height{};
			int64_t     opacity{ default_opacity };
			bool        visible{ true };
			bool        has_parent{};
			int64_t     parent_id{};
		};

		struct encoded_image
		{
			std::vector<uint8_t> blob{};
			int64_t              owner{};
		};

		const auto expanded{ psdpimg::pimg::split_shaded(source.layers) };

		std::vector<const psdpimg::psd::layer*> ordered{};
		ordered.reserve(expanded.size());

		for (size_t i{ expanded.size() }; i-- > 0;)
		{
			ordered.push_back(&expanded[i]);
		}

		std::vector<resolved_name> names(ordered.size());
		std::vector<int64_t> registry{};
		registry.reserve(ordered.size());

		for (size_t i{}; i < ordered.size(); ++i)
		{
			const auto* const item{ ordered[i] };
			const bool        is_divider{ item->section == psdpimg::psd::section_t::divider };

			names[i].divider = is_divider;

			if (is_divider)
			{
				continue;
			}

			std::string label{ item->display_name() };
			const auto marker{ label.rfind(layer_id_marker) };
			int64_t identifier{};

			if (marker != std::string::npos)
			{
				const auto tail{ label.substr(marker + layer_id_marker.size()) };

				std::from_chars(tail.data(), tail.data() + tail.size(), identifier);
				label.erase(marker);
			}
			else if (psdpimg::pimg::is_marker(*item))
			{
				identifier = layer_id_minus_one;
			}
			else if (item->has_layer_id)
			{
				const auto candidate{ static_cast<int64_t>(item->layer_id) };
				const bool known{ std::ranges::find(registry, candidate) != registry.end() };

				if (!known)
				{
					identifier = candidate;
					registry.push_back(candidate);
				}
				else if (candidate == layer_id_minus_one)
				{
					identifier = candidate;
				}
			}

			if (options.trim_names)
			{
				psdpimg::pimg::trim_name(label);
			}

			names[i].name = std::move(label);
			names[i].id   = identifier;
		}

		const auto& layer_patterns{ options.ignore_layers };
		const auto& group_patterns{ options.ignore_groups };
		const auto  layer_regexes{ psdpimg::pimg::compile_patterns(options.ignore_layers) };
		const auto  group_regexes{ psdpimg::pimg::compile_patterns(options.ignore_groups) };

		std::vector<bool> ignored(ordered.size(), false);

		if (!layer_patterns.empty() || !group_patterns.empty())
		{
			std::vector<size_t> scope{};

			for (size_t i{}; i < ordered.size(); ++i)
			{
				const auto section{ ordered[i]->section };

				if (section == psdpimg::psd::section_t::divider)
				{
					if (!scope.empty())
					{
						scope.pop_back();
					}

					continue;
				}

				const bool is_folder
				{
					section == psdpimg::psd::section_t::open_folder ||
					section == psdpimg::psd::section_t::closed_folder
				};

				if (is_folder)
				{
					scope.push_back(i);

					continue;
				}

				const auto in_group{ [&names, &group_patterns, &group_regexes](const size_t index) noexcept -> bool
				{
					return psdpimg::pimg::matches_any(group_patterns, group_regexes, names[index].name);
				} };

				const bool by_name{ psdpimg::pimg::matches_any(layer_patterns, layer_regexes, names[i].name) };

				ignored[i] = by_name || std::ranges::any_of(scope, in_group);
			}
		}

		for (size_t i{}; i < ignored.size(); ++i)
		{
			if (!ignored[i])
			{
				continue;
			}

			const auto previous{ names[i].id };

			names[i].id = layer_id_minus_one;

			if (previous > 0)
			{
				std::erase(registry, previous);
			}
		}

		int64_t next_identifier{ 1 };

		for (size_t i{}; i < names.size(); ++i)
		{
			if (names[i].divider || names[i].id != 0)
			{
				continue;
			}

			while (std::ranges::find(registry, next_identifier) != registry.end())
			{
				++next_identifier;
			}

			names[i].id = next_identifier;
			registry.push_back(next_identifier);
		}

		psdpimg::psb::value layers{ psdpimg::psb::value::make_list() };
		std::vector<group_record>                                 groups{};
		std::vector<size_t>                                       stack{};
		std::vector<encoded_image>                                encoded{};
		std::vector<std::pair<std::string, std::vector<uint8_t>>> resources{};

		for (size_t i{}; i < ordered.size(); ++i)
		{
			const auto* const item{ ordered[i] };

			if (item->section == psdpimg::psd::section_t::divider)
			{
				if (!stack.empty())
				{
					stack.pop_back();
				}

				continue;
			}

			const bool is_folder
			{
				item->section == psdpimg::psd::section_t::open_folder ||
				item->section == psdpimg::psd::section_t::closed_folder
			};

			if (is_folder)
			{
				group_record entry{};
				entry.name       = names[i].name;
				entry.id         = names[i].id;
				entry.left       = item->left;
				entry.top        = item->top;
				entry.width      = item->width();
				entry.height     = item->height();
				entry.opacity    = item->opacity;
				entry.visible    = item->visible;
				entry.has_parent = !stack.empty();

				if (entry.has_parent)
				{
					entry.parent_id = groups[stack.back()].id;
				}

				groups.push_back(std::move(entry));
				stack.push_back(groups.size() - 1);

				continue;
			}

			psdpimg::psb::value object{ psdpimg::psb::value::make_dictionary() };

			psdpimg::common::image picture{};
			int64_t                left{ item->left };
			int64_t                top{ item->top };
			int64_t                width{ item->width() };
			int64_t                height{ item->height() };
			bool                   usable{ item->has_pixels() };

			if (usable)
			{
				picture.width  = static_cast<uint32_t>(item->width());
				picture.height = static_cast<uint32_t>(item->height());
				picture.pixels = item->pixels;
			}

			const bool cropping{ usable && options.crop_alpha && !ignored[i] };

			if (cropping)
			{
				const auto box{ psdpimg::pimg::alpha_box(picture) };

				if (!box.solid)
				{
					picture = psdpimg::common::image{};
					width   = 0;
					height  = 0;
					usable  = false;
				}
				else
				{
					picture = psdpimg::pimg::crop_image(picture, box);
					left += static_cast<int64_t>(box.left);
					top += static_cast<int64_t>(box.top);
					width  = static_cast<int64_t>(box.right - box.left);
					height = static_cast<int64_t>(box.bottom - box.top);
				}
			}

			object.set("layer_id", psdpimg::psb::value::make_integer(names[i].id));
			object.set("layer_type", psdpimg::psb::value::make_integer(image_layer_type));
			object.set("name", psdpimg::psb::value::make_string(names[i].name));
			object.set("left", psdpimg::psb::value::make_integer(left));
			object.set("top", psdpimg::psb::value::make_integer(top));
			object.set("width", psdpimg::psb::value::make_integer(width));
			object.set("height", psdpimg::psb::value::make_integer(height));
			object.set("type", psdpimg::psb::value::make_integer(element_type));
			object.set("visible", psdpimg::psb::value::make_integer(item->visible ? 1 : 0));
			object.set("opacity", psdpimg::psb::value::make_integer(item->opacity));

			if (!stack.empty())
			{
				object.set("group_layer_id", psdpimg::psb::value::make_integer(groups[stack.back()].id));
			}

			const bool encodable{ usable && !ignored[i] && names[i].id >= 0 };

			if (encodable)
			{
				auto blob{ psdpimg::tlg::encode(picture) };

				size_t duplicate{ not_found };

				for (size_t j{}; j < encoded.size(); ++j)
				{
					if (encoded[j].blob == blob)
					{
						duplicate = j;

						break;
					}
				}

				if (duplicate != not_found)
				{
					object.set("same_image", psdpimg::psb::value::make_integer(encoded[duplicate].owner));
				}
				else
				{
					const auto key{ std::to_string(names[i].id) + ".tlg" };

					resources.emplace_back(key, blob);
					encoded.push_back({ std::move(blob), names[i].id });
				}
			}

			layers.push_back(std::move(object));
		}

		for (const auto& entry : groups)
		{
			psdpimg::psb::value object{ psdpimg::psb::value::make_dictionary() };

			object.set("layer_id", psdpimg::psb::value::make_integer(entry.id));
			object.set("layer_type", psdpimg::psb::value::make_integer(group_layer_type));
			object.set("type", psdpimg::psb::value::make_integer(element_type));
			object.set("width", psdpimg::psb::value::make_integer(entry.width));
			object.set("height", psdpimg::psb::value::make_integer(entry.height));
			object.set("left", psdpimg::psb::value::make_integer(entry.left));
			object.set("top", psdpimg::psb::value::make_integer(entry.top));
			object.set("name", psdpimg::psb::value::make_string(entry.name));
			object.set("visible", psdpimg::psb::value::make_integer(entry.visible ? 1 : 0));
			object.set("opacity", psdpimg::psb::value::make_integer(entry.opacity));

			if (entry.has_parent)
			{
				object.set("group_layer_id", psdpimg::psb::value::make_integer(entry.parent_id));
			}

			layers.push_back(std::move(object));
		}

		psdpimg::psb::value root{ psdpimg::psb::value::make_dictionary() };

		root.set("width", psdpimg::psb::value::make_integer(static_cast<int64_t>(source.width)));
		root.set("height", psdpimg::psb::value::make_integer(static_cast<int64_t>(source.height)));
		root.set("layers", std::move(layers));

		for (auto& entry : resources)
		{
			root.set(std::move(entry.first), psdpimg::psb::value::make_resource(0, std::move(entry.second)));
		}

		psdpimg::psb::archive archive{};
		archive.version = target_psb_version;
		archive.root    = std::move(root);

		auto bytes{ psdpimg::psb::write(archive) };
		return bytes;
	}
}
