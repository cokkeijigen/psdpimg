#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pimg/pimg_convert.hpp"
#include "psd/psd_document.hpp"

namespace psdpimg
{
	namespace
	{
		enum class format_t : uint8_t
		{
			unknown,
			psd,
			pimg,
		};

		enum class direction_t : uint8_t
		{
			detect,
			to_pimg,
			to_psd,
		};

		constexpr uint8_t signature_psd[4]{ '8', 'B', 'P', 'S' };
		constexpr uint8_t signature_psb[4]{ 'P', 'S', 'B', 0 };
		constexpr size_t  minimum_size{ 16 };
		constexpr int     exit_ok{ 0 };
		constexpr int     exit_failure{ 1 };
		constexpr int     exit_usage{ 2 };

		struct options
		{
			std::filesystem::path         input{};
			std::filesystem::path         output{};
			direction_t                   mode{ psdpimg::direction_t::detect };
			psdpimg::pimg::import_options incoming{};
			bool                          show_help{};
		};

		struct filter_switch
		{
			std::string_view           token{};
			psdpimg::pimg::filter_kind kind{};
			bool                       group{};
			bool                       list{};
		};

		constexpr filter_switch filter_switches[]
		{
			{ "-ignore=", psdpimg::pimg::filter_kind::equal, false, false },
			{ "-ignore-prefix=", psdpimg::pimg::filter_kind::prefix, false, false },
			{ "-ignore-suffix=", psdpimg::pimg::filter_kind::suffix, false, false },
			{ "-ignore-include=", psdpimg::pimg::filter_kind::contains, false, false },
			{ "-ignoregroup=", psdpimg::pimg::filter_kind::equal, true, false },
			{ "-ignoregroup-prefix=", psdpimg::pimg::filter_kind::prefix, true, false },
			{ "-ignoregroup-suffix=", psdpimg::pimg::filter_kind::suffix, true, false },
			{ "-ignoregroup-include=", psdpimg::pimg::filter_kind::contains, true, false },
			{ "-ignore-list=", psdpimg::pimg::filter_kind::pattern, false, true },
			{ "-ignoregroup-list=", psdpimg::pimg::filter_kind::pattern, true, true },
		};

		static auto load_list_file(const std::string_view path, std::vector<psdpimg::pimg::name_filter>& target) -> bool
		{
			std::ifstream stream{ std::string{ path }, std::ios::binary };
			const bool    opened{ stream.is_open() };

			if (!opened)
			{
				std::cerr << "cannot open the list file: " << path << '\n';

				return false;
			}

			const std::string body{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
			std::string_view  text{ body };

			if (text.starts_with("\xEF\xBB\xBF"))
			{
				text.remove_prefix(3);
			}

			size_t position{};
			size_t line_no{};

			while (position <= text.size())
			{
				const auto stop{ text.find('\n', position) };
				const auto end{ stop == std::string_view::npos ? text.size() : stop };
				auto       line{ text.substr(position, end - position) };

				++line_no;

				if (!line.empty() && line.back() == '\r')
				{
					line.remove_suffix(1);
				}

				if (!line.empty())
				{
					try
					{
						const std::regex probe{ std::string{ line } };

						target.push_back({ psdpimg::pimg::filter_kind::pattern, std::string{ line } });
					}
					catch (const std::regex_error&)
					{
						std::cerr << "invalid pattern in " << path << " on line " << line_no << ": " << line << '\n';

						return false;
					}
				}

				if (stop == std::string_view::npos)
				{
					break;
				}

				position = stop + 1;
			}

			return true;
		}

		static auto append_filter(const std::string_view argument, psdpimg::pimg::import_options& target, bool& accepted) -> bool
		{
			for (const auto& item : filter_switches)
			{
				if (!argument.starts_with(item.token))
				{
					continue;
				}

				auto& list{ item.group ? target.ignore_groups : target.ignore_layers };

				if (item.list)
				{
					accepted = psdpimg::load_list_file(argument.substr(item.token.size()), list);

					return true;
				}

				list.push_back({ item.kind, std::string{ argument.substr(item.token.size()) } });

				accepted = true;

				return true;
			}

			return false;
		}

		static auto print_usage() -> void
		{
			constexpr const char message[]
			{
				"usage:\n"
				"  psdpimg -pimg <input.psd>  [output.pimg] [import options]\n"
				"  psdpimg -psd  <input.pimg> [output.psd]\n"
				"\n"
				"  -pimg   build a pimg container from a PSD document\n"
				"  -psd    build a layered PSD document from a pimg container\n"
				"\n"
				"  Both switches are optional; the direction is detected from the\n"
				"  input signature when neither is given. The output path defaults\n"
				"  to the input directory with the matching extension.\n"
				"\n"
				"  import options (all optional, none is required):\n"
				"    -type=ui                preset: -ignore-include=rect -ignore-prefix=#\n"
				"                            -ignoregroup-prefix=# -trim -crop\n"
				"    -ignore=                layer name equals the text\n"
				"    -ignore-prefix=         layer name starts with the text\n"
				"    -ignore-suffix=         layer name ends with the text\n"
				"    -ignore-include=        layer name contains the text\n"
				"    -ignoregroup=           an ancestor group is named the text\n"
				"    -ignoregroup-prefix=    an ancestor group starts with the text\n"
				"    -ignoregroup-suffix=    an ancestor group ends with the text\n"
				"    -ignoregroup-include=   an ancestor group contains the text\n"
				"    -ignore-list=<file>     drop layers matching a pattern per line of the file\n"
				"    -ignoregroup-list=<file>  drop layers matching an ancestor group pattern per line\n"
				"    -trim                   strip leading and trailing blanks from layer names\n"
				"    -crop                   shrink every kept layer to its non transparent bounds\n"
				"\n"
				"  the text is taken literally, never a regex and never split on commas;\n"
				"  repeat a switch to add more filters, e.g. -ignore-prefix=# -ignore-prefix=;\n"
				"  a dropped layer keeps its rectangle but loses its bitmap, and is\n"
				"  written with layer_id -1, as psdconv does.\n"
				"\n"
				"  the list files hold one ECMAScript regex per line, blank lines ignored,\n"
				"  BOM and CRLF tolerated, no comment syntax; a bad pattern aborts the run.\n"
			};

			std::cout << message << std::endl;
		}

		static auto apply_type(const std::string_view kind, psdpimg::pimg::import_options& target) -> bool
		{
			const bool ui{ kind == "ui" };

			if (!ui)
			{
				std::cerr << "unsupported type: " << kind << '\n';

				return false;
			}

			const auto hash_prefix{ psdpimg::pimg::name_filter{ psdpimg::pimg::filter_kind::prefix, "#" } };

			target.ignore_layers = { { psdpimg::pimg::filter_kind::contains, "rect" }, hash_prefix };
			target.ignore_groups = { hash_prefix };
			target.trim_names    = true;
			target.crop_alpha    = true;

			return true;
		}

		static auto read_file(const std::filesystem::path& path) -> std::vector<uint8_t>
		{
			std::ifstream stream{ path, std::ios::binary };
			const bool opened{ stream.is_open() };

			if (!opened)
			{
				throw std::runtime_error{ "psdpimg::read_file: cannot open the input file" };
			}

			const std::istreambuf_iterator<char> begin{ stream };
			const std::istreambuf_iterator<char> end{};
			std::vector<uint8_t>                 data{ begin, end };

			return data;
		}

		static auto write_file(const std::filesystem::path& path, const std::span<const uint8_t> data) -> void
		{
			std::ofstream stream{ path, std::ios::binary | std::ios::trunc };
			const bool opened{ stream.is_open() };

			if (!opened)
			{
				throw std::runtime_error{ "psdpimg::write_file: cannot open the output file" };
			}

			const auto* const bytes{ reinterpret_cast<const char*>(data.data()) };
			const auto size{ static_cast<std::streamsize>(data.size()) };

			stream.write(bytes, size);

			const bool written{ stream.good() };

			if (!written)
			{
				throw std::runtime_error{ "psdpimg::write_file: cannot write the output file" };
			}
		}

		static auto parse(int argc, const char* args[], options& target) -> bool
		{
			bool positional_only{};

			for (int i{ 1 }; i < argc; ++i)
			{
				const std::string_view argument{ args[i] };
				const bool             is_pimg{ argument == "-pimg" };
				const bool             is_psd{ argument == "-psd" };
				const bool             is_help{ argument == "-h" || argument == "--help" };

				if (is_help)
				{
					target.show_help = true;

					return true;
				}

				if (is_pimg || is_psd)
				{
					const auto requested{ is_pimg ? psdpimg::direction_t::to_pimg : psdpimg::direction_t::to_psd };
					const bool conflict{ target.mode != psdpimg::direction_t::detect && target.mode != requested };

					if (conflict)
					{
						std::cerr << "conflicting switches: " << argument << '\n';

						return false;
					}

					target.mode = requested;

					continue;
				}

				if (argument == "--")
				{
					positional_only = true;

					continue;
				}

				if (argument.starts_with("-type="))
				{
					const auto kind{ argument.substr(6) };

					if (!psdpimg::apply_type(kind, target.incoming))
					{
						return false;
					}

					continue;
				}

				bool accepted{};

				if (psdpimg::append_filter(argument, target.incoming, accepted))
				{
					if (!accepted)
					{
						return false;
					}

					continue;
				}

				if (argument == "-trim")
				{
					target.incoming.trim_names = true;

					continue;
				}

				if (argument == "-crop")
				{
					target.incoming.crop_alpha = true;

					continue;
				}

				const bool looks_like_switch{ !positional_only && argument.size() > 1 && argument[0] == '-' };

				if (looks_like_switch)
				{
					std::cerr << "unknown switch: " << argument << '\n';

					return false;
				}

				if (target.input.empty())
				{
					target.input = std::filesystem::path{ argument };

					continue;
				}

				if (target.output.empty())
				{
					target.output = std::filesystem::path{ argument };

					continue;
				}

				std::cerr << "too many arguments: " << argument << '\n';

				return false;
			}

			return true;
		}

		static auto detect(const std::span<const uint8_t> bytes) noexcept -> format_t
		{
			if (bytes.size() < 4)
			{
				return psdpimg::format_t::unknown;
			}

			if (std::memcmp(bytes.data(), signature_psd, 4) == 0)
			{
				return psdpimg::format_t::psd;
			}

			if (std::memcmp(bytes.data(), signature_psb, 4) == 0)
			{
				return psdpimg::format_t::pimg;
			}

			return psdpimg::format_t::unknown;
		}

		static auto default_output(const std::filesystem::path& input, const direction_t target) -> std::filesystem::path
		{
			const auto extension{ target == psdpimg::direction_t::to_pimg ? ".pimg" : ".psd" };
			std::filesystem::path result{ input };

			result.replace_extension(extension);

			return result;
		}
	}

	extern "C" auto main(const int argc, const char* args[]) -> int
	{
		options options{};

		if (!psdpimg::parse(argc, args, options))
		{
			psdpimg::print_usage();

			return exit_usage;
		}

		if (options.show_help)
		{
			psdpimg::print_usage();

			return exit_ok;
		}

		if (options.input.empty())
		{
			std::cerr << "no input file\n";
			psdpimg::print_usage();

			return exit_usage;
		}

		try
		{
			const auto source{ psdpimg::read_file(options.input) };
			const auto input_format{ psdpimg::detect(source) };
			const bool too_small{ source.size() < minimum_size };

			if (too_small || input_format == psdpimg::format_t::unknown)
			{
				std::cerr << "unsupported input: " << options.input << '\n';

				return exit_failure;
			}

			direction_t target{ options.mode };

			if (target == psdpimg::direction_t::detect)
			{
				const bool is_psd_input{ input_format == psdpimg::format_t::psd };

				target = is_psd_input ? psdpimg::direction_t::to_pimg : psdpimg::direction_t::to_psd;
			}

			const bool needs_pimg_input{ target == psdpimg::direction_t::to_psd };
			const bool input_is_pimg{ input_format == psdpimg::format_t::pimg };
			const bool input_is_psd{ input_format == psdpimg::format_t::psd };
			const bool accepts{ needs_pimg_input ? (input_is_pimg || input_is_psd) : input_is_psd };

			if (!accepts)
			{
				std::cerr << "the switch does not match the input signature\n";

				return exit_failure;
			}

			std::vector<uint8_t> output{};
			size_t               layer_count{};

			if (target == psdpimg::direction_t::to_psd)
			{
				const auto document{ psdpimg::pimg::to_psd(source) };

				layer_count = document.layers.size();
				output      = psdpimg::psd::write(document);
			}
			else
			{
				const auto document{ psdpimg::psd::read(source) };

				layer_count = document.layers.size();
				output      = psdpimg::pimg::from_psd(document, options.incoming);
			}

			const bool auto_output{ options.output.empty() };
			const auto fallback{ psdpimg::default_output(options.input, target) };
			const auto destination{ auto_output ? fallback : options.output };

			psdpimg::write_file(destination, output);

			std::cout << destination << "  " << layer_count << " layers  " << output.size() << " bytes\n";

			return exit_ok;
		}
		catch (const std::exception& error)
		{
			std::cerr << "error: " << error.what() << '\n';

			return exit_failure;
		}
	}
}
