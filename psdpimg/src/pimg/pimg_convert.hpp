#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <psd_document.hpp>

namespace psdpimg::pimg
{
	enum class filter_kind : uint8_t
	{
		prefix,
		suffix,
		contains,
		equal,
		pattern,
	};

	struct name_filter
	{
		filter_kind kind{};
		std::string text{};
	};

	struct list_source
	{
		std::wstring path{};
		bool         group{};
	};

	struct import_options
	{
		std::vector<name_filter> ignore_layers{};
		std::vector<name_filter> ignore_groups{};
		std::vector<list_source> list_sources{};
		bool                     trim_names{};
		bool                     crop_alpha{};

		[[nodiscard]] inline auto active() const noexcept -> bool
		{
			const bool ignoring
			{
				!this->ignore_layers.empty() ||
				!this->ignore_groups.empty() ||
				!this->list_sources.empty()
			};

			return ignoring || this->trim_names || this->crop_alpha;
		}
	};

	struct export_options
	{
		bool expand_markers{};
	};

	extern [[nodiscard]] auto   to_psd(std::span<const uint8_t> bytes,
	                                   const psdpimg::pimg::export_options& options = {}) -> psdpimg::psd::document;
	extern [[nodiscard]] auto from_psd(const psdpimg::psd::document& source,
	                                   const psdpimg::pimg::import_options& options = {}) -> std::vector<uint8_t>;
}
