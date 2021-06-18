package os2

// TODO(rytc): temporary stub
_error_string :: proc(errno: i32) -> string {
    return "";
}

@private
Errno :: enum i32 {
    EPERM   = 1,
    ENOENT  = 2,
    ESRCH   = 3,
    EINTR   = 4,
    EIO     = 5,
    ENXIO   = 6,
    EBADF   = 9,
    EAGAIN  = 11,
    ENOMEM  = 12,
    EACCES  = 13,
    EFAULT  = 14,
    EEXIST  = 17,
    ENODEV  = 19,
    ENOTDIR = 20,
    EISDIR  = 21,
    EINVAL  = 22,
    ENFILE  = 23,
    EMFILE  = 24,
    ETXTBSY = 26,
    EFBIG   = 27,
    ENOSPC  = 28,
    ESPIPE  = 29,
    EROFS   = 30,
    EPIPE   = 32,
}

_linux_errno :: proc(fd: int) -> Error {
    if fd >= 0 do return nil;

    errno := Errno(-fd);

    #partial switch errno { 
        case Errno.EACCES: fallthrough;
        case Errno.EPERM:
            return General_Error.Permission_Denied;
        case Errno.ENOENT:
            return General_Error.Not_Exist;
        case Errno.EINVAL: 
            return General_Error.Invalid_Argument;
        //case Errno.EBUSY:
        //  return Error.Timeout;
    }

    return General_Error.Invalid_Argument;
}
