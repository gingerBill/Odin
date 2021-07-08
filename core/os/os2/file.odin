package os2

import "core:io"
import "core:time"

Handle :: distinct uintptr;

Seek_From :: enum {
	Start   = 0, // seek relative to the origin of the file
	Current = 1, // seek relative to the current offset
	End     = 2, // seek relative to the end
}

File_Mode :: distinct u32;
File_Mode_Dir         :: File_Mode(1<<16);
File_Mode_Named_Pipe  :: File_Mode(1<<17);
File_Mode_Device      :: File_Mode(1<<18);
File_Mode_Char_Device :: File_Mode(1<<19);
File_Mode_Sym_Link    :: File_Mode(1<<20);


O_RDONLY :: int( 0);
O_WRONLY :: int( 1);
O_RDWR   :: int( 2);
O_APPEND :: int( 4);
O_CREATE :: int( 8);
O_EXCL   :: int(16);
O_SYNC   :: int(32);
O_TRUNC  :: int(64);
/*
	Open as a sparse file on supported filesystems.
	The implicit 'holes' won't take up diskspace.
	Does nothing if the OS or filesystem don't support it.

	NOTE: Sparse files may require additional calls after `open` to query where the holes are,
	tell the OS to keep treating it as a sparse file, etc.
*/
O_SPARSE :: int(128); 

File_Range :: struct {
	start: int, // Start of hole to punch in sparse file, or beginning of allocated range
	end:   int, // End of hole to punch in sparse file, or end of allocated range
}


stdin:  Handle = 0; // OS-Specific
stdout: Handle = 1; // OS-Specific
stderr: Handle = 2; // OS-Specific


create :: proc(name: string) -> (Handle, Error) {
	return _create(name);
}

open :: proc(name: string) -> (Handle, Error) {
	return _open(name);
}

open_file :: proc(name: string, flag: int, perm: File_Mode) -> (Handle, Error) {
	return _open_file(name, flag, perm);
}

close :: proc(fd: Handle) -> Error {
	return _close(fd);
}

name :: proc(fd: Handle, allocator := context.allocator) -> string {
	return _name(fd);
}

seek :: proc(fd: Handle, offset: i64, whence: Seek_From) -> (ret: i64, err: Error) {
	return _seek(fd, offset, whence);
}

read :: proc(fd: Handle, p: []byte) -> (n: int, err: Error) {
	return _read(fd, p);
}

read_at :: proc(fd: Handle, p: []byte, offset: i64) -> (n: int, err: Error) {
	return _read_at(fd, p, offset);
}

read_from :: proc(fd: Handle, r: io.Reader) -> (n: i64, err: Error) {
	return _read_from(fd, r);
}

write :: proc(fd: Handle, p: []byte) -> (n: int, err: Error) {
	return _write(fd, p);
}

write_at :: proc(fd: Handle, p: []byte, offset: i64) -> (n: int, err: Error) {
	return _write_at(fd, p, offset);
}

write_to :: proc(fd: Handle, w: io.Writer) -> (n: i64, err: Error) {
	return _write_to(fd, w);
}

file_size :: proc(fd: Handle) -> (n: i64, err: Error) {
	return _file_size(fd);
}


sync :: proc(fd: Handle) -> Error {
	return _sync(fd);
}

flush :: proc(fd: Handle) -> Error {
	return _flush(fd);
}

truncate :: proc(fd: Handle, size: i64) -> Maybe(Path_Error) {
	return _truncate(fd, size);
}

remove :: proc(name: string) -> Maybe(Path_Error) {
	return _remove(name);
}

rename :: proc(old_path, new_path: string) -> Maybe(Path_Error) {
	return _rename(old_path, new_path);
}


link :: proc(old_name, new_name: string) -> Maybe(Link_Error) {
	return _link(old_name, new_name);
}

symlink :: proc(old_name, new_name: string) -> Maybe(Link_Error) {
	return _symlink(old_name, new_name);
}

read_link :: proc(name: string) -> (string, Maybe(Path_Error)) {
	return _read_link(name);
}


chdir :: proc(fd: Handle) -> Error {
	return _chdir(fd);
}

chmod :: proc(fd: Handle, mode: File_Mode) -> Error {
	return _chmod(fd, mode);
}

chown :: proc(fd: Handle, uid, gid: int) -> Error {
	return _chown(fd, uid, gid);
}


lchown :: proc(name: string, uid, gid: int) -> Error {
	return _lchown(name, uid, gid);
}


chtimes :: proc(name: string, atime, mtime: time.Time) -> Maybe(Path_Error) {
	return _chtimes(name, atime, mtime);
}

exists :: proc(path: string) -> bool {
	return _exists(path);
}

is_file :: proc(path: string) -> bool {
	return _is_file(path);
}

is_dir :: proc(path: string) -> bool {
	return _is_dir(path);
}

is_sparse_supported :: proc(path: string) -> bool {
	return _is_sparse_supported(path);
}

sparse_set_mode :: proc(fd: Handle) -> Error {
	return _sparse_set_mode(fd);
}

sparse_zero_range :: proc(fd: Handle, range: File_Range) -> Error {
	/*
		Fill the range with zeroes.

		Where they hit an already allocated pages, they'll be written.
		The parts of the range that hit unallocated pages won't be written.
		This may also be used to make sparse (and deallocate) file pages you know to contain zeroes,
		or are okay with containing them implicitly.
	*/
	return _sparse_zero_range(fd, range);
}

sparse_query_allocated_ranges :: proc(fd: Handle, allocator := context.allocator) -> (allocated: []File_Range, err: Error) {
	/*
		Returns an array of file ranges that have backing storage allocated for them.
		These may still contain zeroes. Offsets not contained within these ranges are sparse.
		NOTE: the slice of File_Ranges will be allocated using the supplied allocator
	*/
	return _sparse_query_allocated_ranges(fd, allocator);
}