//+private
package os2

import "core:io"
import "core:time"
import win32 "core:sys/windows"

_create :: proc(name: string) -> (Handle, Error) {
	return 0, nil;
}

_open :: proc(name: string) -> (Handle, Error) {
	return 0, nil;
}

_open_file :: proc(name: string, flag: int, perm: File_Mode) -> (Handle, Error) {
	return 0, nil;
}

_close :: proc(fd: Handle) -> Error {
	return nil;
}

_name :: proc(fd: Handle, allocator := context.allocator) -> string {
	return "";
}

_seek :: proc(fd: Handle, offset: i64, whence: Seek_From) -> (ret: i64, err: Error) {
	return;
}

_read :: proc(fd: Handle, p: []byte) -> (n: int, err: Error) {
	return;
}

_read_at :: proc(fd: Handle, p: []byte, offset: i64) -> (n: int, err: Error) {
	return;
}

_read_from :: proc(fd: Handle, r: io.Reader) -> (n: i64, err: Error) {
	return;
}

_write :: proc(fd: Handle, p: []byte) -> (n: int, err: Error) {
	return;
}

_write_at :: proc(fd: Handle, p: []byte, offset: i64) -> (n: int, err: Error) {
	return;
}

_write_to :: proc(fd: Handle, w: io.Writer) -> (n: i64, err: Error) {
	return;
}

_file_size :: proc(fd: Handle) -> (n: i64, err: Error) {
	return;
}


_sync :: proc(fd: Handle) -> Error {
	return nil;
}

_flush :: proc(fd: Handle) -> Error {
	return nil;
}

_truncate :: proc(fd: Handle, size: i64) -> Maybe(Path_Error) {
	return nil;
}

_remove :: proc(name: string) -> Maybe(Path_Error) {
	return nil;
}

_rename :: proc(old_path, new_path: string) -> Maybe(Path_Error) {
	return nil;
}


_link :: proc(old_name, new_name: string) -> Maybe(Link_Error) {
	return nil;
}

_symlink :: proc(old_name, new_name: string) -> Maybe(Link_Error) {
	return nil;
}

_read_link :: proc(name: string) -> (string, Maybe(Path_Error)) {
	return "", nil;
}


_chdir :: proc(fd: Handle) -> Error {
	return nil;
}

_chmod :: proc(fd: Handle, mode: File_Mode) -> Error {
	return nil;
}

_chown :: proc(fd: Handle, uid, gid: int) -> Error {
	return nil;
}


_lchown :: proc(name: string, uid, gid: int) -> Error {
	return nil;
}


_chtimes :: proc(name: string, atime, mtime: time.Time) -> Maybe(Path_Error) {
	return nil;
}


_exists :: proc(path: string) -> bool {
	return false;
}

_is_file :: proc(path: string) -> bool {
	return false;
}

_is_dir :: proc(path: string) -> bool {
	return false;
}

_path_error_delete :: proc(perr: Maybe(Path_Error)) {

}

_link_error_delete :: proc(lerr: Maybe(Link_Error)) {

}

/*
	Sparse file support:
	https://docs.microsoft.com/en-us/windows/win32/fileio/sparse-file-operations
*/

_is_sparse_supported :: proc(path: string) -> bool {
	return false;
}

_sparse_set_mode :: proc(Handle) -> Error {
	return .None;
}

_sparse_zero_range :: proc(fd: Handle, range: File_Range) -> Error {
	/*
		Fill the range with zeroes.

		Where they hit an already allocated pages, they'll be written.
		The parts of the range that hit unallocated pages won't be written.
		This may also be used to make sparse (and deallocate) file pages you know to contain zeroes,
		or are okay with containing them implicitly.

		https://docs.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_set_zero_data
	*/
	return .None;
}

_sparse_query_allocated_ranges :: proc(fd: Handle, allocator := context.allocator) -> (allocated: []File_Range, err: Error) {
	/*
		Returns an array of file ranges that have backing storage allocated for them.
		These may still contain zeroes. Offsets not contained within these ranges are sparse.
		NOTE: the slice of File_Ranges will be allocated using the supplied allocator

		https://docs.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_query_allocated_ranges
	*/
	return nil, .None;
}