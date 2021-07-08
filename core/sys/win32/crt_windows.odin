package win32

import "core:strings";

foreign {
	@(link_name="_wgetcwd") _get_cwd_wide :: proc(buffer: Wstring, buf_len: int) ->  ^Wstring ---
}

get_cwd :: proc(allocator := context.temp_allocator) -> string {
	buffer := make([]u16, MAX_PATH_WIDE, allocator);
	_get_cwd_wide(Wstring(&buffer[0]), MAX_PATH_WIDE);
	file := utf16_to_utf8(buffer[:], allocator);
	return strings.trim_right_null(file);
}
