//+private
package os2

import win32 "core:sys/windows"

_pipe :: proc() -> (r, w: Handle, err: Error) {
	p: [2]win32.HANDLE;
	if !win32.CreatePipe(&p[0], &p[1], nil, 0) {
		return 0, 0, Platform_Error{i32(win32.GetLastError())};
	}
	return Handle(p[0]), Handle(p[1]), nil;
}

_get_file_type :: proc(fd: Handle) -> u32 {
	p := win32.HANDLE(fd);
	return win32.GetFileType(p); // Kernel32
}

_is_pipe :: proc(fd: Handle) -> bool {
	return _get_file_type(fd) == win32.FILE_TYPE_PIPE;
}