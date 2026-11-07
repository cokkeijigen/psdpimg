#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <exception>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include <console.hpp>
#include <xfsys.hpp>
#include <xstr.hpp>

#include <pimg_convert.hpp>
#include <psd_document.hpp>
#include <args.hpp>

console::helper_t console::helper{ L"" PROJECT_NAME " v" PROJECT_VERSION };

namespace psdpimg
{

	static constexpr char usage_text[]
	{
		"usage:\n"
		"  psdpimg -pimg <input.psd>  [output.pimg] [import options]\n"
		"  psdpimg -psd  <input.pimg> [output.psd]\n"
		"  psdpimg -pimg <directory>  [output directory] [import options]\n"
		"  psdpimg -psd  <directory>  [output directory]\n"
		"  psd2pimg <input.psd>  <input.psd>  ...  [import options]\n"
		"  pimg2psd <input.pimg> <input.pimg> ...\n"
		"  psd2pimg <directory>  [import options]\n"
		"  pimg2psd <directory>\n"
		"\n"
		"  -pimg   build a pimg container from a PSD document\n"
		"  -psd    build a layered PSD document from a pimg container\n"
		"\n"
		"  For a single file both switches are optional; the direction is then\n"
		"  detected from the input signature. A directory always needs one of\n"
		"  them, because it may hold both kinds of files.\n"
		"\n"
		"  Renaming the executable to psd2pimg.exe or pimg2psd.exe fixes the\n"
		"  direction from the name. Every argument is then an input, no output\n"
		"  path is accepted, and each result is written next to its source:\n"
		"    psd2pimg a.psd b.psd     -> a.pimg b.pimg\n"
		"    pimg2psd a.pimg b.pimg   -> a.psd  b.psd\n"
		"\n"
		"  A directory is scanned without recursion: only *.psd are taken when\n"
		"  building pimg, only *.pimg when building psd. The results go to the\n"
		"  input directory unless another one is given; a missing output\n"
		"  directory is created. A directory without a single match is an error.\n"
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
		"\n"
		"  export options (-psd only):\n"
		"    -expand-markers       write every marker layer separately with a\n"
		"                          #lyid#N suffix; the default merges them back into\n"
		"                          the single ## positioning layer of the source PSD.\n"
	};

	constexpr uint8_t signature_psd[4]{ '8', 'B', 'P', 'S' };
	constexpr uint8_t signature_psb[4]{ 'P', 'S', 'B', 0   };

	constexpr size_t minimum_size { 16 };
	constexpr int    exit_ok      { 0 };
	constexpr int    exit_failure { 1 };
	constexpr int    exit_usage   { 2 };

	struct task
	{
		std::wstring source{};
		std::wstring destination{};
	};

	static auto expand(const psdpimg::args& args, std::vector<psdpimg::task>& tasks) -> bool
	{
		const bool to_pimg   { args.mode == psdpimg::direction_t::to_pimg };
		const bool undecided { args.mode == psdpimg::direction_t::detect };
		const bool has_output{ !args.output.empty() };

		const std::wstring_view wanted  { to_pimg ? L".psd"  : L".pimg" };
		const std::wstring_view produced{ to_pimg ? L".pimg" : L".psd"  };

		for (const std::wstring& input : args.inputs)
		{
			if (!xfsys::is_directory(input))
			{
				std::wstring destination{ args.output };

				if (destination.empty() && !undecided)
				{
					destination = xfsys::extname_change(input, produced);
				}

				tasks.push_back({ .source{ input }, .destination{ destination } });

				continue;
			}

			const std::wstring_view target{ has_output ? std::wstring_view{ args.output } : std::wstring_view{ input } };

			if (has_output && !xfsys::create_directory(args.output))
			{
				const xstr::str errmsg{ L"cannot create the output directory: ", args.output };

				xcout::helper.set_attrs(xcout::attrs::text_dark_red)
							 .writeline(errmsg)
							 .reset_attrs();

				return false;
			}

			size_t found{};

			std::wstring scan{ input };
			{
				const DWORD needed{ ::GetFullPathNameW(input.c_str(), 0, nullptr, nullptr) };

				if (needed > 0)
				{
					std::wstring full{};
					full.resize(needed);

					const DWORD written{ ::GetFullPathNameW(input.c_str(), needed, full.data(), nullptr) };

					if (written > 0 && written < needed)
					{
						full.resize(written);

						scan = std::move(full);
					}
				}
			}

			for (const auto& entry : xfsys::directory::wstring_path_iterator{ scan })
			{
				if (!entry.is_file())
				{
					continue;
				}

				const std::wstring_view name{ entry.name() };

				if (name.size() <= wanted.size())
				{
					continue;
				}

				std::wstring tail{ name.substr(name.size() - wanted.size()) };

				for (wchar_t& ch : tail)
				{
					ch = static_cast<wchar_t>(::towlower(ch));
				}

				if (tail != wanted)
				{
					continue;
				}

				tasks.push_back
				({
					.source{ entry.full_path() },
					.destination{ xfsys::path::join(target, xfsys::extname_change(name, produced)) }
				});

				++found;
			}

			if (found == 0)
			{
				const xstr::str errmsg{ L"no matching file in the input directory: ", input };

				xcout::helper.set_attrs(xcout::attrs::text_dark_red)
							 .writeline(errmsg)
							 .reset_attrs();

				return false;
			}
		}

		std::ranges::sort(tasks, [](const psdpimg::task& left, const psdpimg::task& right)
		{
			return left.source < right.source;
		});

		return true;
	}

	static auto convert(const psdpimg::args& args, const std::wstring& source_path,
		const std::wstring& destination_hint) -> bool
	{
		const xfsys::file source{ xfsys::open(source_path, xfsys::read, false) };

		if (!source.is_open())
		{
			const xstr::str errmsg{ L"cannot open the input file: ", source_path };

			xcout::helper.set_attrs(xcout::attrs::text_dark_red)
						 .writeline(errmsg)
						 .reset_attrs();

			return false;
		}

		std::vector<uint8_t> input  {};
		const size_t         length { source.size() };

		if (source.read(input, length) != length)
		{
			const xstr::str errmsg{ L"cannot read the input file: ", source_path };

			xcout::helper.set_attrs(xcout::attrs::text_dark_red)
						 .writeline(errmsg)
						 .reset_attrs();

			return false;
		}

		const bool sized   { input.size() >= 4 };
		const bool is_psd  { sized && std::memcmp(input.data(), psdpimg::signature_psd, 4) == 0 };
		const bool is_pimg { sized && std::memcmp(input.data(), psdpimg::signature_psb, 4) == 0 };

		if (input.size() < psdpimg::minimum_size || (!is_psd && !is_pimg))
		{
			const xstr::str errmsg{ L"unsupported input: ", source_path };

			xcout::helper.set_attrs(xcout::attrs::text_dark_red)
						 .writeline(errmsg)
						 .reset_attrs();

			return false;
		}

		psdpimg::direction_t direction{ args.mode };

		if (direction == psdpimg::direction_t::detect)
		{
			direction = is_psd ? psdpimg::direction_t::to_pimg : psdpimg::direction_t::to_psd;
		}

		const bool needs_pimg_input { direction == psdpimg::direction_t::to_psd };
		const bool accepts          { needs_pimg_input ? (is_pimg || is_psd) : is_psd };

		if (!accepts)
		{
			const xstr::str errmsg{ L"the switch does not match the input signature: ", source_path };

			xcout::helper.set_attrs(xcout::attrs::text_dark_red)
						 .writeline(errmsg)
						 .reset_attrs();

			return false;
		}

		std::vector<uint8_t> output      {};
		size_t               layer_count {};

		if (direction == psdpimg::direction_t::to_psd)
		{
			const psdpimg::pimg::export_options options
			{
				.expand_markers{ args.expand_markers }
			};
			const psdpimg::psd::document document{ psdpimg::pimg::to_psd(input, options) };

			layer_count = document.layers.size();
			output      = psdpimg::psd::write(document);
		}
		else
		{
			const psdpimg::psd::document document{ psdpimg::psd::read(input) };

			layer_count = document.layers.size();
			output      = psdpimg::pimg::from_psd(document, args.incoming);
		}

		const std::wstring_view extension
		{
			direction == psdpimg::direction_t::to_pimg ?
			L".pimg" : L".psd"
		};

		const std::wstring destination
		{
			!destination_hint.empty() ? destination_hint :
			xfsys::extname_change(source_path, extension)
		};

		const xfsys::file output_file{ xfsys::create(destination) };

		if (!output_file.is_open())
		{
			const xstr::str errmsg{ L"cannot create the output file: ", destination };

			xcout::helper.set_attrs(xcout::attrs::text_dark_red)
						 .writeline(errmsg)
						 .reset_attrs();

			return false;
		}

		if (output_file.write(output.data(), output.size()) != output.size())
		{
			const xstr::str errmsg{ L"cannot write the output file: ", destination };

			xcout::helper.set_attrs(xcout::attrs::text_dark_red)
						 .writeline(errmsg)
						 .reset_attrs();

			return false;
		}

		const xstr::str message
		{
			destination,                    L"  ",
			std::to_wstring(layer_count),   L" layers  ",
			std::to_wstring(output.size()), L" bytes"
		};

		xcout::helper.writeline(message);

		return true;
	}

	static auto main(const psdpimg::args& args) noexcept -> int
	{
		try
		{
			if (args.status == psdpimg::args::state::help)
			{
				xcout::helper.writeline(psdpimg::usage_text);
				return { psdpimg::exit_ok };
			}

			if (args.status != psdpimg::args::state::ok)
			{
				xcout::helper.set_attrs(xcout::attrs::text_dark_red);
				switch (args.status)
				{
				case psdpimg::args::state::no_input:
				{
					xcout::helper.writeline(L"no input file");
					break;
				}
				case psdpimg::args::state::unknown_switch:
				{
					xcout::helper.writeline(L"unknown switch");
					break;
				}
				case psdpimg::args::state::conflicting_switch:
				{
					xcout::helper.writeline(L"conflicting switches");
					break;
				}
				case psdpimg::args::state::too_many_arguments:
				{
					xcout::helper.writeline(L"too many arguments");
					break;
				}
				case psdpimg::args::state::invalid_value:
				{
					xcout::helper.writeline(L"invalid option value");
					break;
				}
				case psdpimg::args::state::ambiguous_direction:
				{
					xcout::helper.writeline(L"a directory needs -pimg or -psd");
					break;
				}
				};

				xcout::helper.reset_attrs();
				xcout::helper.writeline(psdpimg::usage_text);

				return { psdpimg::exit_usage };
			}

			std::vector<psdpimg::task> tasks{};

			if (!psdpimg::expand(args, tasks))
			{
				return { psdpimg::exit_failure };
			}

			for (const psdpimg::task& item : tasks)
			{
				if (!psdpimg::convert(args, item.source, item.destination))
				{
					return { psdpimg::exit_failure };
				}
			}

			if (tasks.size() > 1)
			{
				const xstr::str summary{ L"converted ", std::to_wstring(tasks.size()), L" files" };

				xcout::helper.writeline(summary);
			}

			return { psdpimg::exit_ok };
		}
		catch (const std::exception& error)
		{
			xcout::helper.set_attrs(xcout::attrs::text_dark_red)
						  .writeline(error.what())
						  .reset_attrs();
		}

		return { psdpimg::exit_failure };
	}

	extern "C" auto main(void) -> int
	{
		int           argc{};
		const LPWSTR  cmds{ ::GetCommandLineW() };
		const LPWSTR* argv{ ::CommandLineToArgvW(cmds, &argc) };

		return psdpimg::main(psdpimg::args{ argc, argv });
	}
}
