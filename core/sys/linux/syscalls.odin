//+build linux
package linux

import "core:strings"

open :: proc(name: string, flags: int, mode: u32) -> int  {
    name_ptr := strings.ptr_from_string(name);
    handle_or_error := _syscall3(SYSCALL_OPEN, int, name_ptr, flags, mode);
    return handle_or_error;
}

close :: proc(fd: int) -> int {
    result := _syscall1(SYSCALL_CLOSE, int, fd);
    return result;
}

lseek :: proc(fd: int, offset: i64, whence: uint) -> i64 {
    result := _syscall3(SYSCALL_LSEEK, i64, fd, offset, whence);
    return result;
}

read :: proc(fd: int, p: []byte) -> int {
    result := _syscall3(SYSCALL_READ, int, fd, raw_data(p), len(p));
    return result;
};

write :: proc(fd: int, p: []byte) -> int {
    result := _syscall3(SYSCALL_WRITE, int, fd, raw_data(p), len(p));
    return result;
}

fsync :: proc(fd: int) -> int {
    result := _syscall1(SYSCALL_FSYNC, int, fd); 
    return result;
}

fstat :: proc(fd: int, stat: uintptr) -> int {
    result := _syscall2(SYSCALL_FSTAT, int, fd, stat);
    return result;
}

lstat :: proc(name: string, stat: uintptr) -> int {
    result := _syscall2(SYSCALL_LSTAT, int, strings.ptr_from_string(name), stat);
    return result;
}

readlink :: proc(name: string, p: []byte) -> int {
    result := _syscall3(SYSCALL_READLINK, int, strings.ptr_from_string(name), raw_data(p), len(p));
    return result;
}

mkdir :: proc(name: string, mode: u16) -> int {
    result := _syscall2(SYSCALL_MKDIR, int, strings.ptr_from_string(name), mode);
    return result;
}

getcwd :: proc(p: []byte) -> ^u8 {
    result := _syscall2(SYSCALL_GETCWD, ^u8, raw_data(p), len(p));
    return result;
}

rmdir :: proc(name: string) -> int {
    result := _syscall1(SYSCALL_RMDIR, int, strings.ptr_from_string(name));
    return result;
}

chmod :: proc(name: string, perm: u16) -> int {
    result := _syscall2(SYSCALL_CHMOD, int, strings.ptr_from_string(name), perm);
    return result;
}

umask :: proc(mask: int) -> int {
    result := _syscall1(SYSCALL_UMASK, int, mask);
    return result;
}

chdir :: proc(name: string) -> int {
    result := _syscall1(SYSCALL_CHDIR, int, strings.ptr_from_string(name));
    return result;
}

truncate :: proc(name: string, len: i64) -> int {
    result := _syscall2(SYSCALL_TRUNCATE, int, strings.ptr_from_string(name), len);
    return result;
}

rename :: proc(old_name: string, new_name: string) -> int {
    result := _syscall2(SYSCALL_RENAME, int, strings.ptr_from_string(old_name), strings.ptr_from_string(new_name));
    return result;
}

exit :: proc "contextless" (error_code: int) -> int {
    result := _syscall1(SYSCALL_EXIT, int, error_code);
    return result;
}

link :: proc(old_name: string, new_name: string) -> int {
    result := _syscall2(SYSCALL_LINK, int, strings.ptr_from_string(old_name), strings.ptr_from_string(new_name));
    return result;
}

symlink :: proc(old_name: string, new_name: string) -> int {
    result := _syscall2(SYSCALL_SYMLINK, int, strings.ptr_from_string(old_name), strings.ptr_from_string(new_name));
    return result;
}

chown :: proc(name: string, uid, gid: int) -> int {
    result := _syscall3(SYSCALL_CHOWN, int, strings.ptr_from_string(name), uid, gid);
    return result;
}

lchown :: proc(name: string, uid, gid: int) -> int {
    result := _syscall3(SYSCALL_LCHOWN, int, strings.ptr_from_string(name), uid, gid);
    return result;
}

setxattr :: proc(path, attr_name: string, value: uintptr, size: uint, flags: int) -> int {
    result := _syscall5(SYSCALL_SETXATTR, int, strings.ptr_from_string(path), strings.ptr_from_string(attr_name), value, size, flags);
    return result;
}

utime :: proc(name: string, atime, mtime: i64) -> int {
    utimbuf :: struct {
        atime: i64,
        mtime: i64,
    };
    values := utimbuf{atime, mtime};

    result := _syscall2(SYSCALL_UTIME, int, strings.ptr_from_string(name), uintptr(&values));
    return result;

}

pipe :: proc() -> (fd: [2]int, err: int) {
    handles : [2]int;
    result := _syscall1(SYSCALL_PIPE, int, &handles[0]);
    return handles, result;
}

pipe2 :: proc(flags: int) -> (fd: [2]int, err: int) {
    handles : [2]int;
    result := _syscall2(SYSCALL_PIPE2, int, &handles[0], flags);
    return handles, result;
}

mmap :: proc(addr, len, prot, flags, fd, offset: uint) -> uintptr {
    handles : [2]int;

    result := _syscall6(SYSCALL_MMAP, u64, addr, len, prot, flags, fd, offset);

    // NOTE(rytc): these syscalls returns the error code in a negative number.
    // To be able to easily translate it into a pointer on success, we just 
    // follow c's underflow rule for uints to figure out what would be an 
    // error code or invalid ptr.
    // This magic number is based on the possible error codes that mmap could 
    // return
    if result >= max(u64) - 76 {
        return 0; 
    }

    return transmute(uintptr)result;
}

munmap :: proc(addr: rawptr, len: uint) -> int {
    handles : [2]int;
    result := _syscall2(SYSCALL_MUNMAP, int, addr, len);
    return result;
}

mremap :: proc(addr, old_len, new_len, flags, new_addr: uint) -> uintptr {
    handles : [2]int;
    result := _syscall5(SYSCALL_MREMAP, u64, addr, old_len, new_len, flags, new_addr);

    // NOTE(rytc): See note in mmap
    if result >= max(u64) - 76 {
        return 0;
    }

    return transmute(uintptr)result;
}

get_uid :: proc() -> int {
    result := _syscall(SYSCALL_GETUID, int);
    return result;
}

get_euid :: proc() -> int {
    result := _syscall(SYSCALL_GETEUID, int);
    return result;
}

get_gid :: proc() -> int {
    result := _syscall(SYSCALL_GETGID, int);
    return result;
}

get_egid :: proc() -> int {
    result := _syscall(SYSCALL_GETEGID, int);
    return result;
}

get_pid :: proc() -> int {
    result := _syscall(SYSCALL_GETPID, int);
    return result;
}

get_ppid :: proc() -> int {
    result := _syscall(SYSCALL_GETPPID, int);
    return result;
}





