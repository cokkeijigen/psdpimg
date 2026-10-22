#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "psd/psd_document.hpp"

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

	struct import_options
	{
		std::vector<name_filter> ignore_layers{};
		std::vector<name_filter> ignore_groups{};
		bool                     trim_names{};
		bool                     crop_alpha{};

		[[nodiscard]] inline auto active() const noexcept -> bool
		{
			const bool ignoring{ !this->ignore_layers.empty() || !this->ignore_groups.empty() };

			return ignoring || this->trim_names || this->crop_alpha;
		}
	};

	extern [[nodiscard]] auto   to_psd(std::span<const uint8_t> bytes) -> psdpimg::psd::document;
	extern [[nodiscard]] auto from_psd(const psdpimg::psd::document& source,
	                                   const psdpimg::pimg::import_options& options = {}) -> std::vector<uint8_t>;
}
