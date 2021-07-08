package os2

pipe :: proc() -> (r, w: Handle, err: Error) {
	return _pipe();
}

is_pipe :: proc(fd: Handle) -> bool {
	return _is_pipe(fd);
}