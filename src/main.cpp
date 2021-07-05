// #define NO_ARRAY_BOUNDS_CHECK
#include <ctype.h>
#include "common.cpp"
#include "timings.cpp"
#include "tokenizer.cpp"
#include "big_int.cpp"
#include "exact_value.cpp"
#include "build_settings.cpp"


gb_global Timings global_timings = {0};

#if defined(GB_SYSTEM_WINDOWS)
#include "llvm-c/Types.h"
#else
#include <llvm-c/Types.h>
#endif

#include "parser.hpp"
#include "checker.hpp"

#include "parser.cpp"
#include "checker.cpp"
#include "docs.cpp"


#include "llvm_backend.cpp"

#if defined(GB_SYSTEM_OSX)
	#include <llvm/Config/llvm-config.h>
	#if LLVM_VERSION_MAJOR < 11
	#error LLVM Version 11+ is required => "brew install llvm@11"
	#endif
#endif

#include "query_data.cpp"


#if defined(GB_SYSTEM_WINDOWS)
// NOTE(IC): In order to find Visual C++ paths without relying on environment variables.
#include "microsoft_craziness.h"
#endif


// NOTE(bill): 'name' is used in debugging and profiling modes
i32 system_exec_command_line_app(char const *name, char const *fmt, ...) {
#if defined(GB_SYSTEM_WINDOWS)
	STARTUPINFOW start_info = {gb_size_of(STARTUPINFOW)};
	PROCESS_INFORMATION pi = {0};
	isize cmd_len = 0;
	isize const cmd_cap = 4096;
	char cmd_line[cmd_cap] = {};
	va_list va;
	gbTempArenaMemory tmp;
	String16 cmd;
	i32 exit_code = 0;


	start_info.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	start_info.wShowWindow = SW_SHOW;
	start_info.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);
	start_info.hStdOutput  = GetStdHandle(STD_OUTPUT_HANDLE);
	start_info.hStdError   = GetStdHandle(STD_ERROR_HANDLE);

	va_start(va, fmt);
	cmd_len = gb_snprintf_va(cmd_line, cmd_cap-1, fmt, va);
	va_end(va);

	if (build_context.show_system_calls) {
		gb_printf_err("[SYSTEM CALL] %s\n", name);
		gb_printf_err("%.*s\n\n", cast(int)(cmd_len-1), cmd_line);
	}

	tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));

	cmd = string_to_string16(string_buffer_allocator, make_string(cast(u8 *)cmd_line, cmd_len-1));
	if (CreateProcessW(nullptr, cmd.text,
	                   nullptr, nullptr, true, 0, nullptr, nullptr,
	                   &start_info, &pi)) {
		WaitForSingleObject(pi.hProcess, INFINITE);
		GetExitCodeProcess(pi.hProcess, cast(DWORD *)&exit_code);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	} else {
		// NOTE(bill): failed to create process
		gb_printf_err("Failed to execute command:\n\t%s\n", cmd_line);
		exit_code = -1;
	}

	if (exit_code) {
		exit(exit_code);
	}

	return exit_code;

#elif defined(GB_SYSTEM_OSX) || defined(GB_SYSTEM_UNIX)

	char cmd_line[4096] = {0};
	isize cmd_len;
	va_list va;
	String cmd;
	i32 exit_code = 0;

	va_start(va, fmt);
	cmd_len = gb_snprintf_va(cmd_line, gb_size_of(cmd_line), fmt, va);
	va_end(va);
	cmd = make_string(cast(u8 *)&cmd_line, cmd_len-1);

	if (build_context.show_system_calls) {
		gb_printf_err("[SYSTEM CALL] %s\n", name);
		gb_printf_err("%s\n\n", cmd_line);
	}
	exit_code = system(cmd_line);
	// pid_t pid = fork();
	// int status = 0;

	// if(pid == 0) {
	// 	// in child, pid == 0.
	// 	int ret = execvp(cmd.text, (char* const*) cmd.text);

	// 	if(ret == -1) {
	// 		gb_printf_err("Failed to execute command:\n\t%s\n", cmd_line);

	// 		// we're in the child, so returning won't do us any good -- just quit.
	// 		exit(-1);
	// 	}

	// 	// unreachable
	// 	abort();
	// } else {
	// 	// wait for child to finish, then we can continue cleanup

	// 	int s = 0;
	// 	waitpid(pid, &s, 0);

	// 	status = WEXITSTATUS(s);
	// }

	// exit_code = status
	return exit_code;
#endif
}




i32 linker_stage(lbGenerator *gen) {
  i32 result = 0;
  Timings *timings = &global_timings;

	String output_base = gen->output_base;

	if (is_arch_wasm()) {
		timings_start_section(timings, str_lit("wasm-ld"));
		system_exec_command_line_app("wasm-ld",
			"\"%.*s\\bin\\wasm-ld\" \"%.*s.wasm-obj\" -o \"%.*s.wasm\" %.*s %.*s",
			LIT(build_context.ODIN_ROOT),
			LIT(output_base), LIT(output_base), LIT(build_context.link_flags), LIT(build_context.extra_linker_flags));
	}

	if (build_context.cross_compiling && selected_target_metrics->metrics == &target_essence_amd64) {
#ifdef GB_SYSTEM_UNIX
		result = system_exec_command_line_app("linker", "x86_64-essence-gcc \"%.*s.o\" -o \"%.*s\" %.*s %.*s",
			LIT(output_base), LIT(output_base), LIT(build_context.link_flags), LIT(build_context.extra_linker_flags));
#else
		gb_printf_err("Linking for cross compilation for this platform is not yet supported (%.*s %.*s)\n",
			LIT(target_os_names[build_context.metrics.os]),
			LIT(target_arch_names[build_context.metrics.arch])
		);
#endif
	} else if (build_context.cross_compiling && build_context.different_os) {
		gb_printf_err("Linking for cross compilation for this platform is not yet supported (%.*s %.*s)\n",
			LIT(target_os_names[build_context.metrics.os]),
			LIT(target_arch_names[build_context.metrics.arch])
		);
		build_context.keep_object_files = true;
	} else {
	#if defined(GB_SYSTEM_WINDOWS)
		String section_name = str_lit("msvc-link");
		if (build_context.use_lld) {
			section_name = str_lit("lld-link");
		}
		timings_start_section(timings, section_name);

		gbString lib_str = gb_string_make(heap_allocator(), "");
		defer (gb_string_free(lib_str));
		char lib_str_buf[1024] = {0};

		char const *output_ext = "exe";
		gbString link_settings = gb_string_make_reserve(heap_allocator(), 256);
		defer (gb_string_free(link_settings));


		// NOTE(ic): It would be nice to extend this so that we could specify the Visual Studio version that we want instead of defaulting to the latest.
		Find_Result_Utf8 find_result = find_visual_studio_and_windows_sdk_utf8();

		if (find_result.windows_sdk_version == 0) {
			gb_printf_err("Windows SDK not found.\n");
			exit(1);
		}

		if (build_context.ignore_microsoft_magic) {
			find_result = {};
		}

		// Add library search paths.
		if (find_result.vs_library_path.len > 0) {
			GB_ASSERT(find_result.windows_sdk_um_library_path.len > 0);
			GB_ASSERT(find_result.windows_sdk_ucrt_library_path.len > 0);

			String path = {};
			auto add_path = [&](String path) {
				if (path[path.len-1] == '\\') {
					path.len -= 1;
				}
				link_settings = gb_string_append_fmt(link_settings, " /LIBPATH:\"%.*s\"", LIT(path));
			};
			add_path(find_result.windows_sdk_um_library_path);
			add_path(find_result.windows_sdk_ucrt_library_path);
			add_path(find_result.vs_library_path);
		}

		for_array(j, gen->modules.entries) {
			lbModule *m = gen->modules.entries[j].value;
			for_array(i, m->foreign_library_paths) {
				String lib = m->foreign_library_paths[i];
				GB_ASSERT(lib.len < gb_count_of(lib_str_buf)-1);
				isize len = gb_snprintf(lib_str_buf, gb_size_of(lib_str_buf),
				                        " \"%.*s\"", LIT(lib));
				lib_str = gb_string_appendc(lib_str, lib_str_buf);
			}
		}

		for_array(i, gen->default_module.foreign_library_paths) {
			String lib = gen->default_module.foreign_library_paths[i];
			GB_ASSERT(lib.len < gb_count_of(lib_str_buf)-1);
			isize len = gb_snprintf(lib_str_buf, gb_size_of(lib_str_buf),
			                        " \"%.*s\"", LIT(lib));
			lib_str = gb_string_appendc(lib_str, lib_str_buf);
		}



		if (build_context.build_mode == BuildMode_DynamicLibrary) {
			output_ext = "dll";
			link_settings = gb_string_append_fmt(link_settings, " /DLL");
		} else {
			link_settings = gb_string_append_fmt(link_settings, " /ENTRY:mainCRTStartup");
		}

		if (build_context.pdb_filepath != "") {
			link_settings = gb_string_append_fmt(link_settings, " /PDB:%.*s", LIT(build_context.pdb_filepath));
		}

		if (build_context.no_crt) {
			link_settings = gb_string_append_fmt(link_settings, " /nodefaultlib");
		} else {
			link_settings = gb_string_append_fmt(link_settings, " /defaultlib:libcmt");
		}

		if (build_context.ODIN_DEBUG) {
			link_settings = gb_string_append_fmt(link_settings, " /DEBUG");
		}

		gbString object_files = gb_string_make(heap_allocator(), "");
		defer (gb_string_free(object_files));
		for_array(i, gen->output_object_paths) {
			String object_path = gen->output_object_paths[i];
			object_files = gb_string_append_fmt(object_files, "\"%.*s\" ", LIT(object_path));
		}

		char const *subsystem_str = build_context.use_subsystem_windows ? "WINDOWS" : "CONSOLE";
		if (!build_context.use_lld) { // msvc
			if (build_context.has_resource) {
				result = system_exec_command_line_app("msvc-link",
					"\"rc.exe\" /nologo /fo \"%.*s.res\" \"%.*s.rc\"",
					LIT(output_base),
					LIT(build_context.resource_filepath)
				);

				if (result == 0) {
					result = system_exec_command_line_app("msvc-link",
						"\"%.*slink.exe\" %s \"%.*s.res\" -OUT:\"%.*s.%s\" %s "
						"/nologo /incremental:no /opt:ref /subsystem:%s "
						" %.*s "
						" %.*s "
						" %s "
						"",
						LIT(find_result.vs_exe_path), object_files, LIT(output_base), LIT(output_base), output_ext,
						link_settings,
						subsystem_str,
						LIT(build_context.link_flags),
						LIT(build_context.extra_linker_flags),
						lib_str
					  );
				}
			} else {
				result = system_exec_command_line_app("msvc-link",
					"\"%.*slink.exe\" %s -OUT:\"%.*s.%s\" %s "
					"/nologo /incremental:no /opt:ref /subsystem:%s "
					" %.*s "
					" %.*s "
					" %s "
					"",
					LIT(find_result.vs_exe_path), object_files, LIT(output_base), output_ext,
					link_settings,
					subsystem_str,
					LIT(build_context.link_flags),
					LIT(build_context.extra_linker_flags),
					lib_str
				);
			}
		} else { // lld
			  result = system_exec_command_line_app("msvc-lld-link",
				"\"%.*s\\bin\\lld-link\" %s -OUT:\"%.*s.%s\" %s "
				"/nologo /incremental:no /opt:ref /subsystem:%s "
				" %.*s "
				" %.*s "
				" %s "
				"",
				LIT(build_context.ODIN_ROOT), object_files, LIT(output_base),output_ext,
				link_settings,
				subsystem_str,
				LIT(build_context.link_flags),
				LIT(build_context.extra_linker_flags),
				lib_str
			);
		}
	#else
		timings_start_section(timings, str_lit("ld-link"));

		// NOTE(vassvik): get cwd, for used for local shared libs linking, since those have to be relative to the exe
		char cwd[256];
		getcwd(&cwd[0], 256);
		//printf("%s\n", cwd);

		// NOTE(vassvik): needs to add the root to the library search paths, so that the full filenames of the library
		//                files can be passed with -l:
		gbString lib_str = gb_string_make(heap_allocator(), "-L/");
		defer (gb_string_free(lib_str));

		for_array(i, gen->default_module.foreign_library_paths) {
			String lib = gen->default_module.foreign_library_paths[i];

			// NOTE(zangent): Sometimes, you have to use -framework on MacOS.
			//   This allows you to specify '-f' in a #foreign_system_library,
			//   without having to implement any new syntax specifically for MacOS.
			#if defined(GB_SYSTEM_OSX)
				if (string_ends_with(lib, str_lit(".framework"))) {
					// framework thingie
					String lib_name = lib;
					lib_name = remove_extension_from_path(lib_name);
					lib_str = gb_string_append_fmt(lib_str, " -framework %.*s ", LIT(lib_name));
				} else if (string_ends_with(lib, str_lit(".a")) || string_ends_with(lib, str_lit(".o")) || string_ends_with(lib, str_lit(".dylib"))) {
					// For:
					// object
					// dynamic lib
					// static libs, absolute full path relative to the file in which the lib was imported from
					lib_str = gb_string_append_fmt(lib_str, " %.*s ", LIT(lib));
				} else {
					// dynamic or static system lib, just link regularly searching system library paths
					lib_str = gb_string_append_fmt(lib_str, " -l%.*s ", LIT(lib));
				}
			#else
				// NOTE(vassvik): static libraries (.a files) in linux can be linked to directly using the full path,
				//                since those are statically linked to at link time. shared libraries (.so) has to be
				//                available at runtime wherever the executable is run, so we make require those to be
				//                local to the executable (unless the system collection is used, in which case we search
				//                the system library paths for the library file).
				if (string_ends_with(lib, str_lit(".a"))) {
					// static libs, absolute full path relative to the file in which the lib was imported from
					lib_str = gb_string_append_fmt(lib_str, " -l:\"%.*s\" ", LIT(lib));
				} else if (string_ends_with(lib, str_lit(".so"))) {
					// dynamic lib, relative path to executable
					// NOTE(vassvik): it is the user's responsibility to make sure the shared library files are visible
					//                at runtimeto the executable
					lib_str = gb_string_append_fmt(lib_str, " -l:\"%s/%.*s\" ", cwd, LIT(lib));
				} else {
					// dynamic or static system lib, just link regularly searching system library paths
					lib_str = gb_string_append_fmt(lib_str, " -l%.*s ", LIT(lib));
				}
			#endif
		}

		gbString object_files = gb_string_make(heap_allocator(), "");
		defer (gb_string_free(object_files));
		for_array(i, gen->output_object_paths) {
			String object_path = gen->output_object_paths[i];
			object_files = gb_string_append_fmt(object_files, "\"%.*s\" ", LIT(object_path));
		}

		// Unlike the Win32 linker code, the output_ext includes the dot, because
		// typically executable files on *NIX systems don't have extensions.
		String output_ext = {};
		gbString link_settings = gb_string_make_reserve(heap_allocator(), 32);
		char const *linker;
		if (build_context.build_mode == BuildMode_DynamicLibrary) {
			// NOTE(tetra, 2020-11-06): __$startup_runtime must be called at DLL load time.
			// Clang, for some reason, won't let us pass the '-init' flag that lets us do this,
			// so use ld instead.
			// :UseLDForShared
			linker = "ld";
			link_settings = gb_string_appendc(link_settings, "-init '__$startup_runtime' ");
			// Shared libraries are .dylib on MacOS and .so on Linux.
			#if defined(GB_SYSTEM_OSX)
				output_ext = STR_LIT(".dylib");
				link_settings = gb_string_appendc(link_settings, "-dylib -dynamic ");
			#else
				output_ext = STR_LIT(".so");
				link_settings = gb_string_appendc(link_settings, "-shared ");
			#endif
		} else {
			#if defined(GB_SYSTEM_OSX)
				linker = "ld";
			#else
				// TODO(zangent): Figure out how to make ld work on Linux.
				//   It probably has to do with including the entire CRT, but
				//   that's quite a complicated issue to solve while remaining distro-agnostic.
				//   Clang can figure out linker flags for us, and that's good enough _for now_.
				linker = "clang -Wno-unused-command-line-argument";
			#endif
		}

		if (build_context.metrics.os == TargetOs_linux) {
			link_settings = gb_string_appendc(link_settings, "-no-pie ");
		}


		if (build_context.out_filepath.len > 0) {
			//NOTE(thebirk): We have a custom -out arguments, so we should use the extension from that
			isize pos = string_extension_position(build_context.out_filepath);
			if (pos > 0) {
				output_ext = substring(build_context.out_filepath, pos, build_context.out_filepath.len);
			}
		}

		result = system_exec_command_line_app("ld-link",
			"%s %s -o \"%.*s%.*s\" %s "
			" %s "
			" %.*s "
			" %.*s "
			" %s "
			#if defined(GB_SYSTEM_OSX)
				// This sets a requirement of Mountain Lion and up, but the compiler doesn't work without this limit.
				// NOTE: If you change this (although this minimum is as low as you can go with Odin working)
				//       make sure to also change the 'mtriple' param passed to 'opt'
				#if defined(GB_CPU_ARM)
				" -macosx_version_min 11.0.0 "
				#else
				" -macosx_version_min 10.8.0 "
				#endif
				// This points the linker to where the entry point is
				" -e _main "
			#endif
			, linker, object_files, LIT(output_base), LIT(output_ext),
      			#if defined(GB_SYSTEM_OSX)
        			"-lSystem -lm -syslibroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -L/usr/local/lib",
      			#else
        			"-lc -lm",
      			#endif
      			lib_str,
			LIT(build_context.link_flags),
			LIT(build_context.extra_linker_flags),
			link_settings);

	#if defined(GB_SYSTEM_OSX)
		if (build_context.ODIN_DEBUG) {
			// NOTE: macOS links DWARF symbols dynamically. Dsymutil will map the stubs in the exe
			// to the symbols in the object file
			system_exec_command_line_app("dsymutil",
				"dsymutil %.*s%.*s", LIT(output_base), LIT(output_ext)
			);
		}
	#endif

	#endif
	}

	return result;
}

Array<String> setup_args(int argc, char const **argv) {
	gbAllocator a = heap_allocator();

#if defined(GB_SYSTEM_WINDOWS)
	int wargc = 0;
	wchar_t **wargv = command_line_to_wargv(GetCommandLineW(), &wargc);
	auto args = array_make<String>(a, 0, wargc);
	for (isize i = 0; i < wargc; i++) {
		wchar_t *warg = wargv[i];
		isize wlen = string16_len(warg);
		String16 wstr = make_string16(warg, wlen);
		String arg = string16_to_string(a, wstr);
		if (arg.len > 0) {
			array_add(&args, arg);
		}
	}
	return args;
#else
	auto args = array_make<String>(a, 0, argc);
	for (isize i = 0; i < argc; i++) {
		String arg = make_string_c(argv[i]);
		if (arg.len > 0) {
			array_add(&args, arg);
		}
	}
	return args;
#endif
}





void print_usage_line(i32 indent, char const *fmt, ...) {
	while (indent --> 0) {
		gb_printf_err("\t");
	}
	va_list va;
	va_start(va, fmt);
	gb_printf_err_va(fmt, va);
	va_end(va);
	gb_printf_err("\n");
}

void usage(String argv0) {
	print_usage_line(0, "%.*s is a tool for managing Odin source code", LIT(argv0));
	print_usage_line(0, "Usage:");
	print_usage_line(1, "%.*s command [arguments]", LIT(argv0));
	print_usage_line(0, "Commands:");
	print_usage_line(1, "build     compile .odin file, or directory of .odin files, as an executable.");
	print_usage_line(1, "          one must contain the program's entry point, all must be in the same package.");
	print_usage_line(1, "run       same as 'build', but also then runs the newly compiled executable.");
	print_usage_line(1, "check     parse and type check .odin file");
	print_usage_line(1, "query     parse, type check, and output a .json file containing information about the program");
	print_usage_line(1, "doc       generate documentation .odin file, or directory of .odin files");
	print_usage_line(1, "version   print version");
	print_usage_line(0, "");
	print_usage_line(0, "For more information of flags, apply the flag to see what is possible");
	print_usage_line(1, "-help");
}


bool string_is_valid_identifier(String str) {
	if (str.len <= 0) return false;

	isize rune_count = 0;

	isize w = 0;
	isize offset = 0;
	while (offset < str.len) {
		Rune r = 0;
		w = gb_utf8_decode(str.text, str.len, &r);
		if (r == GB_RUNE_INVALID) {
			return false;
		}

		if (rune_count == 0) {
			if (!rune_is_letter(r)) {
				return false;
			}
		} else {
			if (!rune_is_letter(r) && !rune_is_digit(r)) {
				return false;
			}
		}
		rune_count += 1;
		offset += w;
	}

	return true;
}

enum BuildFlagKind {
	BuildFlag_Invalid,

	BuildFlag_Help,

	BuildFlag_OutFile,
	BuildFlag_OptimizationLevel,
	BuildFlag_OptimizationMode,
	BuildFlag_ShowTimings,
	BuildFlag_ShowUnused,
	BuildFlag_ShowUnusedWithLocation,
	BuildFlag_ShowMoreTimings,
	BuildFlag_ShowSystemCalls,
	BuildFlag_ThreadCount,
	BuildFlag_KeepTempFiles,
	BuildFlag_Collection,
	BuildFlag_Define,
	BuildFlag_BuildMode,
	BuildFlag_Target,
	BuildFlag_Debug,
	BuildFlag_DisableAssert,
	BuildFlag_NoBoundsCheck,
	BuildFlag_NoDynamicLiterals,
	BuildFlag_NoCRT,
	BuildFlag_NoEntryPoint,
	BuildFlag_UseLLD,
	BuildFlag_UseSeparateModules,
	BuildFlag_Vet,
	BuildFlag_VetExtra,
	BuildFlag_UseLLVMApi,
	BuildFlag_IgnoreUnknownAttributes,
	BuildFlag_ExtraLinkerFlags,
	BuildFlag_Microarch,

	BuildFlag_TestName,

	BuildFlag_DisallowDo,
	BuildFlag_DefaultToNilAllocator,
	BuildFlag_InsertSemicolon,
	BuildFlag_StrictStyle,

	BuildFlag_Compact,
	BuildFlag_GlobalDefinitions,
	BuildFlag_GoToDefinitions,

	BuildFlag_Short,
	BuildFlag_AllPackages,
	BuildFlag_DocFormat,

	BuildFlag_IgnoreWarnings,
	BuildFlag_WarningsAsErrors,
	BuildFlag_VerboseErrors,

#if defined(GB_SYSTEM_WINDOWS)
	BuildFlag_IgnoreVsSearch,
	BuildFlag_ResourceFile,
	BuildFlag_WindowsPdbName,
	BuildFlag_Subsystem,
#endif

	BuildFlag_COUNT,
};

enum BuildFlagParamKind {
	BuildFlagParam_None,

	BuildFlagParam_Boolean,
	BuildFlagParam_Integer,
	BuildFlagParam_Float,
	BuildFlagParam_String,

	BuildFlagParam_COUNT,
};

struct BuildFlag {
	BuildFlagKind      kind;
	String             name;
	BuildFlagParamKind param_kind;
	u32                command_support;
	bool               allow_mulitple;
};


void add_flag(Array<BuildFlag> *build_flags, BuildFlagKind kind, String name, BuildFlagParamKind param_kind, u32 command_support, bool allow_mulitple=false) {
	BuildFlag flag = {kind, name, param_kind, command_support, allow_mulitple};
	array_add(build_flags, flag);
}

ExactValue build_param_to_exact_value(String name, String param) {
	ExactValue value = {};
	if (str_eq_ignore_case(param, str_lit("t")) ||
	    str_eq_ignore_case(param, str_lit("true"))) {
		value = exact_value_bool(true);
	} else if (str_eq_ignore_case(param, str_lit("f")) ||
	           str_eq_ignore_case(param, str_lit("false"))) {
		value = exact_value_bool(false);
	} else if (param.len > 0) {
		if (isalpha(param[0]) && !isdigit(param[0])) {
			value = exact_value_string(param);
		} else if (param[0] == '-' || param[0] == '+' || gb_is_between(param[0], '0', '9')) {
			if (string_contains_char(param, '.')) {
				value = exact_value_float_from_string(param);
			} else {
				value = exact_value_integer_from_string(param);
			}
			if (value.kind == ExactValue_Invalid) {
				gb_printf_err("Invalid flag parameter for '%.*s' = '%.*s'\n", LIT(name), LIT(param));
			}
		}
	} else {
		gb_printf_err("Invalid flag parameter for '%.*s' = '%.*s'\n", LIT(name), LIT(param));
	}

	return value;
}


bool parse_build_flags(Array<String> args) {
	auto build_flags = array_make<BuildFlag>(heap_allocator(), 0, BuildFlag_COUNT);
	add_flag(&build_flags, BuildFlag_Help,              str_lit("help"),                BuildFlagParam_None, Command_all);
	add_flag(&build_flags, BuildFlag_OutFile,           str_lit("out"),                 BuildFlagParam_String, Command__does_build &~ Command_test);
	add_flag(&build_flags, BuildFlag_OptimizationLevel, str_lit("opt"),                 BuildFlagParam_Integer, Command__does_build);
	add_flag(&build_flags, BuildFlag_OptimizationMode,  str_lit("o"),                   BuildFlagParam_String, Command__does_build);
	add_flag(&build_flags, BuildFlag_OptimizationMode,  str_lit("O"),                   BuildFlagParam_String, Command__does_build);
	add_flag(&build_flags, BuildFlag_ShowTimings,       str_lit("show-timings"),        BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_ShowMoreTimings,   str_lit("show-more-timings"),   BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_ShowUnused,        str_lit("show-unused"),         BuildFlagParam_None, Command_check);
	add_flag(&build_flags, BuildFlag_ShowUnusedWithLocation, str_lit("show-unused-with-location"), BuildFlagParam_None, Command_check);
	add_flag(&build_flags, BuildFlag_ShowSystemCalls,   str_lit("show-system-calls"),   BuildFlagParam_None, Command_all);
	add_flag(&build_flags, BuildFlag_ThreadCount,       str_lit("thread-count"),        BuildFlagParam_Integer, Command_all);
	add_flag(&build_flags, BuildFlag_KeepTempFiles,     str_lit("keep-temp-files"),     BuildFlagParam_None, Command__does_build);
	add_flag(&build_flags, BuildFlag_Collection,        str_lit("collection"),          BuildFlagParam_String, Command__does_check);
	add_flag(&build_flags, BuildFlag_Define,            str_lit("define"),              BuildFlagParam_String, Command__does_check, true);
	add_flag(&build_flags, BuildFlag_BuildMode,         str_lit("build-mode"),          BuildFlagParam_String, Command__does_build); // Commands_build is not used to allow for a better error message
	add_flag(&build_flags, BuildFlag_Target,            str_lit("target"),              BuildFlagParam_String, Command__does_check);
	add_flag(&build_flags, BuildFlag_Debug,             str_lit("debug"),               BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_DisableAssert,     str_lit("disable-assert"),      BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_NoBoundsCheck,     str_lit("no-bounds-check"),     BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_NoDynamicLiterals, str_lit("no-dynamic-literals"), BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_NoCRT,             str_lit("no-crt"),              BuildFlagParam_None, Command__does_build);
	add_flag(&build_flags, BuildFlag_NoEntryPoint,      str_lit("no-entry-point"),      BuildFlagParam_None, Command__does_check &~ Command_test);
	add_flag(&build_flags, BuildFlag_UseLLD,            str_lit("lld"),                 BuildFlagParam_None, Command__does_build);
	add_flag(&build_flags, BuildFlag_UseSeparateModules,str_lit("use-separate-modules"),BuildFlagParam_None, Command__does_build);
	add_flag(&build_flags, BuildFlag_Vet,               str_lit("vet"),                 BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_VetExtra,          str_lit("vet-extra"),           BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_UseLLVMApi,        str_lit("llvm-api"),            BuildFlagParam_None, Command__does_build);
	add_flag(&build_flags, BuildFlag_IgnoreUnknownAttributes, str_lit("ignore-unknown-attributes"), BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_ExtraLinkerFlags,  str_lit("extra-linker-flags"),              BuildFlagParam_String, Command__does_build);
	add_flag(&build_flags, BuildFlag_Microarch,         str_lit("microarch"),                       BuildFlagParam_String, Command__does_build);

	add_flag(&build_flags, BuildFlag_TestName,         str_lit("test-name"),                       BuildFlagParam_String, Command_test);

	add_flag(&build_flags, BuildFlag_DisallowDo,            str_lit("disallow-do"),              BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_DefaultToNilAllocator, str_lit("default-to-nil-allocator"), BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_InsertSemicolon,       str_lit("insert-semicolon"),         BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_StrictStyle,           str_lit("strict-style"),             BuildFlagParam_None, Command__does_check);
	add_flag(&build_flags, BuildFlag_Compact,           str_lit("compact"),            BuildFlagParam_None, Command_query);
	add_flag(&build_flags, BuildFlag_GlobalDefinitions, str_lit("global-definitions"), BuildFlagParam_None, Command_query);
	add_flag(&build_flags, BuildFlag_GoToDefinitions,   str_lit("go-to-definitions"),  BuildFlagParam_None, Command_query);

	add_flag(&build_flags, BuildFlag_Short,         str_lit("short"),        BuildFlagParam_None, Command_doc);
	add_flag(&build_flags, BuildFlag_AllPackages,   str_lit("all-packages"), BuildFlagParam_None, Command_doc);
	add_flag(&build_flags, BuildFlag_DocFormat,     str_lit("doc-format"),   BuildFlagParam_None, Command_doc);

	add_flag(&build_flags, BuildFlag_IgnoreWarnings,   str_lit("ignore-warnings"),    BuildFlagParam_None, Command_all);
	add_flag(&build_flags, BuildFlag_WarningsAsErrors, str_lit("warnings-as-errors"), BuildFlagParam_None, Command_all);
	add_flag(&build_flags, BuildFlag_VerboseErrors,    str_lit("verbose-errors"),     BuildFlagParam_None, Command_all);

#if defined(GB_SYSTEM_WINDOWS)
	add_flag(&build_flags, BuildFlag_IgnoreVsSearch, str_lit("ignore-vs-search"),  BuildFlagParam_None, Command__does_build);
	add_flag(&build_flags, BuildFlag_ResourceFile,   str_lit("resource"),          BuildFlagParam_String, Command__does_build);
	add_flag(&build_flags, BuildFlag_WindowsPdbName, str_lit("pdb-name"),          BuildFlagParam_String, Command__does_build);
	add_flag(&build_flags, BuildFlag_Subsystem,      str_lit("subsystem"),         BuildFlagParam_String, Command__does_build);
#endif

	GB_ASSERT(args.count >= 3);
	Array<String> flag_args = array_slice(args, 3, args.count);

	bool set_flags[BuildFlag_COUNT] = {};

	bool bad_flags = false;
	for_array(i, flag_args) {
		String flag = flag_args[i];
		if (flag[0] != '-') {
			gb_printf_err("Invalid flag: %.*s\n", LIT(flag));
			continue;
		}

		if (string_starts_with(flag, str_lit("--"))) {
			flag = substring(flag, 1, flag.len);
		}

		String name = substring(flag, 1, flag.len);
		isize end = 0;
		for (; end < name.len; end++) {
			if (name[end] == ':') break;
			if (name[end] == '=') break; // IMPORTANT TODO(bill): DEPRECATE THIS!!!!
		}
		name = substring(name, 0, end);
		String param = {};
		if (end < flag.len-1) param = substring(flag, 2+end, flag.len);

		bool is_supported = true;
		bool found = false;
		BuildFlag found_bf = {};
		for_array(build_flag_index, build_flags) {
			BuildFlag bf = build_flags[build_flag_index];
			if (bf.name == name) {
				found = true;
				found_bf = bf;
				if ((bf.command_support & build_context.command_kind) == 0) {
					is_supported = false;
					break;
				}

				if (set_flags[bf.kind]) {
					gb_printf_err("Previous flag set: '%.*s'\n", LIT(name));
					bad_flags = true;
				} else {
					ExactValue value = {};
					bool ok = false;
					if (bf.param_kind == BuildFlagParam_None) {
						if (param.len == 0) {
							ok = true;
						} else {
							gb_printf_err("Flag '%.*s' was not expecting a parameter '%.*s'\n", LIT(name), LIT(param));
							bad_flags = true;
						}
					} else if (param.len == 0) {
						gb_printf_err("Flag missing for '%.*s'\n", LIT(name));
						bad_flags = true;
					} else {
						ok = true;
						switch (bf.param_kind) {
						default:
							ok = false;
							break;
						case BuildFlagParam_Boolean: {
							if (str_eq_ignore_case(param, str_lit("t")) ||
							    str_eq_ignore_case(param, str_lit("true")) ||
							    param == "1") {
								value = exact_value_bool(true);
							} else if (str_eq_ignore_case(param, str_lit("f")) ||
							           str_eq_ignore_case(param, str_lit("false")) ||
							           param == "0") {
								value = exact_value_bool(false);
							} else {
								gb_printf_err("Invalid flag parameter for '%.*s' : '%.*s'\n", LIT(name), LIT(param));
							}
						} break;
						case BuildFlagParam_Integer:
							value = exact_value_integer_from_string(param);
							break;
						case BuildFlagParam_Float:
							value = exact_value_float_from_string(param);
							break;
						case BuildFlagParam_String: {
							value = exact_value_string(param);
							if (value.kind == ExactValue_String) {
								String s = value.value_string;
								if (s.len > 1 && s[0] == '"' && s[s.len-1] == '"') {
									value.value_string = substring(s, 1, s.len-1);
								}
							}
							break;
						}
						}
					}
					if (ok) {
						switch (bf.param_kind) {
						case BuildFlagParam_None:
							if (value.kind != ExactValue_Invalid) {
								gb_printf_err("%.*s expected no value, got %.*s", LIT(name), LIT(param));
								bad_flags = true;
								ok = false;
							}
							break;
						case BuildFlagParam_Boolean:
							if (value.kind != ExactValue_Bool) {
								gb_printf_err("%.*s expected a boolean, got %.*s", LIT(name), LIT(param));
								bad_flags = true;
								ok = false;
							}
							break;
						case BuildFlagParam_Integer:
							if (value.kind != ExactValue_Integer) {
								gb_printf_err("%.*s expected an integer, got %.*s", LIT(name), LIT(param));
								bad_flags = true;
								ok = false;
							}
							break;
						case BuildFlagParam_Float:
							if (value.kind != ExactValue_Float) {
								gb_printf_err("%.*s expected a floating pointer number, got %.*s", LIT(name), LIT(param));
								bad_flags = true;
								ok = false;
							}
							break;
						case BuildFlagParam_String:
							if (value.kind != ExactValue_String) {
								gb_printf_err("%.*s expected a string, got %.*s", LIT(name), LIT(param));
								bad_flags = true;
								ok = false;
							}
							break;
						}

						if (ok) switch (bf.kind) {
						case BuildFlag_Help:
							build_context.show_help = true;
							break;

						case BuildFlag_OutFile: {
							GB_ASSERT(value.kind == ExactValue_String);
							String path = value.value_string;
							path = string_trim_whitespace(path);
							if (is_build_flag_path_valid(path)) {
								build_context.out_filepath = path_to_full_path(heap_allocator(), path);
							} else {
								gb_printf_err("Invalid -out path, got %.*s\n", LIT(path));
								bad_flags = true;
							}
							break;
						}
						case BuildFlag_OptimizationLevel:
							GB_ASSERT(value.kind == ExactValue_Integer);
							if (set_flags[BuildFlag_OptimizationMode]) {
								gb_printf_err("Mixture of -opt and -o is not allowed\n");
								bad_flags = true;
								break;
							}
							build_context.optimization_level = cast(i32)big_int_to_i64(&value.value_integer);
							break;
						case BuildFlag_OptimizationMode:
							GB_ASSERT(value.kind == ExactValue_String);
							if (set_flags[BuildFlag_OptimizationLevel]) {
								gb_printf_err("Mixture of -opt and -o is not allowed\n");
								bad_flags = true;
								break;
							}

							if (value.value_string == "minimal") {
								build_context.optimization_level = 0;
							} else if (value.value_string == "size") {
								build_context.optimization_level = 1;
							} else if (value.value_string == "speed") {
								build_context.optimization_level = 2;
							} else {
								gb_printf_err("Invalid optimization mode for -o:<string>, got %.*s\n", LIT(value.value_string));
								gb_printf_err("Valid optimization modes:\n");
								gb_printf_err("\tminimal\n");
								gb_printf_err("\tsize\n");
								gb_printf_err("\tspeed\n");
								bad_flags = true;
							}
							break;
						case BuildFlag_ShowTimings:
							GB_ASSERT(value.kind == ExactValue_Invalid);
							build_context.show_timings = true;
							break;
						case BuildFlag_ShowUnused:
							GB_ASSERT(value.kind == ExactValue_Invalid);
							build_context.show_unused = true;
							break;
						case BuildFlag_ShowUnusedWithLocation:
							GB_ASSERT(value.kind == ExactValue_Invalid);
							build_context.show_unused = true;
							build_context.show_unused_with_location = true;
							break;
						case BuildFlag_ShowMoreTimings:
							GB_ASSERT(value.kind == ExactValue_Invalid);
							build_context.show_timings = true;
							build_context.show_more_timings = true;
							break;
						case BuildFlag_ShowSystemCalls:
							GB_ASSERT(value.kind == ExactValue_Invalid);
							build_context.show_system_calls = true;
							break;
						case BuildFlag_ThreadCount: {
							GB_ASSERT(value.kind == ExactValue_Integer);
							isize count = cast(isize)big_int_to_i64(&value.value_integer);
							if (count <= 0) {
								gb_printf_err("%.*s expected a positive non-zero number, got %.*s\n", LIT(name), LIT(param));
								build_context.thread_count = 1;
							} else {
								build_context.thread_count = count;
							}
							break;
						}
						case BuildFlag_KeepTempFiles:
							GB_ASSERT(value.kind == ExactValue_Invalid);
							build_context.keep_temp_files = true;
							break;

						case BuildFlag_Collection: {
							GB_ASSERT(value.kind == ExactValue_String);
							String str = value.value_string;
							isize eq_pos = -1;
							for (isize i = 0; i < str.len; i++) {
								if (str[i] == '=') {
									eq_pos = i;
									break;
								}
							}
							if (eq_pos < 0) {
								gb_printf_err("Expected 'name=path', got '%.*s'\n", LIT(param));
								bad_flags = true;
								break;
							}
							String name = substring(str, 0, eq_pos);
							String path = substring(str, eq_pos+1, str.len);
							if (name.len == 0 || path.len == 0) {
								gb_printf_err("Expected 'name=path', got '%.*s'\n", LIT(param));
								bad_flags = true;
								break;
							}

							if (!string_is_valid_identifier(name)) {
								gb_printf_err("Library collection name '%.*s' must be a valid identifier\n", LIT(name));
								bad_flags = true;
								break;
							}

							if (name == "_") {
								gb_printf_err("Library collection name cannot be an underscore\n");
								bad_flags = true;
								break;
							}

							if (name == "system") {
								gb_printf_err("Library collection name 'system' is reserved\n");
								bad_flags = true;
								break;
							}

							String prev_path = {};
							bool found = find_library_collection_path(name, &prev_path);
							if (found) {
								gb_printf_err("Library collection '%.*s' already exists with path '%.*s'\n", LIT(name), LIT(prev_path));
								bad_flags = true;
								break;
							}

							gbAllocator a = heap_allocator();
							String fullpath = path_to_fullpath(a, path);
							if (!path_is_directory(fullpath)) {
								gb_printf_err("Library collection '%.*s' path must be a directory, got '%.*s'\n", LIT(name), LIT(fullpath));
								gb_free(a, fullpath.text);
								bad_flags = true;
								break;
							}

							add_library_collection(name, path);

							// NOTE(bill): Allow for multiple library collections
							continue;
						}


						case BuildFlag_Define: {
							GB_ASSERT(value.kind == ExactValue_String);
							String str = value.value_string;
							isize eq_pos = -1;
							for (isize i = 0; i < str.len; i++) {
								if (str[i] == '=') {
									eq_pos = i;
									break;
								}
							}
							if (eq_pos < 0) {
								gb_printf_err("Expected 'name=value', got '%.*s'\n", LIT(param));
								bad_flags = true;
								break;
							}
							String name = substring(str, 0, eq_pos);
							String value = substring(str, eq_pos+1, str.len);
							if (name.len == 0 || value.len == 0) {
								gb_printf_err("Expected 'name=value', got '%.*s'\n", LIT(param));
								bad_flags = true;
								break;
							}

							if (!string_is_valid_identifier(name)) {
								gb_printf_err("Defined constant name '%.*s' must be a valid identifier\n", LIT(name));
								bad_flags = true;
								break;
							}

							if (name == "_") {
								gb_printf_err("Defined constant name cannot be an underscore\n");
								bad_flags = true;
								break;
							}

							HashKey key = hash_pointer(string_intern(name));

							if (map_get(&build_context.defined_values, key) != nullptr) {
								gb_printf_err("Defined constant '%.*s' already exists\n", LIT(name));
								bad_flags = true;
								break;
							}

							ExactValue v = build_param_to_exact_value(name, value);
							if (v.kind != ExactValue_Invalid) {
								map_set(&build_context.defined_values, key, v);
							} else {
								gb_printf_err("Invalid define constant value: '%.*s'. Define constants must be a valid Odin literal.\n", LIT(value));
								bad_flags = true;
							}

							break;
						}

						case BuildFlag_Target: {
							GB_ASSERT(value.kind == ExactValue_String);
							String str = value.value_string;
							bool found = false;

							for (isize i = 0; i < gb_count_of(named_targets); i++) {
								if (str_eq_ignore_case(str, named_targets[i].name)) {
									found = true;
									selected_target_metrics = named_targets + i;
									break;
								}
							}

							if (!found) {
								struct DistanceAndTarget {
									isize distance;
									isize target_index;
								};
								DistanceAndTarget distances[gb_count_of(named_targets)] = {};
								for (isize i = 0; i < gb_count_of(named_targets); i++) {
									distances[i].target_index = i;
									distances[i].distance = levenstein_distance_case_insensitive(str, named_targets[i].name);
								}
								gb_sort_array(distances, gb_count_of(distances), gb_isize_cmp(gb_offset_of(DistanceAndTarget, distance)));

								gb_printf_err("Unknown target '%.*s'\n", LIT(str));

								enum {MAX_SMALLEST_DISTANCE = 3};
								if (distances[0].distance <= MAX_SMALLEST_DISTANCE) {
									gb_printf_err("Did you mean:\n");
									for (isize i = 0; i < gb_count_of(named_targets); i++) {
										if (distances[i].distance > MAX_SMALLEST_DISTANCE) {
											break;
										}
										gb_printf_err("\t%.*s\n", LIT(named_targets[distances[i].target_index].name));
									}
								}
								gb_printf_err("All supported targets:\n");
								for (isize i = 0; i < gb_count_of(named_targets); i++) {
									gb_printf_err("\t%.*s\n", LIT(named_targets[i].name));
								}
								bad_flags = true;
							}

							break;
						}

						case BuildFlag_BuildMode: {
							GB_ASSERT(value.kind == ExactValue_String);
							String str = value.value_string;

							if (build_context.command != "build") {
								gb_printf_err("'build-mode' can only be used with the 'build' command\n");
								bad_flags = true;
								break;
							}

							if (str == "dll" || str == "shared") {
								build_context.build_mode = BuildMode_DynamicLibrary;
							} else if (str == "obj" || str == "object") {
								build_context.build_mode = BuildMode_Object;
							} else if (str == "exe") {
								build_context.build_mode = BuildMode_Executable;
							} else if (str == "asm" || str == "assembly" || str == "assembler") {
								build_context.build_mode = BuildMode_Assembly;
							} else if (str == "llvm" || str == "llvm-ir") {
								build_context.build_mode = BuildMode_LLVM_IR;
							} else {
								gb_printf_err("Unknown build mode '%.*s'\n", LIT(str));
								gb_printf_err("Valid build modes:\n");
								gb_printf_err("\tdll, shared\n");
								gb_printf_err("\tobj, object\n");
								gb_printf_err("\texe\n");
								gb_printf_err("\tasm, assembly, assembler\n");
								gb_printf_err("\tllvm, llvm-ir\n");
								bad_flags = true;
								break;
							}

							break;
						}

						case BuildFlag_Debug:
							build_context.ODIN_DEBUG = true;
							break;

						case BuildFlag_DisableAssert:
							build_context.ODIN_DISABLE_ASSERT = true;
							break;

						case BuildFlag_NoBoundsCheck:
							build_context.no_bounds_check = true;
							break;

						case BuildFlag_NoDynamicLiterals:
							build_context.no_dynamic_literals = true;
							break;

						case BuildFlag_NoCRT:
							build_context.no_crt = true;
							break;

						case BuildFlag_NoEntryPoint:
							build_context.no_entry_point = true;
							break;

						case BuildFlag_UseLLD:
							build_context.use_lld = true;
							break;

						case BuildFlag_UseSeparateModules:
							build_context.use_separate_modules = true;
							break;

						case BuildFlag_Vet:
							build_context.vet = true;
							break;
						case BuildFlag_VetExtra:
							build_context.vet = true;
							build_context.vet_extra = true;
							break;

						case BuildFlag_UseLLVMApi:
							gb_printf_err("-llvm-api flag is not required any more\n");
							bad_flags = true;
							break;

						case BuildFlag_IgnoreUnknownAttributes:
							build_context.ignore_unknown_attributes = true;
							break;

						case BuildFlag_ExtraLinkerFlags:
							GB_ASSERT(value.kind == ExactValue_String);
							build_context.extra_linker_flags = value.value_string;
							break;

						case BuildFlag_Microarch:
							GB_ASSERT(value.kind == ExactValue_String);
							build_context.microarch = value.value_string;
							string_to_lower(&build_context.microarch);
							break;

						case BuildFlag_TestName:
							GB_ASSERT(value.kind == ExactValue_String);
							{
								String name = value.value_string;
								if (!string_is_valid_identifier(name)) {
									gb_printf_err("Test name '%.*s' must be a valid identifier\n", LIT(name));
									bad_flags = true;
									break;
								}
								string_set_add(&build_context.test_names, name);

								// NOTE(bill): Allow for multiple -test-name
								continue;
							}

						case BuildFlag_DisallowDo:
							build_context.disallow_do = true;
							break;

						case BuildFlag_DefaultToNilAllocator:
							build_context.ODIN_DEFAULT_TO_NIL_ALLOCATOR = true;
							break;

						case BuildFlag_InsertSemicolon:
							build_context.insert_semicolon = true;
							break;

						case BuildFlag_StrictStyle:
							gb_printf_err("-strict-style flag is not required any more\n");
							bad_flags = true;
							break;


						case BuildFlag_Compact:
							if (!build_context.query_data_set_settings.ok) {
								gb_printf_err("Invalid use of -compact flag, only allowed with 'odin query'\n");
								bad_flags = true;
							} else {
								build_context.query_data_set_settings.compact = true;
							}
							break;

						case BuildFlag_GlobalDefinitions:
							if (!build_context.query_data_set_settings.ok) {
								gb_printf_err("Invalid use of -global-definitions flag, only allowed with 'odin query'\n");
								bad_flags = true;
							} else if (build_context.query_data_set_settings.kind != QueryDataSet_Invalid) {
								gb_printf_err("Invalid use of -global-definitions flag, a previous flag for 'odin query' was set\n");
								bad_flags = true;
							} else {
								build_context.query_data_set_settings.kind = QueryDataSet_GlobalDefinitions;
							}
							break;
						case BuildFlag_GoToDefinitions:
							if (!build_context.query_data_set_settings.ok) {
								gb_printf_err("Invalid use of -go-to-definitions flag, only allowed with 'odin query'\n");
								bad_flags = true;
							} else if (build_context.query_data_set_settings.kind != QueryDataSet_Invalid) {
								gb_printf_err("Invalid use of -global-definitions flag, a previous flag for 'odin query' was set\n");
								bad_flags = true;
							} else {
								build_context.query_data_set_settings.kind = QueryDataSet_GoToDefinitions;
							}
							break;

						case BuildFlag_Short:
							build_context.cmd_doc_flags |= CmdDocFlag_Short;
							break;
						case BuildFlag_AllPackages:
							build_context.cmd_doc_flags |= CmdDocFlag_AllPackages;
							break;
						case BuildFlag_DocFormat:
							build_context.cmd_doc_flags |= CmdDocFlag_DocFormat;
							break;
						case BuildFlag_IgnoreWarnings:
							if (build_context.warnings_as_errors) {
								gb_printf_err("-ignore-warnings cannot be used with -warnings-as-errors\n");
								bad_flags = true;
							} else {
								build_context.ignore_warnings = true;
							}
							break;
						case BuildFlag_WarningsAsErrors:
							if (build_context.ignore_warnings) {
								gb_printf_err("-warnings-as-errors cannot be used with -ignore-warnings\n");
								bad_flags = true;
							} else {
								build_context.warnings_as_errors = true;
							}
							break;

						case BuildFlag_VerboseErrors:
							build_context.show_error_line = true;
							break;

					#if defined(GB_SYSTEM_WINDOWS)
						case BuildFlag_IgnoreVsSearch:
							GB_ASSERT(value.kind == ExactValue_Invalid);
							build_context.ignore_microsoft_magic = true;
							break;

						case BuildFlag_ResourceFile: {
							GB_ASSERT(value.kind == ExactValue_String);
							String path = value.value_string;
							path = string_trim_whitespace(path);
							if (is_build_flag_path_valid(path)) {
								if(!string_ends_with(path, str_lit(".rc"))) {
									gb_printf_err("Invalid -resource path %.*s, missing .rc\n", LIT(path));
									bad_flags = true;
									break;
								}
								build_context.resource_filepath = substring(path, 0, string_extension_position(path));
								build_context.has_resource = true;
							} else {
								gb_printf_err("Invalid -resource path, got %.*s\n", LIT(path));
								bad_flags = true;
							}
							break;
						}
						case BuildFlag_WindowsPdbName: {
							GB_ASSERT(value.kind == ExactValue_String);
							String path = value.value_string;
							path = string_trim_whitespace(path);
							if (is_build_flag_path_valid(path)) {
								// #if defined(GB_SYSTEM_WINDOWS)
								// 	String ext = path_extension(path);
								// 	if (ext != ".pdb") {
								// 		path = substring(path, 0, string_extension_position(path));
								// 	}
								// #endif
								build_context.pdb_filepath = path;
							} else {
								gb_printf_err("Invalid -pdb-name path, got %.*s\n", LIT(path));
								bad_flags = true;
							}
							break;
						}

						case BuildFlag_Subsystem: {
							GB_ASSERT(value.kind == ExactValue_String);
							String subsystem = value.value_string;
							if (str_eq_ignore_case(subsystem, str_lit("console"))) {
								build_context.use_subsystem_windows = false;
							} else if (str_eq_ignore_case(subsystem, str_lit("window"))) {
								build_context.use_subsystem_windows = true;
							} else if (str_eq_ignore_case(subsystem, str_lit("windows"))) {
								build_context.use_subsystem_windows = true;
							} else {
								gb_printf_err("Invalid -subsystem string, got %.*s, expected either 'console' or 'windows'\n", LIT(subsystem));
								bad_flags = true;
							}
							break;
						}
					#endif

						}
					}

					if (!bf.allow_mulitple) {
						set_flags[bf.kind] = ok;
					}
				}
				break;
			}
		}
		if (found && !is_supported) {
			gb_printf_err("Unknown flag for 'odin %.*s': '%.*s'\n", LIT(build_context.command), LIT(name));
			gb_printf_err("'%.*s' is supported with the following commands:\n", LIT(name));
			gb_printf_err("\t");
			i32 count = 0;
			for (u32 i = 0; i < 32; i++) {
				if (found_bf.command_support & (1<<i)) {
					if (count > 0) {
						gb_printf_err(", ");
					}
					gb_printf_err("%s", odin_command_strings[i]);
					count += 1;
				}
			}
			gb_printf_err("\n");
			bad_flags = true;
		} else if (!found) {
			gb_printf_err("Unknown flag: '%.*s'\n", LIT(name));
			bad_flags = true;
		}
	}

	if (build_context.query_data_set_settings.ok) {
		if (build_context.query_data_set_settings.kind == QueryDataSet_Invalid) {
			gb_printf_err("'odin query' requires a flag determining the kind of query data set to be returned\n");
			gb_printf_err("\t-global-definitions : outputs a JSON file of global definitions\n");
			gb_printf_err("\t-go-to-definitions  : outputs a OGTD binary file of go to definitions for identifiers within an Odin project\n");
			bad_flags = true;
		}
	}

	return !bad_flags;
}

void show_timings(Checker *c, Timings *t) {
	Parser *p      = c->parser;
	isize lines    = p->total_line_count;
	isize tokens   = p->total_token_count;
	isize files    = 0;
	isize packages = p->packages.count;
	isize total_file_size = 0;
	f64 total_tokenizing_time = 0;
	f64 total_parsing_time = 0;
	for_array(i, p->packages) {
		files += p->packages[i]->files.count;
		for_array(j, p->packages[i]->files) {
			AstFile *file = p->packages[i]->files[j];
			total_tokenizing_time += file->time_to_tokenize;
			total_parsing_time += file->time_to_parse;
			total_file_size += file->tokenizer.end - file->tokenizer.start;
		}
	}

	timings_print_all(t);
	if (build_context.show_more_timings) {
		{
			gb_printf("\n");
			gb_printf("Total Lines     - %td\n", lines);
			gb_printf("Total Tokens    - %td\n", tokens);
			gb_printf("Total Files     - %td\n", files);
			gb_printf("Total Packages  - %td\n", packages);
			gb_printf("Total File Size - %td\n", total_file_size);
			gb_printf("\n");
		}
		{
			f64 time = total_tokenizing_time;
			gb_printf("Tokenization Only\n");
			gb_printf("LOC/s        - %.3f\n", cast(f64)lines/time);
			gb_printf("us/LOC       - %.3f\n", 1.0e6*time/cast(f64)lines);
			gb_printf("Tokens/s     - %.3f\n", cast(f64)tokens/time);
			gb_printf("us/Token     - %.3f\n", 1.0e6*time/cast(f64)tokens);
			gb_printf("bytes/s      - %.3f\n", cast(f64)total_file_size/time);
			gb_printf("MiB/s        - %.3f\n", cast(f64)(total_file_size/time)/(1024*1024));
			gb_printf("us/bytes     - %.3f\n", 1.0e6*time/cast(f64)total_file_size);

			gb_printf("\n");
		}
		{
			f64 time = total_parsing_time;
			gb_printf("Parsing Only\n");
			gb_printf("LOC/s        - %.3f\n", cast(f64)lines/time);
			gb_printf("us/LOC       - %.3f\n", 1.0e6*time/cast(f64)lines);
			gb_printf("Tokens/s     - %.3f\n", cast(f64)tokens/time);
			gb_printf("us/Token     - %.3f\n", 1.0e6*time/cast(f64)tokens);
			gb_printf("bytes/s      - %.3f\n", cast(f64)total_file_size/time);
			gb_printf("MiB/s        - %.3f\n", cast(f64)(total_file_size/time)/(1024*1024));
			gb_printf("us/bytes     - %.3f\n", 1.0e6*time/cast(f64)total_file_size);

			gb_printf("\n");
		}
		{
			TimeStamp ts = {};
			for_array(i, t->sections) {
				TimeStamp s = t->sections[i];
				if (s.label == "parse files") {
					ts = s;
					break;
				}
			}
			GB_ASSERT(ts.label == "parse files");

			f64 parse_time = time_stamp_as_s(ts, t->freq);
			gb_printf("Parse pass\n");
			gb_printf("LOC/s        - %.3f\n", cast(f64)lines/parse_time);
			gb_printf("us/LOC       - %.3f\n", 1.0e6*parse_time/cast(f64)lines);
			gb_printf("Tokens/s     - %.3f\n", cast(f64)tokens/parse_time);
			gb_printf("us/Token     - %.3f\n", 1.0e6*parse_time/cast(f64)tokens);
			gb_printf("bytes/s      - %.3f\n", cast(f64)total_file_size/parse_time);
			gb_printf("MiB/s        - %.3f\n", cast(f64)(total_file_size/parse_time)/(1024*1024));
			gb_printf("us/bytes     - %.3f\n", 1.0e6*parse_time/cast(f64)total_file_size);

			gb_printf("\n");
		}
		{
			TimeStamp ts = {};
			TimeStamp ts_end = {};
			for_array(i, t->sections) {
				TimeStamp s = t->sections[i];
				if (s.label == "type check") {
					ts = s;
				}
				if (s.label == "type check finish") {
					GB_ASSERT(ts.label != "");
					ts_end = s;
					break;
				}
			}
			GB_ASSERT(ts.label != "");
			GB_ASSERT(ts_end.label != "");

			ts.finish = ts_end.finish;

			f64 parse_time = time_stamp_as_s(ts, t->freq);
			gb_printf("Checker pass\n");
			gb_printf("LOC/s        - %.3f\n", cast(f64)lines/parse_time);
			gb_printf("us/LOC       - %.3f\n", 1.0e6*parse_time/cast(f64)lines);
			gb_printf("Tokens/s     - %.3f\n", cast(f64)tokens/parse_time);
			gb_printf("us/Token     - %.3f\n", 1.0e6*parse_time/cast(f64)tokens);
			gb_printf("bytes/s      - %.3f\n", cast(f64)total_file_size/parse_time);
			gb_printf("MiB/s        - %.3f\n", (cast(f64)total_file_size/parse_time)/(1024*1024));
			gb_printf("us/bytes     - %.3f\n", 1.0e6*parse_time/cast(f64)total_file_size);
			gb_printf("\n");
		}
		{
			f64 total_time = t->total_time_seconds;
			gb_printf("Total pass\n");
			gb_printf("LOC/s        - %.3f\n", cast(f64)lines/total_time);
			gb_printf("us/LOC       - %.3f\n", 1.0e6*total_time/cast(f64)lines);
			gb_printf("Tokens/s     - %.3f\n", cast(f64)tokens/total_time);
			gb_printf("us/Token     - %.3f\n", 1.0e6*total_time/cast(f64)tokens);
			gb_printf("bytes/s      - %.3f\n", cast(f64)total_file_size/total_time);
			gb_printf("MiB/s        - %.3f\n", cast(f64)(total_file_size/total_time)/(1024*1024));
			gb_printf("us/bytes     - %.3f\n", 1.0e6*total_time/cast(f64)total_file_size);
			gb_printf("\n");
		}
	}
}

void remove_temp_files(lbGenerator *gen) {
	if (build_context.keep_temp_files) return;

	for_array(i, gen->output_temp_paths) {
		String path = gen->output_temp_paths[i];
		gb_file_remove(cast(char const *)path.text);
	}

	if (!build_context.keep_object_files) {
		switch (build_context.build_mode) {
		case BuildMode_Executable:
		case BuildMode_DynamicLibrary:
			for_array(i, gen->output_object_paths) {
				String path = gen->output_object_paths[i];
				gb_file_remove(cast(char const *)path.text);
			}
			break;
		}
	}
}


void print_show_help(String const arg0, String const &command) {
	print_usage_line(0, "%.*s is a tool for managing Odin source code", LIT(arg0));
	print_usage_line(0, "Usage");
	print_usage_line(1, "%.*s %.*s [arguments]", LIT(arg0), LIT(command));
	print_usage_line(0, "");

	if (command == "build") {
		print_usage_line(1, "build     compile .odin file, or directory of .odin files, as an executable.");
		print_usage_line(1, "          one must contain the program's entry point, all must be in the same package.");
	} else if (command == "run") {
		print_usage_line(1, "run       same as 'build', but also then runs the newly compiled executable.");
	} else if (command == "check") {
		print_usage_line(1, "check     parse and type check .odin file");
	} else if (command == "test") {
		print_usage_line(1, "test      build ands runs procedures with the attribute @(test) in the initial package");
	} else if (command == "query") {
		print_usage_line(1, "query     [experimental] parse, type check, and output a .json file containing information about the program");
	} else if (command == "doc") {
		print_usage_line(1, "doc       generate documentation from a .odin file, or directory of .odin files");
		print_usage_line(2, "Examples:");
		print_usage_line(3, "odin doc core/path");
		print_usage_line(3, "odin doc core/path core/path/filepath");
	} else if (command == "version") {
		print_usage_line(1, "version   print version");
	}

	bool doc = command == "doc";
	bool build = command == "build";
	bool run_or_build = command == "run" || command == "build" || command == "test";
	bool test_only = command == "test";
	bool check_only = command == "check";
	bool check = run_or_build || command == "check";

	print_usage_line(0, "");
	print_usage_line(1, "Flags");
	print_usage_line(0, "");

	if (doc) {
		print_usage_line(1, "-short");
		print_usage_line(2, "Show shortened documentation for the packages");
		print_usage_line(0, "");

		print_usage_line(1, "-all-packages");
		print_usage_line(2, "Generates documentation for all packages used in the current project");
		print_usage_line(0, "");

		print_usage_line(1, "-doc-format");
		print_usage_line(2, "Generates documentation as the .odin-doc format (useful for external tooling)");
		print_usage_line(0, "");
	}

	if (run_or_build) {
		print_usage_line(1, "-out:<filepath>");
		print_usage_line(2, "Set the file name of the outputted executable");
		print_usage_line(2, "Example: -out:foo.exe");
		print_usage_line(0, "");

		print_usage_line(1, "-opt:<integer>");
		print_usage_line(2, "Set the optimization level for compilation");
		print_usage_line(2, "Accepted values: 0, 1, 2, 3");
		print_usage_line(2, "Example: -opt:2");
		print_usage_line(0, "");

		print_usage_line(1, "-o:<string>");
		print_usage_line(2, "Set the optimization mode for compilation");
		print_usage_line(2, "Accepted values: minimal, size, speed");
		print_usage_line(2, "Example: -o:speed");
		print_usage_line(0, "");
	}

	if (check) {
		print_usage_line(1, "-show-timings");
		print_usage_line(2, "Shows basic overview of the timings of different stages within the compiler in milliseconds");
		print_usage_line(0, "");

		print_usage_line(1, "-show-more-timings");
		print_usage_line(2, "Shows an advanced overview of the timings of different stages within the compiler in milliseconds");
		print_usage_line(0, "");

		print_usage_line(1, "-thread-count:<integer>");
		print_usage_line(2, "Override the number of threads the compiler will use to compile with");
		print_usage_line(2, "Example: -thread-count:2");
		print_usage_line(0, "");
	}

	if (check_only) {
		print_usage_line(1, "-show-unused");
		print_usage_line(2, "Shows unused package declarations within the current project");
		print_usage_line(0, "");
		print_usage_line(1, "-show-unused-with-location");
		print_usage_line(2, "Shows unused package declarations within the current project with the declarations source location");
		print_usage_line(0, "");
	}

	if (run_or_build) {
		print_usage_line(1, "-keep-temp-files");
		print_usage_line(2, "Keeps the temporary files generated during compilation");
		print_usage_line(0, "");
	}

	if (check) {
		print_usage_line(1, "-collection:<name>=<filepath>");
		print_usage_line(2, "Defines a library collection used for imports");
		print_usage_line(2, "Example: -collection:shared:dir/to/shared");
		print_usage_line(2, "Usage in Code:");
		print_usage_line(3, "import \"shared:foo\"");
		print_usage_line(0, "");

		print_usage_line(1, "-define:<name>=<expression>");
		print_usage_line(2, "Defines a global constant with a value");
		print_usage_line(2, "Example: -define:SPAM=123");
		print_usage_line(0, "");
	}

	if (build) {
		print_usage_line(1, "-build-mode:<mode>");
		print_usage_line(2, "Sets the build mode");
		print_usage_line(2, "Available options:");
		print_usage_line(3, "-build-mode:exe       Build as an executable");
		print_usage_line(3, "-build-mode:dll       Build as a dynamically linked library");
		print_usage_line(3, "-build-mode:shared    Build as a dynamically linked library");
		print_usage_line(3, "-build-mode:obj       Build as an object file");
		print_usage_line(3, "-build-mode:object    Build as an object file");
		print_usage_line(3, "-build-mode:assembly  Build as an object file");
		print_usage_line(3, "-build-mode:assembler Build as an assembly file");
		print_usage_line(3, "-build-mode:asm       Build as an assembly file");
		print_usage_line(3, "-build-mode:llvm-ir   Build as an LLVM IR file");
		print_usage_line(3, "-build-mode:llvm      Build as an LLVM IR file");
		print_usage_line(0, "");
	}

	if (check) {
		print_usage_line(1, "-target:<string>");
		print_usage_line(2, "Sets the target for the executable to be built in");
		print_usage_line(0, "");
	}

	if (run_or_build) {
		print_usage_line(1, "-debug");
		print_usage_line(2, "Enabled debug information, and defines the global constant ODIN_DEBUG to be 'true'");
		print_usage_line(0, "");

		print_usage_line(1, "-disable-assert");
		print_usage_line(2, "Disable the code generation of the built-in run-time 'assert' procedure, and defines the global constant ODIN_DISABLE_ASSERT to be 'true'");
		print_usage_line(0, "");

		print_usage_line(1, "-no-bounds-check");
		print_usage_line(2, "Disables bounds checking program wide");
		print_usage_line(0, "");

		print_usage_line(1, "-no-crt");
		print_usage_line(2, "Disables automatic linking with the C Run Time");
		print_usage_line(0, "");

		print_usage_line(1, "-lld");
		print_usage_line(2, "Use the LLD linker rather than the default");
		print_usage_line(0, "");

		print_usage_line(1, "-use-separate-modules");
		print_usage_line(1, "[EXPERIMENTAL]");
		print_usage_line(2, "The backend generates multiple build units which are then linked together");
		print_usage_line(2, "Normally, a single build unit is generated for a standard project");
		print_usage_line(0, "");

	}

	if (check) {
		print_usage_line(1, "-vet");
		print_usage_line(2, "Do extra checks on the code");
		print_usage_line(2, "Extra checks include:");
		print_usage_line(3, "Variable shadowing within procedures");
		print_usage_line(3, "Unused declarations");
		print_usage_line(0, "");

		print_usage_line(1, "-vet-extra");
		print_usage_line(2, "Do even more checks than standard vet on the code");
		print_usage_line(2, "To treat the extra warnings as errors, use -warnings-as-errors");
		print_usage_line(0, "");

		print_usage_line(1, "-ignore-unknown-attributes");
		print_usage_line(2, "Ignores unknown attributes");
		print_usage_line(2, "This can be used with metaprogramming tools");
		print_usage_line(0, "");

		if (command != "test") {
			print_usage_line(1, "-no-entry-point");
			print_usage_line(2, "Removes default requirement of an entry point (e.g. main procedure)");
			print_usage_line(0, "");
		}
	}

	if (test_only) {
		print_usage_line(1, "-test-name:<string>");
		print_usage_line(2, "Run specific test only by name");
		print_usage_line(0, "");
	}

	if (run_or_build) {
		print_usage_line(1, "-extra-linker-flags:<string>");
		print_usage_line(2, "Adds extra linker specific flags in a string");
		print_usage_line(0, "");

		print_usage_line(1, "-microarch:<string>");
		print_usage_line(2, "Specifies the specific micro-architecture for the build in a string");
		print_usage_line(2, "Examples:");
		print_usage_line(3, "-microarch:sandybridge");
		print_usage_line(3, "-microarch:native");
		print_usage_line(0, "");
	}

	if (check) {
		print_usage_line(1, "-disallow-do");
		print_usage_line(2, "Disallows the 'do' keyword in the project");
		print_usage_line(0, "");

		print_usage_line(1, "-default-to-nil-allocator");
		print_usage_line(2, "Sets the default allocator to be the nil_allocator, an allocator which does nothing");
		print_usage_line(0, "");

		print_usage_line(1, "-insert-semicolon");
		print_usage_line(2, "Inserts semicolons on newlines during tokenization using a basic rule");
		print_usage_line(0, "");

		print_usage_line(1, "-ignore-warnings");
		print_usage_line(2, "Ignores warning messages");
		print_usage_line(0, "");

		print_usage_line(1, "-warnings-as-errors");
		print_usage_line(2, "Treats warning messages as error messages");
		print_usage_line(0, "");

		print_usage_line(1, "-verbose-errors");
		print_usage_line(2, "Prints verbose error messages showing the code on that line and the location in that line");
		print_usage_line(0, "");
	}

	if (run_or_build) {
		#if defined(GB_SYSTEM_WINDOWS)
		print_usage_line(1, "-ignore-vs-search");
		print_usage_line(2, "[Windows only]");
		print_usage_line(2, "Ignores the Visual Studio search for library paths");
		print_usage_line(0, "");

		print_usage_line(1, "-resource:<filepath>");
		print_usage_line(2, "[Windows only]");
		print_usage_line(2, "Defines the resource file for the executable");
		print_usage_line(2, "Example: -resource:path/to/file.rc");
		print_usage_line(0, "");

		print_usage_line(1, "-pdb-name:<filepath>");
		print_usage_line(2, "[Windows only]");
		print_usage_line(2, "Defines the generated PDB name when -debug is enabled");
		print_usage_line(2, "Example: -pdb-name:different.pdb");
		print_usage_line(0, "");

		print_usage_line(1, "-subsystem:<option>");
		print_usage_line(2, "[Windows only]");
		print_usage_line(2, "Defines the subsystem for the application");
		print_usage_line(2, "Available options:");
		print_usage_line(3, "console");
		print_usage_line(3, "windows");
		print_usage_line(0, "");

		#endif
	}
}

void print_show_unused(Checker *c) {
	CheckerInfo *info = &c->info;

	auto unused = array_make<Entity *>(permanent_allocator(), 0, info->entities.count);
	for_array(i, info->entities) {
		Entity *e = info->entities[i];
		if (e == nullptr) {
			continue;
		}
		if (e->pkg == nullptr || e->pkg->scope == nullptr) {
			continue;
		}
		if (e->pkg->scope->flags & ScopeFlag_Builtin) {
			continue;
		}
		switch (e->kind) {
		case Entity_Invalid:
		case Entity_Builtin:
		case Entity_Nil:
		case Entity_Label:
			continue;
		case Entity_Constant:
		case Entity_Variable:
		case Entity_TypeName:
		case Entity_Procedure:
		case Entity_ProcGroup:
		case Entity_ImportName:
		case Entity_LibraryName:
			// Fine
			break;
		}
		if ((e->scope->flags & (ScopeFlag_Pkg|ScopeFlag_File)) == 0) {
			continue;
		}
		if (e->token.string.len == 0) {
			continue;
		}
		if (e->token.string == "_") {
			continue;
		}
		if (ptr_set_exists(&info->minimum_dependency_set, e)) {
			continue;
		}
		array_add(&unused, e);
	}

	gb_sort_array(unused.data, unused.count, cmp_entities_for_printing);

	print_usage_line(0, "Unused Package Declarations");

	AstPackage *curr_pkg = nullptr;
	EntityKind curr_entity_kind = Entity_Invalid;
	for_array(i, unused) {
		Entity *e = unused[i];
		if (curr_pkg != e->pkg) {
			curr_pkg = e->pkg;
			curr_entity_kind = Entity_Invalid;
			print_usage_line(0, "");
			print_usage_line(0, "package %.*s", LIT(curr_pkg->name));
		}
		if (curr_entity_kind != e->kind) {
			curr_entity_kind = e->kind;
			print_usage_line(1, "%s", print_entity_names[e->kind]);
		}
		if (build_context.show_unused_with_location) {
			TokenPos pos = e->token.pos;
			print_usage_line(2, "%s %.*s", token_pos_to_string(pos), LIT(e->token.string));
		} else {
			print_usage_line(2, "%.*s", LIT(e->token.string));
		}
	}
	print_usage_line(0, "");
}

bool check_env(void) {
	gbAllocator a = heap_allocator();
	char const *odin_root = gb_get_env("ODIN_ROOT", a);
	defer (gb_free(a, cast(void *)odin_root));
	if (odin_root) {
		if (!gb_file_exists(odin_root)) {
			gb_printf_err("Invalid ODIN_ROOT, directory does not exist, got %s\n", odin_root);
			return false;
		}
		String path = make_string_c(odin_root);
		if (!path_is_directory(path)) {
			gb_printf_err("Invalid ODIN_ROOT, expected a directory, got %s\n", odin_root);
			return false;
		}
	}
	return true;
}


int main(int arg_count, char const **arg_ptr) {
	if (arg_count < 2) {
		usage(make_string_c(arg_ptr[0]));
		return 1;
	}

	Timings *timings = &global_timings;

	timings_init(timings, str_lit("Total Time"), 2048);
	defer (timings_destroy(timings));

	arena_init(&permanent_arena, heap_allocator());
	temp_allocator_init(&temporary_allocator_data, 16*1024*1024);
	arena_init(&global_ast_arena, heap_allocator());
	permanent_arena.use_mutex = true;

	init_string_buffer_memory();
	init_string_interner();
	init_global_error_collector();
	init_keyword_hash_table();
	global_big_int_init();

	if (!check_env()) {
		return 1;
	}

	array_init(&library_collections, heap_allocator());
	// NOTE(bill): 'core' cannot be (re)defined by the user
	add_library_collection(str_lit("core"), get_fullpath_relative(heap_allocator(), odin_root_dir(), str_lit("core")));

	map_init(&build_context.defined_values, heap_allocator());
	build_context.extra_packages.allocator = heap_allocator();
	string_set_init(&build_context.test_names, heap_allocator());

	Array<String> args = setup_args(arg_count, arg_ptr);

	String command = args[1];
	String init_filename = {};
	String run_args_string = {};

	bool run_output = false;
	if (command == "run" || command == "test") {
		if (args.count < 3) {
			usage(args[0]);
			return 1;
		}
		build_context.command_kind = Command_run;
		if (command == "test") {
			build_context.command_kind = Command_test;
		}

		Array<String> run_args = array_make<String>(heap_allocator(), 0, arg_count);
		defer (array_free(&run_args));

		isize last_non_run_arg = args.count;
		for_array(i, args) {
			if (args[i] == "--") {
				last_non_run_arg = i;
			}
			if (i <= last_non_run_arg) {
				continue;
			}
			array_add(&run_args, args[i]);
		}

		args = array_slice(args, 0, last_non_run_arg);
		run_args_string = string_join_and_quote(heap_allocator(), run_args);
		init_filename = args[2];
		run_output = true;

	} else if (command == "build") {
		if (args.count < 3) {
			usage(args[0]);
			return 1;
		}
		build_context.command_kind = Command_build;
		init_filename = args[2];
	} else if (command == "check") {
		if (args.count < 3) {
			usage(args[0]);
			return 1;
		}
		build_context.command_kind = Command_check;
		build_context.no_output_files = true;
		init_filename = args[2];
	} else if (command == "query") {
		if (args.count < 3) {
			usage(args[0]);
			return 1;
		}
		build_context.command_kind = Command_query;
		build_context.no_output_files = true;
		build_context.query_data_set_settings.ok = true;
		init_filename = args[2];
	} else if (command == "doc") {
		if (args.count < 3) {
			usage(args[0]);
			return 1;
		}

		build_context.command_kind = Command_doc;
		init_filename = args[2];
		for (isize i = 3; i < args.count; i++) {
			auto arg = args[i];
			if (string_starts_with(arg, str_lit("-"))) {
				break;
			}
			array_add(&build_context.extra_packages, arg);
		}
		isize extra_count = build_context.extra_packages.count;
		if (extra_count > 0) {
			gb_memmove(args.data + 3, args.data + 3 + extra_count, extra_count * gb_size_of(*args.data));
			args.count -= extra_count;
		}


		build_context.no_output_files = true;
		build_context.generate_docs = true;
		build_context.no_entry_point = true; // ignore entry point
		#if 0
		print_usage_line(0, "Documentation generation is not yet supported");
		return 1;
		#endif
	} else if (command == "version") {
		build_context.command_kind = Command_version;
		gb_printf("%.*s version %.*s", LIT(args[0]), LIT(ODIN_VERSION));

		#ifdef NIGHTLY
		gb_printf("-nightly");
		#endif

		#ifdef GIT_SHA
		gb_printf(":%s", GIT_SHA);
		#endif

		gb_printf("\n");
		return 0;
	} else {
		usage(args[0]);
		return 1;
	}

	if (init_filename == "-help") {
		build_context.show_help = true;
	}

	build_context.command = command;

	if (!parse_build_flags(args)) {
		return 1;
	}

	if (build_context.show_help) {
		print_show_help(args[0], command);
		return 0;
	}

	// NOTE(bill): add 'shared' directory if it is not already set
	if (!find_library_collection_path(str_lit("shared"), nullptr)) {
		add_library_collection(str_lit("shared"),
		                       get_fullpath_relative(heap_allocator(), odin_root_dir(), str_lit("shared")));
	}


	init_build_context(selected_target_metrics ? selected_target_metrics->metrics : nullptr);
	// if (build_context.word_size == 4 && build_context.metrics.os != TargetOs_js) {
	// 	print_usage_line(0, "%.*s 32-bit is not yet supported for this platform", LIT(args[0]));
	// 	return 1;
	// }

	init_universal();
	// TODO(bill): prevent compiling without a linker

	timings_start_section(timings, str_lit("parse files"));

	Parser parser = {0};
	if (!init_parser(&parser)) {
		return 1;
	}
	defer (destroy_parser(&parser));

	if (parse_packages(&parser, init_filename) != ParseFile_None) {
		return 1;
	}

	if (any_errors()) {
		return 1;
	}

	temp_allocator_free_all(&temporary_allocator_data);

	timings_start_section(timings, str_lit("type check"));

	Checker checker = {0};

	bool checked_inited = init_checker(&checker, &parser);
	defer (if (checked_inited) {
		destroy_checker(&checker);
	});

	if (checked_inited) {
		check_parsed_files(&checker);
	}
	if (any_errors()) {
		return 1;
	}

	temp_allocator_free_all(&temporary_allocator_data);

	if (build_context.generate_docs) {
		if (global_error_collector.count != 0) {
			return 1;
		}
		generate_documentation(&checker);
		return 0;
	}

	if (build_context.no_output_files) {
		if (build_context.show_unused) {
			print_show_unused(&checker);
		}

		if (build_context.query_data_set_settings.ok) {
			generate_and_print_query_data(&checker, timings);
		} else {
			if (build_context.show_timings) {
				show_timings(&checker, timings);
			}
		}

		if (global_error_collector.count != 0) {
			return 1;
		}

		return 0;
	}

	if (!checked_inited) {
		return 1;
	}

	timings_start_section(timings, str_lit("LLVM API Code Gen"));
	lbGenerator gen = {};
	if (!lb_init_generator(&gen, &checker)) {
		return 1;
	}
	lb_generate_code(&gen);

	temp_allocator_free_all(&temporary_allocator_data);

	switch (build_context.build_mode) {
	case BuildMode_Executable:
	case BuildMode_DynamicLibrary:
		i32 result = linker_stage(&gen);
		if (result != 0) {
			if (build_context.show_timings) {
				show_timings(&checker, timings);
			}
			return 1;
		}
		break;
	}

	if (build_context.show_timings) {
		show_timings(&checker, timings);
	}

	remove_temp_files(&gen);

	if (run_output) {
	#if defined(GB_SYSTEM_WINDOWS)
		return system_exec_command_line_app("odin run", "%.*s.exe %.*s", LIT(gen.output_base), LIT(run_args_string));
	#else
		//NOTE(thebirk): This whole thing is a little leaky
		String output_ext = {};
		String complete_path = concatenate_strings(heap_allocator(), gen.output_base, output_ext);
		complete_path = path_to_full_path(heap_allocator(), complete_path);
		return system_exec_command_line_app("odin run", "\"%.*s\" %.*s", LIT(complete_path), LIT(run_args_string));
	#endif
	}

	return 0;
}
