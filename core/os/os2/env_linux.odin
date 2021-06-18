package os2

// NOTE(rytc): Since Linux does not have a registry, there is no API to fetch environment variables.
// Environment variables are passed to the program from the shell it's run in through 
// https://linux.die.net/man/7/environ
// execl: https://linux.die.net/man/3/execl
// main: https://github.com/bminor/glibc/blob/1d67cf9e8a0194588e66fb3b7afcbdc3bf836a30/csu/libc-start.c#L248
// Thus to be able to access the env variables without using libc, Odin will need to expose them somehow.


/*
    foreign import libc "system:c"
    foreign libc {
        @(link_name="getenv")           _unix_getenv        :: proc(cstring) -> cstring ---;
    }
     secure_getenv   
     putenv, setenv
*/


_lookup_env :: proc(key: string, allocator := context.allocator) -> (value: string, found: bool) {
	return "", false;
}

_set_env :: proc(key, value: string) -> bool {
	return false;
}

_unset_env :: proc(key: string) -> bool {
	return false;
}

_clear_env :: proc() {
	
}

_environ :: proc(allocator := context.allocator) -> []string {
	return nil;
}


