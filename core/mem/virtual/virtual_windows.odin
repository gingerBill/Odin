package virtual

import win "core:sys/windows"
import "core:mem"
import "core:os"

PAGE_NOACCESS           :: 0x01;
PAGE_READONLY           :: 0x02;
PAGE_READWRITE          :: 0x04;
PAGE_WRITECOPY          :: 0x08;
PAGE_EXECUTE            :: 0x10;
PAGE_EXECUTE_READ       :: 0x20;
PAGE_EXECUTE_READWRITE  :: 0x40;
PAGE_EXECUTE_WRITECOPY  :: 0x80;


Memory_Access_Flag :: enum i32 {
	// NOTE(tetra): Order is important here.
	Read,
	Write,
	Execute,
}
Memory_Access_Flags :: bit_set[Memory_Access_Flag; i32]; // NOTE: For PROT_NONE, use `{}`.

access_to_flags :: proc(access: Memory_Access_Flags) -> u32 {
	flags: u32 = PAGE_NOACCESS;

	if .Write in access {
		if .Execute in access {
			flags = PAGE_EXECUTE_READWRITE;
		} else {
			assert(.Read in access, "Windows cannot set memory to write-only");
			flags = PAGE_READWRITE;
		}
	} else if .Read in access {
		if .Execute in access {
			flags = PAGE_EXECUTE_READ;
		} else {
			flags = PAGE_READONLY;
		}
	}

	return flags;
}


reserve :: proc(size: int, desired_base: rawptr = nil) -> (memory: []byte) {
	ptr := win.VirtualAlloc(desired_base, uint(size), win.MEM_RESERVE, win.PAGE_NOACCESS);
	if ptr != nil {
		memory = mem.slice_ptr_to_bytes(ptr, size);
	}
	return;
}

release :: proc(originally_reserved_region: []byte) {
	if originally_reserved_region == nil do return;

	page_size := os.get_page_size();
	assert(mem.is_aligned_to(raw_data(originally_reserved_region), page_size), "must start at page boundary");

	// NOTE(tetra): On Windows, freeing virtual originally_reserved_region doesn't use lengths; the system
	// simply frees the block that was reserved originally.
	// For portability, we just ignore the length here, but still ask for the slice.
	//
	// Ideally we'd check that the user passed the entire region that was reserved in the first place
	// but that doesn't appear to be possible.
	ok := bool(win.VirtualFree(raw_data(originally_reserved_region), 0, win.MEM_RELEASE));
	assert(ok);
}

@(require_results)
commit :: proc(memory: []byte, access := Memory_Access_Flags{.Read, .Write}) -> bool {
	assert(memory != nil);

	page_size := os.get_page_size();
	assert(mem.is_aligned_to(raw_data(memory), page_size), "must start at page boundary");

	flags := access_to_flags(access);
	ptr := win.VirtualAlloc(raw_data(memory), uint(len(memory)), win.MEM_COMMIT, flags);
	return ptr != nil;
}

decommit :: proc(memory: []byte) {
	assert(memory != nil);

	page_size := os.get_page_size();
	assert(mem.is_aligned_to(raw_data(memory), page_size), "must start at page boundary");

	ok := bool(win.VirtualFree(raw_data(memory), uint(len(memory)), win.MEM_DECOMMIT));
	assert(ok);
}

set_access :: proc(memory: []byte, access: Memory_Access_Flags) -> bool {
	assert(memory != nil);

	page_size := os.get_page_size();
	assert(mem.is_aligned_to(raw_data(memory), page_size), "must start at page boundary");

	flags := access_to_flags(access);
	unused: u32 = ---;
	ok := bool(win.VirtualProtect(raw_data(memory), uint(len(memory)), u32(flags), &unused));
	return ok;
}
