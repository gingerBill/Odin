#if defined(GB_SYSTEM_FREEBSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

enum TargetOsKind {
	TargetOs_Invalid,

	TargetOs_windows,
	TargetOs_darwin,
	TargetOs_linux,
	TargetOs_essence,
	TargetOs_js,
	TargetOs_freebsd,

	TargetOs_freestanding,

	TargetOs_COUNT,
};

enum TargetArchKind {
	TargetArch_Invalid,

	TargetArch_amd64,
	TargetArch_386,
	TargetArch_riscv64,
	TargetArch_riscv32,
	TargetArch_wasm32,

	TargetArch_COUNT,
};

enum TargetEndianKind {
	TargetEndian_Invalid,

	TargetEndian_Little,
	TargetEndian_Big,

	TargetEndian_COUNT,
};

String target_os_names[TargetOs_COUNT] = {
	str_lit(""),
	str_lit("windows"),
	str_lit("darwin"),
	str_lit("linux"),
	str_lit("essence"),
	str_lit("js"),
	str_lit("freebsd"),

	str_lit("freestanding"),
};

String target_arch_names[TargetArch_COUNT] = {
	str_lit(""),
	str_lit("amd64"),
	str_lit("386"),
	str_lit("riscv64"),
	str_lit("riscv32"),
	str_lit("wasm32"),
};

String target_endian_names[TargetEndian_COUNT] = {
	str_lit(""),
	str_lit("little"),
	str_lit("big"),
};

TargetEndianKind target_endians[TargetArch_COUNT] = {
	TargetEndian_Invalid,
	TargetEndian_Little,
	TargetEndian_Little,
	TargetEndian_Little,
	TargetEndian_Little,
	TargetEndian_Little,
};



String const ODIN_VERSION = str_lit("0.13.1");



struct TargetMetrics {
	TargetOsKind   os;
	TargetArchKind arch;
	isize          word_size;
	isize          max_align;
	String         target_triplet;
	String         target_data_layout;
};


enum QueryDataSetKind {
	QueryDataSet_Invalid,
	QueryDataSet_GlobalDefinitions,
	QueryDataSet_GoToDefinitions,
};

struct QueryDataSetSettings {
	QueryDataSetKind kind;
	bool ok;
	bool compact;
};

enum BuildModeKind {
	BuildMode_Executable,
	BuildMode_DynamicLibrary,
	BuildMode_Object,
};


// This stores the information for the specify architecture of this build
struct BuildContext {
	// Constants
	String ODIN_OS;      // target operating system
	String ODIN_ARCH;    // target architecture
	String ODIN_ENDIAN;  // target endian
	String ODIN_VENDOR;  // compiler vendor
	String ODIN_VERSION; // compiler version
	String ODIN_ROOT;    // Odin ROOT
	bool   ODIN_DEBUG;   // Odin in debug mode
	bool   ODIN_DISABLE_ASSERT; // Whether the default 'assert' et al is disabled in code or not
	bool   ODIN_DEFAULT_TO_NIL_ALLOCATOR; // Whether the default allocator is a "nil" allocator or not (i.e. it does nothing)

	TargetEndianKind endian_kind;

	// In bytes
	i64    word_size; // Size of a pointer, must be >= 4
	i64    max_align; // max alignment, must be >= 1 (and typically >= word_size)

	String command;

	TargetMetrics metrics;

	bool show_help;

	String out_filepath;
	String resource_filepath;
	String pdb_filepath;
	bool   has_resource;
	String opt_flags;
	String llc_flags;
	String link_flags;
	String extra_linker_flags;
	BuildModeKind build_mode;
	bool   generate_docs;
	i32    optimization_level;
	bool   show_timings;
	bool   show_more_timings;
	bool   show_system_calls;
	bool   keep_temp_files;
	bool   ignore_unknown_attributes;
	bool   no_bounds_check;
	bool   no_dynamic_literals;
	bool   no_output_files;
	bool   no_crt;
	bool   use_lld;
	bool   vet;
	bool   cross_compiling;
	bool   different_os;
	bool   keep_object_files;

	bool   use_llvm_api;

	bool   use_subsystem_windows;
	bool   ignore_microsoft_magic;
	bool   linker_map_file;

	QueryDataSetSettings query_data_set_settings;

	gbAffinity affinity;
	isize      thread_count;

	Map<ExactValue> defined_values; // Key:
};



gb_global BuildContext build_context = {0};


gb_global TargetMetrics target_windows_386 = {
	TargetOs_windows,
	TargetArch_386,
	4,
	8,
	str_lit("i386-pc-windows-msvc"),
};
gb_global TargetMetrics target_windows_amd64 = {
	TargetOs_windows,
	TargetArch_amd64,
	8,
	16,
	str_lit("x86_64-pc-windows-msvc"),
	str_lit("e-m:w-i64:64-f80:128-n8:16:32:64-S128"),
};

gb_global TargetMetrics target_linux_386 = {
	TargetOs_linux,
	TargetArch_386,
	4,
	8,
	str_lit("i386-pc-linux-gnu"),

};
gb_global TargetMetrics target_linux_amd64 = {
	TargetOs_linux,
	TargetArch_amd64,
	8,
	16,
	str_lit("x86_64-pc-linux-gnu"),
	str_lit("e-m:w-i64:64-f80:128-n8:16:32:64-S128"),
};
gb_global TargetMetrics target_linux_riscv32 = {
	TargetOs_linux,
	TargetArch_riscv32,
	4,
	8,
	str_lit("riscv32-pc-linux-gnu"),
};
gb_global TargetMetrics target_linux_riscv64 = {
	TargetOs_linux,
	TargetArch_riscv64,
	8,
	16,
	str_lit("riscv64-pc-linux-gnu"),
	str_lit("e-m:e-p:64:64-i64:64-i128:128-n64-S128"),
};

gb_global TargetMetrics target_darwin_amd64 = {
	TargetOs_darwin,
	TargetArch_amd64,
	8,
	16,
	str_lit("x86_64-apple-darwin"),
	str_lit("e-m:o-i64:64-f80:128-n8:16:32:64-S128"),
};

gb_global TargetMetrics target_freebsd_386 = {
	TargetOs_freebsd,
	TargetArch_386,
	4,
	8,
	str_lit("i386-unknown-freebsd-elf"),
};

gb_global TargetMetrics target_freebsd_amd64 = {
	TargetOs_freebsd,
	TargetArch_amd64,
	8,
	16,
	str_lit("x86_64-unknown-freebsd-elf"),
	str_lit("e-m:w-i64:64-f80:128-n8:16:32:64-S128"),
};
gb_global TargetMetrics target_freebsd_riscv32 = {
	TargetOs_freebsd,
	TargetArch_riscv32,
	4,
	8,
	str_lit("riscv32-unknown-freebsd-elf"),
};
gb_global TargetMetrics target_freebsd_riscv64 = {
	TargetOs_freebsd,
	TargetArch_riscv64,
	8,
	16,
	str_lit("riscv64-unknown-freebsd-elf"),
	str_lit("e-m:w-i64:64-f80:128-n8:16:32:64-S128"), // TODO
};

gb_global TargetMetrics target_essence_amd64 = {
	TargetOs_essence,
	TargetArch_amd64,
	8,
	16,
	str_lit("x86_64-pc-none-elf"),
};

gb_global TargetMetrics target_js_wasm32 = {
	TargetOs_js,
	TargetArch_wasm32,
	4,
	8,
	str_lit("wasm32-freestanding-js"),
	str_lit(""),
};


struct NamedTargetMetrics {
	String name;
	TargetMetrics *metrics;
};

gb_global NamedTargetMetrics named_targets[] = {
	{ str_lit("darwin_amd64"),  &target_darwin_amd64 },
	{ str_lit("essence_amd64"), &target_essence_amd64 },
	{ str_lit("js_wasm32"),     &target_js_wasm32 },
	{ str_lit("linux_386"),     &target_linux_386 },
	{ str_lit("linux_amd64"),   &target_linux_amd64 },
	{ str_lit("linux_riscv32"),   &target_linux_riscv32 },
	{ str_lit("linux_riscv64"),   &target_linux_riscv64 },
	{ str_lit("windows_386"),   &target_windows_386 },
	{ str_lit("windows_amd64"), &target_windows_amd64 },
	{ str_lit("freebsd_386"),   &target_freebsd_386 },
	{ str_lit("freebsd_amd64"), &target_freebsd_amd64 },
	{ str_lit("freebsd_riscv32"), &target_freebsd_riscv32 },
	{ str_lit("freebsd_riscv64"), &target_freebsd_riscv64 },
};

NamedTargetMetrics *selected_target_metrics;

TargetOsKind get_target_os_from_string(String str) {
	for (isize i = 0; i < TargetOs_COUNT; i++) {
		if (str_eq_ignore_case(target_os_names[i], str)) {
			return cast(TargetOsKind)i;
		}
	}
	return TargetOs_Invalid;
}

TargetArchKind get_target_arch_from_string(String str) {
	for (isize i = 0; i < TargetArch_COUNT; i++) {
		if (str_eq_ignore_case(target_arch_names[i], str)) {
			return cast(TargetArchKind)i;
		}
	}
	return TargetArch_Invalid;
}


bool is_excluded_target_filename(String name) {
	String const ext = str_lit(".odin");
	String original_name = name;
	GB_ASSERT(string_ends_with(name, ext));
	name = substring(name, 0, name.len-ext.len);

	String str1 = {};
	String str2 = {};
	isize n = 0;

	str1 = name;
	n = str1.len;
	for (isize i = str1.len-1; i >= 0 && str1[i] != '_'; i--) {
		n -= 1;
	}
	str1 = substring(str1, n, str1.len);

	str2 = substring(name, 0, gb_max(n-1, 0));
	n = str2.len;
	for (isize i = str2.len-1; i >= 0 && str2[i] != '_'; i--) {
		n -= 1;
	}
	str2 = substring(str2, n, str2.len);

	if (str1 == name) {
		return false;
	}

	TargetOsKind   os1   = get_target_os_from_string(str1);
	TargetArchKind arch1 = get_target_arch_from_string(str1);
	TargetOsKind   os2   = get_target_os_from_string(str2);
	TargetArchKind arch2 = get_target_arch_from_string(str2);

	if (os1 != TargetOs_Invalid && arch2 != TargetArch_Invalid) {
		return os1 != build_context.metrics.os || arch2 != build_context.metrics.arch;
	} else if (arch1 != TargetArch_Invalid && os2 != TargetOs_Invalid) {
		return arch1 != build_context.metrics.arch || os2 != build_context.metrics.os;
	} else if (os1 != TargetOs_Invalid) {
		return os1 != build_context.metrics.os;
	} else if (arch1 != TargetArch_Invalid) {
		return arch1 != build_context.metrics.arch;
	}

	return false;
}


struct LibraryCollections {
	String name;
	String path;
};

gb_global Array<LibraryCollections> library_collections = {0};

void add_library_collection(String name, String path) {
	// TODO(bill): Check the path is valid and a directory
	LibraryCollections lc = {name, string_trim_whitespace(path)};
	array_add(&library_collections, lc);
}

bool find_library_collection_path(String name, String *path) {
	for_array(i, library_collections) {
		if (library_collections[i].name == name) {
			if (path) *path = library_collections[i].path;
			return true;
		}
	}
	return false;
}


// TODO(bill): OS dependent versions for the BuildContext
// join_path
// is_dir
// is_file
// is_abs_path
// has_subdir

String const WIN32_SEPARATOR_STRING = {cast(u8 *)"\\", 1};
String const NIX_SEPARATOR_STRING   = {cast(u8 *)"/",  1};

#if defined(GB_SYSTEM_WINDOWS)
String odin_root_dir(void) {
	String path = global_module_path;
	isize len, i;
	gbTempArenaMemory tmp;
	wchar_t *text;

	if (global_module_path_set) {
		return global_module_path;
	}

	auto path_buf = array_make<wchar_t>(heap_allocator(), 300);

	len = 0;
	for (;;) {
		len = GetModuleFileNameW(nullptr, &path_buf[0], cast(int)path_buf.count);
		if (len == 0) {
			return make_string(nullptr, 0);
		}
		if (len < path_buf.count) {
			break;
		}
		array_resize(&path_buf, 2*path_buf.count + 300);
	}
	len += 1; // NOTE(bill): It needs an extra 1 for some reason

	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));

	text = gb_alloc_array(string_buffer_allocator, wchar_t, len+1);

	GetModuleFileNameW(nullptr, text, cast(int)len);
	path = string16_to_string(heap_allocator(), make_string16(text, len));

	for (i = path.len-1; i >= 0; i--) {
		u8 c = path[i];
		if (c == '/' || c == '\\') {
			break;
		}
		path.len--;
	}

	global_module_path = path;
	global_module_path_set = true;


	array_free(&path_buf);

	return path;
}

#elif defined(GB_SYSTEM_OSX)

#include <mach-o/dyld.h>

String path_to_fullpath(gbAllocator a, String s);

String odin_root_dir(void) {
	String path = global_module_path;
	isize len, i;
	gbTempArenaMemory tmp;
	u8 *text;

	if (global_module_path_set) {
		return global_module_path;
	}

	auto path_buf = array_make<char>(heap_allocator(), 300);

	len = 0;
	for (;;) {
		u32 sz = path_buf.count;
		int res = _NSGetExecutablePath(&path_buf[0], &sz);
		if(res == 0) {
			len = sz;
			break;
		} else {
			array_resize(&path_buf, sz + 1);
		}
	}

	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));

	text = gb_alloc_array(string_buffer_allocator, u8, len + 1);
	gb_memmove(text, &path_buf[0], len);

	path = path_to_fullpath(heap_allocator(), make_string(text, len));

	for (i = path.len-1; i >= 0; i--) {
		u8 c = path[i];
		if (c == '/' || c == '\\') {
			break;
		}
		path.len--;
	}

	global_module_path = path;
	global_module_path_set = true;


	// array_free(&path_buf);

	return path;
}
#else

// NOTE: Linux / Unix is unfinished and not tested very well.
#include <sys/stat.h>

String path_to_fullpath(gbAllocator a, String s);

String odin_root_dir(void) {
	String path = global_module_path;
	isize len, i;
	gbTempArenaMemory tmp;
	u8 *text;

	if (global_module_path_set) {
		return global_module_path;
	}

	auto path_buf = array_make<char>(heap_allocator(), 300);
	defer (array_free(&path_buf));

	len = 0;
	for (;;) {
		// This is not a 100% reliable system, but for the purposes
		// of this compiler, it should be _good enough_.
		// That said, there's no solid 100% method on Linux to get the program's
		// path without checking this link. Sorry.
#if defined(GB_SYSTEM_FREEBSD)
		int mib[4];
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = KERN_PROC_PATHNAME;
		mib[3] = -1;
		len = path_buf.count;
		sysctl(mib, 4, &path_buf[0], (size_t *) &len, NULL, 0);
#elif defined(GB_SYSTEM_NETBSD)
		len = readlink("/proc/curproc/exe", &path_buf[0], path_buf.count);
#elif defined(GB_SYSTEM_DRAGONFLYBSD)
		len = readlink("/proc/curproc/file", &path_buf[0], path_buf.count);
#else
		len = readlink("/proc/self/exe", &path_buf[0], path_buf.count);
#endif
		if(len == 0) {
			return make_string(nullptr, 0);
		}
		if (len < path_buf.count) {
			break;
		}
		array_resize(&path_buf, 2*path_buf.count + 300);
	}

	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));

	text = gb_alloc_array(string_buffer_allocator, u8, len + 1);

	gb_memmove(text, &path_buf[0], len);

	path = path_to_fullpath(heap_allocator(), make_string(text, len));
	for (i = path.len-1; i >= 0; i--) {
		u8 c = path[i];
		if (c == '/' || c == '\\') {
			break;
		}
		path.len--;
	}

	global_module_path = path;
	global_module_path_set = true;

	return path;
}
#endif


#if defined(GB_SYSTEM_WINDOWS)
String path_to_fullpath(gbAllocator a, String s) {
	String result = {};
	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));
	String16 string16 = string_to_string16(string_buffer_allocator, s);

	DWORD len = GetFullPathNameW(&string16[0], 0, nullptr, nullptr);
	if (len != 0) {
		wchar_t *text = gb_alloc_array(string_buffer_allocator, wchar_t, len+1);
		GetFullPathNameW(&string16[0], len, text, nullptr);
		text[len] = 0;
		result = string16_to_string(a, make_string16(text, len));
		result = string_trim_whitespace(result);

		// Replace Windows style separators
		for (isize i = 0; i < result.len; i++) {
			if (result.text[i] == '\\') {
				result.text[i] = '/';
			}
		}
	}

	return result;
}
#elif defined(GB_SYSTEM_OSX) || defined(GB_SYSTEM_UNIX)
String path_to_fullpath(gbAllocator a, String s) {
	char *p;
	gb_mutex_lock(&string_buffer_mutex);
	p = realpath(cast(char *)s.text, 0);
	gb_mutex_unlock(&string_buffer_mutex);
	if(p == nullptr) return String{};
	return make_string_c(p);
}
#else
#error Implement system
#endif


String get_fullpath_relative(gbAllocator a, String base_dir, String path) {
	u8 *str = gb_alloc_array(heap_allocator(), u8, base_dir.len+1+path.len+1);
	defer (gb_free(heap_allocator(), str));

	isize i = 0;
	gb_memmove(str+i, base_dir.text, base_dir.len); i += base_dir.len;
	gb_memmove(str+i, "/", 1);                      i += 1;
	gb_memmove(str+i, path.text,     path.len);     i += path.len;
	str[i] = 0;


	String res = make_string(str, i);
	res = string_trim_whitespace(res);
	return path_to_fullpath(a, res);
}


String get_fullpath_core(gbAllocator a, String path) {
	String module_dir = odin_root_dir();

	String core = str_lit("core/");

	isize str_len = module_dir.len + core.len + path.len;
	u8 *str = gb_alloc_array(heap_allocator(), u8, str_len+1);
	defer (gb_free(heap_allocator(), str));

	isize i = 0;
	gb_memmove(str+i, module_dir.text, module_dir.len); i += module_dir.len;
	gb_memmove(str+i, core.text, core.len);             i += core.len;
	gb_memmove(str+i, path.text, path.len);             i += path.len;
	str[i] = 0;

	String res = make_string(str, i);
	res = string_trim_whitespace(res);
	return path_to_fullpath(a, res);
}



void init_build_context(TargetMetrics *cross_target) {
	BuildContext *bc = &build_context;

	gb_affinity_init(&bc->affinity);
	if (bc->thread_count == 0) {
		bc->thread_count = gb_max(bc->affinity.thread_count, 1);
	}

	bc->ODIN_VENDOR  = str_lit("odin");
	bc->ODIN_VERSION = ODIN_VERSION;
	bc->ODIN_ROOT    = odin_root_dir();

	TargetMetrics *metrics = nullptr;

	#if defined(GB_ARCH_64_BIT)
		#if defined(GB_SYSTEM_WINDOWS)
			metrics = &target_windows_amd64;
		#elif defined(GB_SYSTEM_OSX)
			metrics = &target_darwin_amd64;
		#elif defined(GB_SYSTEM_FREEBSD)
			metrics = &target_freebsd_amd64;
		#else
			metrics = &target_linux_amd64;
		#endif

		#if defined(GB_CPU_RISCV)
			#if defined(GB_SYSTEM_FREEBSD)
				metrics = &target_freebsd_riscv64;
			#else
				metrics = &target_linux_riscv64;
			#endif
		#endif
	#else
		#if defined(GB_SYSTEM_WINDOWS)
			metrics = &target_windows_386;
		#elif defined(GB_SYSTEM_OSX)
			#error "Build Error: Unsupported architecture"
		#elif defined(GB_SYSTEM_FREEBSD)
			metrics = &target_freebsd_386;
		#else
			metrics = &target_linux_386;
		#endif

		#if defined(GB_CPU_RISCV)
			#if defined(GB_SYSTEM_FREEBSD)
				metrics = &target_freebsd_riscv32;
			#else
				metrics = &target_linux_riscv32;
			#endif
		#endif
	#endif

	if (cross_target != nullptr && metrics != cross_target) {
		bc->different_os = cross_target->os != metrics->os;
		bc->cross_compiling = true;
		metrics = cross_target;
	}

	GB_ASSERT(metrics->os != TargetOs_Invalid);
	GB_ASSERT(metrics->arch != TargetArch_Invalid);
	GB_ASSERT(metrics->word_size > 1);
	GB_ASSERT(metrics->max_align > 1);


	bc->metrics = *metrics;
	bc->ODIN_OS     = target_os_names[metrics->os];
	bc->ODIN_ARCH   = target_arch_names[metrics->arch];
	bc->ODIN_ENDIAN = target_endian_names[target_endians[metrics->arch]];
	bc->endian_kind = target_endians[metrics->arch];
	bc->word_size   = metrics->word_size;
	bc->max_align   = metrics->max_align;
	bc->link_flags  = str_lit(" ");
	bc->opt_flags   = str_lit(" ");


	gbString llc_flags = gb_string_make_reserve(heap_allocator(), 64);
	if (bc->ODIN_DEBUG) {
		// llc_flags = gb_string_appendc(llc_flags, "-debug-compile ");
	}

	// NOTE(zangent): The linker flags to set the build architecture are different
	// across OSs. It doesn't make sense to allocate extra data on the heap
	// here, so I just #defined the linker flags to keep things concise.
	if (bc->metrics.arch == TargetArch_amd64) {
		llc_flags = gb_string_appendc(llc_flags, "-march=x86-64 ");

		switch (bc->metrics.os) {
		case TargetOs_windows:
			bc->link_flags = str_lit("/machine:x64 ");
			break;
		case TargetOs_darwin:
			break;
		case TargetOs_linux:
			bc->link_flags = str_lit("-arch x86-64 ");
			break;
		case TargetOs_freebsd:
			bc->link_flags = str_lit("-arch x86-64");
			break;
		}
	} else if (bc->metrics.arch == TargetArch_386) {
		llc_flags = gb_string_appendc(llc_flags, "-march=x86 ");

		switch (bc->metrics.os) {
		case TargetOs_windows:
			bc->link_flags = str_lit("/machine:x86 ");
			break;
		case TargetOs_darwin:
			gb_printf_err("Unsupported architecture\n");
			gb_exit(1);
			break;
		case TargetOs_linux:
			bc->link_flags = str_lit("-arch x86 ");
			break;
		case TargetOs_freebsd:
			bc->link_flags = str_lit("-arch x86");
			break;
		}
	} else if (bc->metrics.arch == TargetArch_riscv32) {
		llc_flags = gb_string_appendc(llc_flags, "-march=riscv32 ");

		switch (bc->metrics.os) {
		case TargetOs_windows:
			bc->link_flags = str_lit("/machine:riscv32 ");
			break;
		case TargetOs_darwin:
			gb_printf_err("Unsupported architecture\n");
			gb_exit(1);
			break;
		case TargetOs_linux:
			bc->link_flags = str_lit("-arch riscv32 ");
			break;
		case TargetOs_freebsd:
			bc->link_flags = str_lit("-arch riscv32");
			break;
		}
	} else if (bc->metrics.arch == TargetArch_riscv64) {
		llc_flags = gb_string_appendc(llc_flags, "-march=riscv64 --float-abi=hard ");

		switch (bc->metrics.os) {
		case TargetOs_windows:
			bc->link_flags = str_lit("/machine:riscv64 ");
			break;
		case TargetOs_darwin:
			gb_printf_err("Unsupported architecture\n");
			gb_exit(1);
			break;
		case TargetOs_linux:
			bc->link_flags = str_lit("-arch rv64imafdc -mfloat-abi=hard ");
			break;
		case TargetOs_freebsd:
			bc->link_flags = str_lit("-arch riscv64 ");
			break;
		}
	} else if (bc->metrics.arch == TargetArch_wasm32) {
		bc->link_flags = str_lit("--no-entry --export-table --export-all --allow-undefined ");
	} else {
		gb_printf_err("Compiler Error: Unsupported architecture\n");;
		gb_exit(1);
	}


	bc->optimization_level = gb_clamp(bc->optimization_level, 0, 3);

	gbString opt_flags = gb_string_make_reserve(heap_allocator(), 64);
	if (bc->optimization_level != 0) {
		opt_flags = gb_string_append_fmt(opt_flags, "-O%d ", bc->optimization_level);
		// NOTE(lachsinc): The following options were previously passed during call
		// to opt in main.cpp:exec_llvm_opt().
		//   -die:       Dead instruction elimination
		//   -memcpyopt: MemCpy optimization
	}
	if (bc->ODIN_DEBUG == false) {
		opt_flags = gb_string_appendc(opt_flags, "-mem2reg -memcpyopt -die ");
	}

	bc->llc_flags = make_string_c(llc_flags);


	// NOTE(lachsinc): This optimization option was previously required to get
	// around an issue in fmt.odin. Thank bp for tracking it down! Leaving for now until the issue
	// is resolved and confirmed by Bill. Maybe it should be readded in non-debug builds.
	// if (bc->ODIN_DEBUG == false) {
	// 	opt_flags = gb_string_appendc(opt_flags, "-mem2reg ");
	// }

	bc->opt_flags = make_string_c(opt_flags);


	#undef LINK_FLAG_X64
	#undef LINK_FLAG_386
}
