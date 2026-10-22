#include <stdexcept>

#include "psb/psb_value.hpp"

namespace psdpimg::psb
{
	auto value::make_null() -> value
	{
		value result{};
		result.m_kind = psdpimg::psb::value_kind_t::null;

		return result;
	}

	auto value::make_boolean(const bool flag) -> value
	{
		value result{};
		result.m_kind    = psdpimg::psb::value_kind_t::boolean;
		result.m_boolean = flag;

		return result;
	}

	auto value::make_integer(const int64_t number) -> value
	{
		value result{};
		result.m_kind    = psdpimg::psb::value_kind_t::integer;
		result.m_integer = number;

		return result;
	}

	auto value::make_real(const double number) -> value
	{
		value result{};
		result.m_kind = psdpimg::psb::value_kind_t::real;
		result.m_real = number;

		return result;
	}

	auto value::make_string(const std::string_view text) -> value
	{
		value result{};
		result.m_kind   = psdpimg::psb::value_kind_t::string;
		result.m_string = text;

		return result;
	}

	auto value::make_resource(const uint32_t index, std::vector<uint8_t> data) -> value
	{
		value result{};
		result.m_kind     = psdpimg::psb::value_kind_t::resource;
		result.m_integer  = static_cast<int64_t>(index);
		result.m_resource = std::move(data);

		return result;
	}

	auto value::make_list() -> value
	{
		value result{};
		result.m_kind = psdpimg::psb::value_kind_t::list;

		return result;
	}

	auto value::make_dictionary() -> value
	{
		value result{};
		result.m_kind = psdpimg::psb::value_kind_t::dictionary;

		return result;
	}

	auto value::as_boolean() const -> bool
	{
		if (this->m_kind != psdpimg::psb::value_kind_t::boolean)
		{
			throw std::runtime_error{ "psb::value::as_boolean: not a boolean" };
		}

		return this->m_boolean;
	}

	auto value::as_integer() const -> int64_t
	{
		if (this->m_kind != psdpimg::psb::value_kind_t::integer)
		{
			throw std::runtime_error{ "psb::value::as_integer: not an integer" };
		}

		return this->m_integer;
	}

	auto value::as_real() const -> double
	{
		if (this->m_kind == psdpimg::psb::value_kind_t::integer)
		{
			return static_cast<double>(this->m_integer);
		}

		if (this->m_kind != psdpimg::psb::value_kind_t::real)
		{
			throw std::runtime_error{ "psb::value::as_real: not a number" };
		}

		return this->m_real;
	}

	auto value::as_string() const -> const std::string&
	{
		if (this->m_kind != psdpimg::psb::value_kind_t::string)
		{
			throw std::runtime_error{ "psb::value::as_string: not a string" };
		}

		return this->m_string;
	}

	auto value::as_resource() const -> const std::vector<uint8_t>&
	{
		if (this->m_kind != psdpimg::psb::value_kind_t::resource)
		{
			throw std::runtime_error{ "psb::value::as_resource: not a resource" };
		}

		return this->m_resource;
	}

	auto value::push_back(value item) -> value&
	{
		this->m_items.push_back(std::move(item));

		return this->m_items.back();
	}

	auto value::set(const std::string_view key, value item) -> value&
	{
		for (size_t i{}; i < this->m_keys.size(); ++i)
		{
			if (this->m_keys[i] == key)
			{
				this->m_items[i] = std::move(item);

				return this->m_items[i];
			}
		}

		this->m_keys.emplace_back(key);
		this->m_items.push_back(std::move(item));

		return this->m_items.back();
	}
}
