#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>
#include <winternl.h>

#include <xfsys.hpp>
#include <xstr.hpp>

#include <pimg_convert.hpp>

namespace psdpimg
{
	enum class direction_t : uint8_t
	{
		detect,
		to_pimg,
		to_psd,
	};

	struct unicode_string
	{
		uint16_t length{};
		uint16_t capacity{};
		wchar_t* buffer{};
	};

	struct ldr_entry
	{
		std::byte* load_flink{};
		std::byte* load_blink{};
		std::byte* memory_flink{};
		std::byte* memory_blink{};
		std::byte* init_flink{};
		std::byte* init_blink{};
		void* base{};
		void* entry_point{};
		uintptr_t               image_size{};
		psdpimg::unicode_string full_name{};
	};

	struct peb_ldr_data
	{
		uint32_t   length{};
		uint8_t    initialized{};
		std::byte  reserved[3]{};
		void*      ss_handle{};
		const ldr_entry* load_flink{};
		const ldr_entry* load_blink{};
	};

	static_assert
	(
		offsetof(psdpimg::ldr_entry, full_name) == (sizeof(void*) == 4 ? 36 : 72),
		"ldr_entry must mirror LDR_DATA_TABLE_ENTRY up to FullDllName"
	);

	inline static auto executable_stem() noexcept -> std::wstring_view
	{
		const ::PEB* const peb{ ::NtCurrentTeb()->ProcessEnvironmentBlock };
		const auto ldr{ reinterpret_cast<const psdpimg::peb_ldr_data*>(peb->Ldr) };
		const std::wstring_view path
		{
			ldr->load_flink->full_name.buffer,
			ldr->load_flink->full_name.length / sizeof(wchar_t)
		};

		const std::size_t slash{ path.find_last_of(L"\\/") };
		const std::size_t begin{ slash == std::wstring_view::npos ? 0 : slash + 1 };

		const std::size_t dot{ path.find_last_of(L'.') };
		const std::size_t end{ dot != std::wstring_view::npos && dot > begin ? dot : path.size() };
		return path.substr(begin, end - begin);
	}

	struct filter_switch
	{
		std::wstring_view          token{};
		psdpimg::pimg::filter_kind kind{};
		bool                       group{};
		bool                       list{};
	};

	inline constexpr filter_switch filter_switches[]
	{
		{ .token{ L"-ignore="             }, .kind{ psdpimg::pimg::filter_kind::equal    }, .group{ false }, .list{ false } },
		{ .token{ L"-ignore-prefix="      }, .kind{ psdpimg::pimg::filter_kind::prefix   }, .group{ false }, .list{ false } },
		{ .token{ L"-ignore-suffix="      }, .kind{ psdpimg::pimg::filter_kind::suffix   }, .group{ false }, .list{ false } },
		{ .token{ L"-ignore-include="     }, .kind{ psdpimg::pimg::filter_kind::contains }, .group{ false }, .list{ false } },
		{ .token{ L"-ignoregroup="        }, .kind{ psdpimg::pimg::filter_kind::equal    }, .group{ true  }, .list{ false } },
		{ .token{ L"-ignoregroup-prefix=" }, .kind{ psdpimg::pimg::filter_kind::prefix   }, .group{ true  }, .list{ false } },
		{ .token{ L"-ignoregroup-suffix=" }, .kind{ psdpimg::pimg::filter_kind::suffix   }, .group{ true  }, .list{ false } },
		{ .token{ L"-ignoregroup-include="}, .kind{ psdpimg::pimg::filter_kind::contains }, .group{ true  }, .list{ false } },
		{ .token{ L"-ignore-list="        }, .kind{ psdpimg::pimg::filter_kind::pattern  }, .group{ false }, .list{ true  } },
		{ .token{ L"-ignoregroup-list="   }, .kind{ psdpimg::pimg::filter_kind::pattern  }, .group{ true  }, .list{ true  } },
	};

	inline auto append_filter(const std::wstring_view argument, psdpimg::pimg::import_options& target) -> bool
	{
		for (const auto& item : filter_switches)
		{
			if (!argument.starts_with(item.token))
			{
				continue;
			}

			const std::wstring_view value{ argument.substr(item.token.size()) };

			if (item.list)
			{
				target.list_sources.push_back({ .path{ std::wstring{ value } }, .group{ item.group } });

				return true;
			}

			std::vector<psdpimg::pimg::name_filter>& list{ item.group ? target.ignore_groups : target.ignore_layers };

			list.push_back({ .kind{ item.kind }, .text{ xstr::cvt::to_utf8(value) } });

			return true;
		}

		return false;
	}

	inline auto apply_type(const std::wstring_view kind, psdpimg::pimg::import_options& target) noexcept -> bool
	{
		const bool ui{ kind == L"ui" };

		if (!ui)
		{
			return false;
		}

		const psdpimg::pimg::name_filter hash_prefix
		{
			.kind{ psdpimg::pimg::filter_kind::prefix },
			.text{ "#" }
		};

		target.ignore_layers =
		{
			{ .kind{ psdpimg::pimg::filter_kind::contains }, .text{ "rect" } },
			hash_prefix
		};
		target.ignore_groups = { hash_prefix };
		target.trim_names    = true;
		target.crop_alpha    = true;

		return true;
	}

	struct args
	{
		enum class state : uint8_t
		{
			ok                  = 0,
			help                = 1,
			no_input            = 2,
			unknown_switch      = 3,
			conflicting_switch  = 4,
			too_many_arguments  = 5,
			invalid_value       = 6,
			ambiguous_direction = 7,
		};

		std::vector<std::wstring>     inputs{};
		std::wstring                  output{};
		psdpimg::direction_t          mode{ psdpimg::direction_t::detect };
		psdpimg::pimg::import_options incoming{};
		bool                          expand_markers{};
		bool                          named{};
		psdpimg::args::state          status{ psdpimg::args::state::ok };

		inline args(const int argc, const wchar_t* const argv[])
		{
			this->parse(argc, argv);
		}

	private:
		inline auto apply_program() -> void
		{
			std::wstring stem{ psdpimg::executable_stem() };

			for (wchar_t& ch : stem)
			{
				if (ch >= L'A' && ch <= L'Z')
				{
					ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
				}
			}

			if (stem == L"psd2pimg")
			{
				this->mode  = psdpimg::direction_t::to_pimg;
				this->named = true;

				return;
			}

			if (stem == L"pimg2psd")
			{
				this->mode  = psdpimg::direction_t::to_psd;
				this->named = true;
			}
		}

		inline auto parse(const int argc, const wchar_t* const argv[]) -> void
		{
			this->apply_program();

			bool positional_only{};

			for (int i{ 1 }; i < argc; ++i)
			{
				const std::wstring_view argument{ argv[i] };

				if (argument == L"-h" || argument == L"--help")
				{
					this->status = psdpimg::args::state::help;

					return;
				}

				if (argument == L"-pimg")
				{
					if (this->mode == psdpimg::direction_t::to_psd)
					{
						this->status = psdpimg::args::state::conflicting_switch;

						return;
					}

					this->mode = psdpimg::direction_t::to_pimg;

					continue;
				}

				if (argument == L"-psd")
				{
					if (this->mode == psdpimg::direction_t::to_pimg)
					{
						this->status = psdpimg::args::state::conflicting_switch;

						return;
					}

					this->mode = psdpimg::direction_t::to_psd;

					continue;
				}

				if (argument == L"--")
				{
					positional_only = true;

					continue;
				}

				if (argument.starts_with(L"-type="))
				{
					const bool applied{ psdpimg::apply_type(argument.substr(6), this->incoming) };

					if (!applied)
					{
						this->status = psdpimg::args::state::invalid_value;

						return;
					}

					continue;
				}

				if (psdpimg::append_filter(argument, this->incoming))
				{
					continue;
				}

				if (argument == L"-trim")
				{
					this->incoming.trim_names = true;

					continue;
				}

				if (argument == L"-crop")
				{
					this->incoming.crop_alpha = true;

					continue;
				}

				if (argument == L"-expand-markers")
				{
					this->expand_markers = true;

					continue;
				}

				const bool looks_like_switch
				{
					!positional_only &&
					argument.size() > 1 &&
					argument.front() == L'-'
				};

				if (looks_like_switch)
				{
					this->status = psdpimg::args::state::unknown_switch;

					return;
				}

				if (this->named || this->inputs.empty())
				{
					this->inputs.emplace_back(argument);

					continue;
				}

				if (this->output.empty())
				{
					this->output.assign(argument);

					continue;
				}

				this->status = psdpimg::args::state::too_many_arguments;

				return;
			}

			if (this->inputs.empty())
			{
				this->status = psdpimg::args::state::no_input;

				return;
			}

			const bool scanned
			{
				std::ranges::any_of
				(
					this->inputs, 
					[](const std::wstring& path)
					{
						return xfsys::is_directory(path);
					}
				)
			};

			const bool undecided{ this->mode == psdpimg::direction_t::detect };

			if (scanned && undecided)
			{
				this->status = psdpimg::args::state::ambiguous_direction;
			}
		}
	};
}
