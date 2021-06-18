//+private
package os2

import "core:fmt"
import "core:strings"
import "core:io"
import "core:sys/linux"
import "core:time"

// Max length in bytes defined by Linux
MAX_PATH_LENGTH     :: 4096;
MAX_FILENAME_LENGTH :: 255;

_create :: proc(name: string) -> (Handle, Error) {
    fd := linux.open(name, O_CREATE, 0);
    return transmute(Handle)fd, _linux_errno(fd);
}

_open :: proc(name: string) -> (Handle, Error) {
    fd := linux.open(name, O_RDONLY, 0);
    return transmute(Handle)fd, _linux_errno(fd);
}

_open_file :: proc(name: string, flag: int, perm: File_Mode) -> (Handle, Error) {
    fd := linux.open(name, flag, transmute(u32)perm);
    return transmute(Handle)fd, _linux_errno(fd);
}

_close :: proc(fd: Handle) -> Error {
    result := linux.close(transmute(int)fd);
    return _linux_errno(result);
}

_name :: proc(fd: Handle) -> string {
    return _get_handle_name(fd);
}

_seek :: proc(fd: Handle, offset: i64, whence: Seek_From) -> (ret: i64, err: Error) {
    result := linux.lseek(transmute(int)fd, offset, transmute(uint)whence);
    return result, _linux_errno(int(result));
}

_read :: proc(fd: Handle, p: []byte) -> (n: int, err: Error) {
    result := linux.read(transmute(int)fd, p);
    return result, _linux_errno(result);
}

_read_at :: proc(fd: Handle, p: []byte, offset: i64) -> (n: int, err: Error) {
    off,err_seek := _seek(fd, offset, Seek_From.Start);

    read_bytes,err_read := _read(fd, p);
    return read_bytes, err_read;
}

// TODO(rytc): temporary stub
_read_from :: proc(fd: Handle, r: io.Reader) -> (n: i64, err: Error) {
    return 0, General_Error.Invalid_Argument;
}

_write :: proc(fd: Handle, p: []byte) -> (n: int, err: Error) {
    result := linux.write(transmute(int)fd, p);
    return result, _linux_errno(result);
}

_write_at :: proc(fd: Handle, p: []byte, offset: i64) -> (n: int, err: Error) {
    off,err_seek := _seek(fd, offset, Seek_From.Start);

    n,err = _write(fd, p);
    return n,err;
}

// TODO(rytc): temporary stub
_write_to :: proc(fd: Handle, w: io.Writer) -> (n: i64, err: Error) {
    return 0, General_Error.Invalid_Argument;
}

_file_size :: proc(fd: Handle) -> (n: i64, err: Error) {
    stat : Linux_Stat;
    stat_err := linux.fstat(transmute(int)fd, uintptr(&stat));

    return stat.size, _linux_errno(stat_err);
}

_sync :: proc(fd: Handle) -> Error {
    err := linux.fsync(transmute(int)fd);
    return _linux_errno(err);
}

_flush :: proc(fd: Handle) -> Error {
    return _sync(fd);
}

// NOTE(rytc): Is it a good idea to truncate an open fil??
_truncate :: proc(fd: Handle, size: i64) -> Maybe(Path_Error) {
    path := _get_handle_path(fd);  
    error := linux.truncate(path, size);

    if error < 0 {
        return Path_Error{"Truncate", path, _linux_errno(error)};
    }

    return nil;
}

_remove :: proc(name: string) -> Maybe(Path_Error) {
    return Path_Error{"Remove (not implemented)", name, General_Error.Invalid_Argument};
}

_rename :: proc(old_path, new_path: string) -> Maybe(Path_Error) {
    err := linux.rename(old_path, new_path);
    if err < 0 {
        // NOTE(rytc): could have smarter error handling here
        return Path_Error{"Rename", old_path, _linux_errno(err)}; 
    }
    return nil;
}

_link :: proc(old_name, new_name: string) -> Maybe(Link_Error) {
    err := linux.link(old_name, new_name);
    if err < 0 {
        return Link_Error{"Link", old_name, new_name, _linux_errno(err)};
    }
    return nil;
}

_symlink :: proc(old_name, new_name: string) -> Maybe(Link_Error) {
    err := linux.symlink(old_name, new_name);
    if err < 0 {
        return  Link_Error{"Link", old_name, new_name, _linux_errno(err)};
    }
    return nil; 
}

_read_link :: proc(name: string) -> (string, Maybe(Path_Error)) {
    p := make([]byte, MAX_PATH_LENGTH, context.allocator);
    err := linux.readlink(name, p);
    if err < 0 {
        return name, Path_Error{"readlink", name, _linux_errno(err)};
    }
    return strings.string_from_ptr(raw_data(p), len(p)), nil;
}

_chdir :: proc(fd: Handle) -> Error {
    fullpath := _get_handle_path(fd, context.temp_allocator);
    filename_break := strings.last_index_byte(fullpath, '/');
    dir := fmt.tprintf(fullpath[:filename_break], filename_break);
    err := linux.chdir(dir);
    return _linux_errno(err);
}

// TODO(rytc): temporary stub
_chmod :: proc(fd: Handle, mode: File_Mode) -> Error {
    // TODO(rytc): This needs permission, not File_mode 
    // path := _get_handle_path(fd, context.temp_allocator);
    // err := linux.chmod(path, mode);
    // return _linux_errno(err);
	return General_Error.Invalid_Argument;
}

// NOTE(rytc): Why does chown take a handle, and lchown take a path?
// chown derefences symbolic links
// lchown does not dereference symbolic links
_chown :: proc(fd: Handle, uid, gid: int) -> Error {
    fullpath := _get_handle_path(fd, context.temp_allocator);
    err := linux.chown(fullpath, uid, gid);
    return _linux_errno(err); 
}

_lchown :: proc(name: string, uid, gid: int) -> Error {
    err := linux.lchown(name, uid, gid);
    return _linux_errno(err); 
}

_chtimes :: proc(name: string, atime, mtime: time.Time) -> Maybe(Path_Error) {
    atime_value := time.time_to_unix(atime);
    mtime_value := time.time_to_unix(mtime);

    linux.utime(name, atime_value, mtime_value);

    return nil;
}

_exists :: proc(path: string) -> bool {
    stat : Linux_Stat;
    err := linux.lstat(path, uintptr(&stat));
	return (_linux_errno(err) != General_Error.Not_Exist);
}

_is_file :: proc(path: string) -> bool {
    stat : Linux_Stat;
    err := linux.lstat(path, uintptr(&stat));
    if err >= 0 {
    	return (_linux_get_mode(stat.mode) < File_Mode_Dir);
    } else {
        return false;
    }
}

_is_dir :: proc(path: string) -> bool {
    stat : Linux_Stat;
    err := linux.lstat(path, uintptr(&stat));
    if err >= 0 {
        return (_linux_get_mode(stat.mode) == File_Mode_Dir);
    } else {
        return false;
    }
}


