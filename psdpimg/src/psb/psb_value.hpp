#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace psdpimg::psb
{
	enum class value_kind_t : uint8_t
	{
		null,
		boolean,
		integer,
		real,
		string,
		resource,
		list,
		dictionary,
	};

	class value
	{
		value_kind_t             m_kind{ psdpimg::psb::value_kind_t::null };
		bool                     m_boolean{};
		int64_t                  m_integer{};
		double                   m_real{};
		std::string              m_string{};
		std::vector<uint8_t>     m_resource{};
		std::vector<std::string> m_keys{};
		std::vector<value>       m_items{};

	public:
		value() noexcept = default;

		[[nodiscard]] static auto       make_null() -> value;
		[[nodiscard]] static auto    make_boolean(const bool flag) -> value;
		[[nodiscard]] static auto    make_integer(const int64_t number) -> value;
		[[nodiscard]] static auto       make_real(const double number) -> value;
		[[nodiscard]] static auto     make_string(const std::string_view text) -> value;
		[[nodiscard]] static auto   make_resource(const uint32_t index, std::vector<uint8_t> data) -> value;
		[[nodiscard]] static auto       make_list() -> value;
		[[nodiscard]] static auto make_dictionary() -> value;

		[[nodiscard]] inline auto kind() const noexcept -> value_kind_t
		{
			return this->m_kind;
		}

		[[nodiscard]] inline auto is_null() const noexcept -> bool
		{
			return this->m_kind == psdpimg::psb::value_kind_t::null;
		}

		[[nodiscard]] auto  as_boolean() const -> bool;
		[[nodiscard]] auto  as_integer() const -> int64_t;
		[[nodiscard]] auto     as_real() const -> double;
		[[nodiscard]] auto   as_string() const -> const std::string&;
		[[nodiscard]] auto as_resource() const -> const std::vector<uint8_t>&;

		[[nodiscard]] inline auto items() const noexcept -> const std::vector<value>&
		{
			return this->m_items;
		}

		[[nodiscard]] inline auto items() noexcept -> std::vector<value>&
		{
			return this->m_items;
		}

		[[nodiscard]] inline auto keys() const noexcept -> const std::vector<std::string>&
		{
			return this->m_keys;
		}

		[[nodiscard]] inline auto find(const std::string_view key) const noexcept -> const value*
		{
			for (size_t i{}; i < this->m_keys.size() && i < this->m_items.size(); ++i)
			{
				if (this->m_keys[i] == key)
				{
					return &this->m_items[i];
				}
			}

			return nullptr;
		}

		auto push_back(value item) -> value&;
		auto       set(const std::string_view key, value item) -> value&;
	};
}
