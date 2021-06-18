//+build arm64, linux
package linux


// NOTE(rytc): is there no `arm64` build tag?
when false {

// NOTE(rytc): Reference: https://github.com/torvalds/linux/blob/v4.17/arch/arm/tools/syscall.tbl

SYSCALL_OPEN     :: 5;
SYSCALL_CLOSE    :: 6;
SYSCALL_LSEEK    :: 19;
SYSCALL_READ     :: 3;
SYSCALL_WRITE    :: 4;
SYSCALL_FSYNC    :: 118; 
SYSCALL_TRUNCATE :: 92;
SYSCALL_RENAME   :: 38;

SYSCALL_FSTAT    :: 108;
SYSCALL_LSTAT    :: 107;
SYSCALL_READLINK :: 85;

SYSCALL_MKDIR    :: 39;
SYSCALL_GETCWD   :: 183;
SYSCALL_RMDIR    :: 40;
SYSCALL_CHDIR    :: 12;

SYSCALL_CHMOD    :: 15;
SYSCALL_CHOWN    :: 182;
SYSCALL_UMASK    :: 60;
SYSCALL_LCHOWN   :: 16;

SYSCALL_LINK     :: 9;
SYSCALL_SYMLINK  :: 83;

SYSCALL_SETXATTR :: 226;
SYSCALL_UTIME    :: 30;

SYSCALL_PIPE     :: 42;
SYSCALL_PIPE2    :: 359;

SYSCALL_MMAP     :: 90;
SYSCALL_MUNMAP   :: 91;
SYSCALL_MREMAP   :: 163;

@private
_syscall1 :: #force_inline proc(syscall: i32, $T: typeid, a: $A) -> T {
    result := asm(i32, A) -> T {
        "syscall",
        "={r0},{r0},{r1}",
    }(syscall, a);

    return result;
}

@private
_syscall2 :: #force_inline proc(syscall: i32, $T: typeid, a: $A, b: $B) -> T {
    result := asm(i32, A, B) -> T {
        "syscall",
        "={r0},{r0},{r1},{r2}",
    }(syscall, a, b);

    return result;
}

@private 
_syscall3 :: proc(syscall: i32, $T: typeid, a: $A, b: $B, c: $C) -> T  {
    result := asm(i32, A, B, C) -> T {
        "syscall",
        "={r0},{r0},{r1},{r2},{r3}",
    }(syscall, a, b, c);

    return result;
}

@private 
_syscall5 :: proc(syscall: i32, $T: typeid, a: $A, b: $B, c: $C, d: $D, e: $E) -> T  {
    result := asm(i32, A, B, C, D, E) -> T {
        "syscall",
        "={r0},{r0},{r1},{r2},{r3},{r4},{r5}",
    }(syscall, a, b, c, d, e);
    
    return result;
}

@private 
_syscall6 :: proc(syscall: i32, $T: typeid, a: $A, b: $B, c: $C, d: $D, e: $E, f: $F) -> T  {
    result := asm(i32, A, B, C, D, E, F) -> T {
        "syscall",
        "={r0},{r0},{r1},{r2},{r3},{r4}{r5}{r6}",
    }(syscall, a, b, c, d, e, f);
    
    return result;
}

}
