//+build amd64, linux
package linux

// NOTE(rytc): Reference: https://filippo.io/linux-syscall-table/

SYSCALL_OPEN     :: 2;
SYSCALL_CLOSE    :: 3;
SYSCALL_LSEEK    :: 8;
SYSCALL_READ     :: 0;
SYSCALL_WRITE    :: 1;
SYSCALL_FSYNC    :: 74; 
SYSCALL_TRUNCATE :: 76;
SYSCALL_RENAME   :: 82;

SYSCALL_EXIT     :: 60;

SYSCALL_FSTAT    :: 5;
SYSCALL_LSTAT    :: 6;
SYSCALL_READLINK :: 89;

SYSCALL_MKDIR    :: 83;
SYSCALL_GETCWD   :: 79;
SYSCALL_RMDIR    :: 84;
SYSCALL_CHDIR    :: 80;

SYSCALL_CHMOD    :: 90;
SYSCALL_CHOWN    :: 92;
SYSCALL_UMASK    :: 95;
SYSCALL_LCHOWN   :: 94;

SYSCALL_LINK     :: 86;
SYSCALL_SYMLINK  :: 88;

SYSCALL_SETXATTR :: 188;
SYSCALL_UTIME    :: 132;

SYSCALL_PIPE     :: 22;
SYSCALL_PIPE2    :: 293;

SYSCALL_MMAP     :: 9;
SYSCALL_MUNMAP   :: 11;
SYSCALL_MREMAP   :: 163;

SYSCALL_GETUID   :: 102;
SYSCALL_GETEUID  :: 107;
SYSCALL_GETGID   :: 104;
SYSCALL_GETEGID  :: 108;
SYSCALL_GETPID   :: 39;
SYSCALL_GETPPID  :: 110;

/*
 * This would be nice, have to find some way to generate the ASM code with variable arguments though
 *
@private
_syscall :: proc(syscall: i32, $T: typeid, args: ..any) -> T {
    result := asm(i32, type_of(..args)) -> T {
        "syscall",
        "={rax},{rax},{rdi}",
    }(syscall, args[:]);

    return result;
}
*/

@private
_syscall :: #force_inline proc"contextless"(syscall: i32, $T: typeid) -> T {
    result := asm(i32) -> T {
        "syscall",
        "={rax},{rax},{rdi}",
    }(syscall);

    return result;
}

@private
_syscall1 :: #force_inline proc"contextless"(syscall: i32, $T: typeid, a: $A) -> T {
    result := asm(i32, A) -> T {
        "syscall",
        "={rax},{rax},{rdi}",
    }(syscall, a);

    return result;
}

@private
_syscall2 :: #force_inline proc(syscall: i32, $T: typeid, a: $A, b: $B) -> T {
    result := asm(i32, A, B) -> T {
        "syscall",
        "={rax},{rax},{rdi},{rsi}",
    }(syscall, a, b);

    return result;
}

@private 
_syscall3 :: proc(syscall: i32, $T: typeid, a: $A, b: $B, c: $C) -> T  {
    result := asm(i32, A, B, C) -> T {
        "syscall",
        "={rax},{rax},{rdi},{rsi},{rdx}",
    }(syscall, a, b, c);

    return result;
}

@private 
_syscall5 :: proc(syscall: i32, $T: typeid, a: $A, b: $B, c: $C, d: $D, e: $E) -> T  {
    result := asm(i32, A, B, C, D, E) -> T {
        "syscall",
        "={rax},{rax},{rdi},{rsi},{rdx},{r10},{r8}",
    }(syscall, a, b, c, d, e);
    
    return result;
}

@private 
_syscall6 :: proc(syscall: i32, $T: typeid, a: $A, b: $B, c: $C, d: $D, e: $E, f: $F) -> T  {
    result := asm(i32, A, B, C, D, E, F) -> T {
        "syscall",
        "={rax},{rax},{rdi},{rsi},{rdx},{r10}{r8}{r9}",
    }(syscall, a, b, c, d, e, f);
    
    return result;
}


