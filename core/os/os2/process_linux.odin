//+private
package os2

import "core:sys/linux"

_exit :: proc "contextless" (code: int) -> ! {
    linux.exit(code); 
}

_get_uid :: proc() -> int {
	return linux.get_uid();
}

_get_euid :: proc() -> int {
	return linux.get_euid();
}

_get_gid :: proc() -> int {
	return linux.get_gid();
}

_get_egid :: proc() -> int {
	return linux.get_egid();
}

_get_pid :: proc() -> int {
	return linux.get_pid();
}

_get_ppid :: proc() -> int {
	return linux.get_ppid();
}




