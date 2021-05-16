package os2

import "core:sys/unix"

_pipe :: proc() -> (r, w: Handle, err: Error) {
    fd, pipe_err := unix.pipe();

    if pipe_err < 0 {
        return 0,0,_unix_errno(pipe_err);
    }

    return transmute(Handle)fd[0], transmute(Handle)fd[1], Platform_Error{0};
}
