package os

foreign import dl   "system:dl"
foreign import libc "system:c"

import "core:runtime"
import "core:strings"
import "core:c"
import "core:strconv"

Handle    :: distinct i32;
File_Time :: distinct u64;
Errno     :: distinct i32;
Syscall   :: distinct i32;

uid_t :: u32;
gid_t :: u32;

INVALID_HANDLE :: ~Handle(0);

ERROR_NONE:     Errno : 0;
EPERM:          Errno : 1;
ENOENT:         Errno : 2;
ESRCH:          Errno : 3;
EINTR:          Errno : 4;
EIO:            Errno : 5;
ENXIO:          Errno : 6;
EBADF:          Errno : 9;
EAGAIN:         Errno : 11;
ENOMEM:         Errno : 12;
EACCES:         Errno : 13;
EFAULT:         Errno : 14;
EEXIST:         Errno : 17;
ENODEV:         Errno : 19;
ENOTDIR:        Errno : 20;
EISDIR:         Errno : 21;
EINVAL:         Errno : 22;
ENFILE:         Errno : 23;
EMFILE:         Errno : 24;
ETXTBSY:        Errno : 26;
EFBIG:          Errno : 27;
ENOSPC:         Errno : 28;
ESPIPE:         Errno : 29;
EROFS:          Errno : 30;
EPIPE:          Errno : 32;

ERANGE:         Errno : 34; /* Result too large */
EDEADLK:        Errno : 35; /* Resource deadlock would occur */
ENAMETOOLONG:   Errno : 36; /* File name too long */
ENOLCK:         Errno : 37; /* No record locks available */

ENOSYS:         Errno : 38;	/* Invalid system call number */

ENOTEMPTY:      Errno : 39;	/* Directory not empty */
ELOOP:          Errno : 40;	/* Too many symbolic links encountered */
EWOULDBLOCK:    Errno : EAGAIN; /* Operation would block */
ENOMSG:         Errno : 42;	/* No message of desired type */
EIDRM:          Errno : 43;	/* Identifier removed */
ECHRNG:         Errno : 44;	/* Channel number out of range */
EL2NSYNC:       Errno : 45;	/* Level 2 not synchronized */
EL3HLT:         Errno : 46;	/* Level 3 halted */
EL3RST:         Errno : 47;	/* Level 3 reset */
ELNRNG:         Errno : 48;	/* Link number out of range */
EUNATCH:        Errno : 49;	/* Protocol driver not attached */
ENOCSI:         Errno : 50;	/* No CSI structure available */
EL2HLT:         Errno : 51;	/* Level 2 halted */
EBADE:          Errno : 52;	/* Invalid exchange */
EBADR:          Errno : 53;	/* Invalid request descriptor */
EXFULL:         Errno : 54;	/* Exchange full */
ENOANO:         Errno : 55;	/* No anode */
EBADRQC:        Errno : 56;	/* Invalid request code */
EBADSLT:        Errno : 57;	/* Invalid slot */
EDEADLOCK:      Errno : EDEADLK;
EBFONT:         Errno : 59;	/* Bad font file format */
ENOSTR:         Errno : 60;	/* Device not a stream */
ENODATA:        Errno : 61;	/* No data available */
ETIME:          Errno : 62;	/* Timer expired */
ENOSR:          Errno : 63;	/* Out of streams resources */
ENONET:         Errno : 64;	/* Machine is not on the network */
ENOPKG:         Errno : 65;	/* Package not installed */
EREMOTE:        Errno : 66;	/* Object is remote */
ENOLINK:        Errno : 67;	/* Link has been severed */
EADV:           Errno : 68;	/* Advertise error */
ESRMNT:         Errno : 69;	/* Srmount error */
ECOMM:          Errno : 70;	/* Communication error on send */
EPROTO:         Errno : 71;	/* Protocol error */
EMULTIHOP:      Errno : 72;	/* Multihop attempted */
EDOTDOT:        Errno : 73;	/* RFS specific error */
EBADMSG:        Errno : 74;	/* Not a data message */
EOVERFLOW:      Errno : 75;	/* Value too large for defined data type */
ENOTUNIQ:       Errno : 76;	/* Name not unique on network */
EBADFD:         Errno : 77;	/* File descriptor in bad state */
EREMCHG:        Errno : 78;	/* Remote address changed */
ELIBACC:        Errno : 79;	/* Can not access a needed shared library */
ELIBBAD:        Errno : 80;	/* Accessing a corrupted shared library */
ELIBSCN:        Errno : 81;	/* .lib section in a.out corrupted */
ELIBMAX:        Errno : 82;	/* Attempting to link in too many shared libraries */
ELIBEXEC:       Errno : 83;	/* Cannot exec a shared library directly */
EILSEQ:         Errno : 84;	/* Illegal byte sequence */
ERESTART:       Errno : 85;	/* Interrupted system call should be restarted */
ESTRPIPE:       Errno : 86;	/* Streams pipe error */
EUSERS:         Errno : 87;	/* Too many users */
ENOTSOCK:       Errno : 88;	/* Socket operation on non-socket */
EDESTADDRREQ:   Errno : 89;	/* Destination address required */
EMSGSIZE:       Errno : 90;	/* Message too long */
EPROTOTYPE:     Errno : 91;	/* Protocol wrong type for socket */
ENOPROTOOPT:    Errno : 92;	/* Protocol not available */
EPROTONOSUPPORT:Errno : 93;	/* Protocol not supported */
ESOCKTNOSUPPORT:Errno : 94;	/* Socket type not supported */
EOPNOTSUPP: 	Errno : 95;	/* Operation not supported on transport endpoint */
EPFNOSUPPORT: 	Errno : 96;	/* Protocol family not supported */
EAFNOSUPPORT: 	Errno : 97;	/* Address family not supported by protocol */
EADDRINUSE: 	Errno : 98;	/* Address already in use */
EADDRNOTAVAIL: 	Errno : 99;	/* Cannot assign requested address */
ENETDOWN: 		Errno : 100;	/* Network is down */
ENETUNREACH: 	Errno : 101;	/* Network is unreachable */
ENETRESET: 		Errno : 102;	/* Network dropped connection because of reset */
ECONNABORTED: 	Errno : 103;	/* Software caused connection abort */
ECONNRESET: 	Errno : 104;	/* Connection reset by peer */
ENOBUFS: 		Errno : 105;	/* No buffer space available */
EISCONN: 		Errno : 106;	/* Transport endpoint is already connected */
ENOTCONN: 		Errno : 107;	/* Transport endpoint is not connected */
ESHUTDOWN: 		Errno : 108;	/* Cannot send after transport endpoint shutdown */
ETOOMANYREFS: 	Errno : 109;	/* Too many references: cannot splice */
ETIMEDOUT: 		Errno : 110;	/* Connection timed out */
ECONNREFUSED: 	Errno : 111;	/* Connection refused */
EHOSTDOWN: 		Errno : 112;	/* Host is down */
EHOSTUNREACH: 	Errno : 113;	/* No route to host */
EALREADY: 		Errno : 114;	/* Operation already in progress */
EINPROGRESS: 	Errno : 115;	/* Operation now in progress */
ESTALE: 		Errno : 116;	/* Stale file handle */
EUCLEAN: 		Errno : 117;	/* Structure needs cleaning */
ENOTNAM: 		Errno : 118;	/* Not a XENIX named type file */
ENAVAIL: 		Errno : 119;	/* No XENIX semaphores available */
EISNAM: 		Errno : 120;	/* Is a named type file */
EREMOTEIO: 		Errno : 121;	/* Remote I/O error */
EDQUOT: 		Errno : 122;	/* Quota exceeded */

ENOMEDIUM: 		Errno : 123;	/* No medium found */
EMEDIUMTYPE: 	Errno : 124;	/* Wrong medium type */
ECANCELED: 		Errno : 125;	/* Operation Canceled */
ENOKEY: 		Errno : 126;	/* Required key not available */
EKEYEXPIRED: 	Errno : 127;	/* Key has expired */
EKEYREVOKED: 	Errno : 128;	/* Key has been revoked */
EKEYREJECTED: 	Errno : 129;	/* Key was rejected by service */

/* for robust mutexes */
EOWNERDEAD: 	Errno : 130;	/* Owner died */
ENOTRECOVERABLE: Errno : 131;	/* State not recoverable */

ERFKILL: 		Errno : 132;	/* Operation not possible due to RF-kill */

EHWPOISON: 		Errno : 133;	/* Memory page has hardware error */

O_RDONLY   :: 0x00000;
O_WRONLY   :: 0x00001;
O_RDWR     :: 0x00002;
O_CREATE   :: 0x00040;
O_EXCL     :: 0x00080;
O_NOCTTY   :: 0x00100;
O_TRUNC    :: 0x00200;
O_NONBLOCK :: 0x00800;
O_APPEND   :: 0x00400;
O_SYNC     :: 0x01000;
O_ASYNC    :: 0x02000;
O_CLOEXEC  :: 0x80000;


SEEK_SET   :: 0;
SEEK_CUR   :: 1;
SEEK_END   :: 2;
SEEK_DATA  :: 3;
SEEK_HOLE  :: 4;
SEEK_MAX   :: SEEK_HOLE;

// NOTE(zangent): These are OS specific!
// Do not mix these up!
RTLD_LAZY         :: 0x001;
RTLD_NOW          :: 0x002;
RTLD_BINDING_MASK :: 0x3;
RTLD_GLOBAL       :: 0x100;

// "Argv" arguments converted to Odin strings
args := _alloc_command_line_arguments();

Unix_File_Time :: struct {
	seconds:     i64,
	nanoseconds: i64,
}

OS_Stat :: struct {
	device_id:     u64, // ID of device containing file
	serial:        u64, // File serial number
	nlink:         u64, // Number of hard links
	mode:          u32, // Mode of the file
	uid:           u32, // User ID of the file's owner
	gid:           u32, // Group ID of the file's group
	_padding:      i32, // 32 bits of padding
	rdev:          u64, // Device ID, if device
	size:          i64, // Size of the file, in bytes
	block_size:    i64, // Optimal bllocksize for I/O
	blocks:        i64, // Number of 512-byte blocks allocated

	last_access:   Unix_File_Time, // Time of last access
	modified:      Unix_File_Time, // Time of last modification
	status_change: Unix_File_Time, // Time of last status change

	_reserve1,
	_reserve2,
	_reserve3:     i64,
};

// NOTE(laleksic, 2021-01-21): Comment and rename these to match OS_Stat above
Dirent :: struct {
	ino:    u64,
	off:    u64,
	reclen: u16,
	type:   u8,
	name:   [256]byte,
};

Dir :: distinct rawptr; // DIR*

// File type
S_IFMT   :: 0o170000; // Type of file mask
S_IFIFO  :: 0o010000; // Named pipe (fifo)
S_IFCHR  :: 0o020000; // Character special
S_IFDIR  :: 0o040000; // Directory
S_IFBLK  :: 0o060000; // Block special
S_IFREG  :: 0o100000; // Regular
S_IFLNK  :: 0o120000; // Symbolic link
S_IFSOCK :: 0o140000; // Socket

// File mode
// Read, write, execute/search by owner
S_IRWXU :: 0o0700; // RWX mask for owner
S_IRUSR :: 0o0400; // R for owner
S_IWUSR :: 0o0200; // W for owner
S_IXUSR :: 0o0100; // X for owner

	// Read, write, execute/search by group
S_IRWXG :: 0o0070; // RWX mask for group
S_IRGRP :: 0o0040; // R for group
S_IWGRP :: 0o0020; // W for group
S_IXGRP :: 0o0010; // X for group

	// Read, write, execute/search by others
S_IRWXO :: 0o0007; // RWX mask for other
S_IROTH :: 0o0004; // R for other
S_IWOTH :: 0o0002; // W for other
S_IXOTH :: 0o0001; // X for other

S_ISUID :: 0o4000; // Set user id on execution
S_ISGID :: 0o2000; // Set group id on execution
S_ISVTX :: 0o1000; // Directory restrcted delete


S_ISLNK  :: #force_inline proc(m: u32) -> bool { return (m & S_IFMT) == S_IFLNK;  }
S_ISREG  :: #force_inline proc(m: u32) -> bool { return (m & S_IFMT) == S_IFREG;  }
S_ISDIR  :: #force_inline proc(m: u32) -> bool { return (m & S_IFMT) == S_IFDIR;  }
S_ISCHR  :: #force_inline proc(m: u32) -> bool { return (m & S_IFMT) == S_IFCHR;  }
S_ISBLK  :: #force_inline proc(m: u32) -> bool { return (m & S_IFMT) == S_IFBLK;  }
S_ISFIFO :: #force_inline proc(m: u32) -> bool { return (m & S_IFMT) == S_IFIFO;  }
S_ISSOCK :: #force_inline proc(m: u32) -> bool { return (m & S_IFMT) == S_IFSOCK; }

F_OK :: 0; // Test for file existance
X_OK :: 1; // Test for execute permission
W_OK :: 2; // Test for write permission
R_OK :: 4; // Test for read permission

passwd :: struct {
	pw_name: cstring,
	pw_passwd: cstring,
	pw_uid: uid_t,
	pw_gid: gid_t,
	pw_gecos: cstring,
	pw_dir: cstring,
	pw_shell: cstring,
}

DIR :: opaque [0]byte;
ino_t :: distinct c.ulong;
off_t :: distinct c.long;

dirent :: struct {
	d_ino: ino_t, // Inode number of the file
	d_off: off_t,
	d_reclen: c.ushort, // Size in bytes of the returned record
	d_type: c.uchar, // File type

	d_name: [256]c.char, // Name of file, null-terminated, can exceede 256 chars (in which case d_reclen exceedes the size of this struct)
}

SYS_GETTID: Syscall : 186;

foreign libc {
	@(link_name="__errno_location") __errno_location    :: proc() -> ^int ---;
	@(link_name="syscall")          syscall             :: proc(number: Syscall, #c_vararg args: ..any) -> i32 ---;

	@(link_name="open")             _unix_open          :: proc(path: cstring, flags: c.int, mode: c.int) -> Handle ---;
	@(link_name="close")            _unix_close         :: proc(fd: Handle) -> c.int ---;
	@(link_name="read")             _unix_read          :: proc(fd: Handle, buf: rawptr, size: c.size_t) -> c.ssize_t ---;
	@(link_name="write")            _unix_write         :: proc(fd: Handle, buf: rawptr, size: c.size_t) -> c.ssize_t ---;
	@(link_name="lseek64")          _unix_seek          :: proc(fd: Handle, offset: i64, whence: c.int) -> i64 ---;
	@(link_name="gettid")           _unix_gettid        :: proc() -> u64 ---;
	@(link_name="getuid")           getuid              :: proc() -> uid_t ---;
	@(link_name="getpagesize")      _unix_getpagesize   :: proc() -> c.int ---;
	@(link_name="stat64")           _unix_stat          :: proc(path: cstring, stat: ^OS_Stat) -> c.int ---;
	@(link_name="lstat")            _unix_lstat         :: proc(path: cstring, stat: ^OS_Stat) -> c.int ---;
	@(link_name="fstat")            _unix_fstat         :: proc(fd: Handle, stat: ^OS_Stat) -> c.int ---;
	@(link_name="fdopendir")        _unix_fdopendir     :: proc(fd: Handle) -> Dir ---;
	@(link_name="closedir")         _unix_closedir      :: proc(dirp: Dir) -> c.int ---;
	@(link_name="rewinddir")        _unix_rewinddir     :: proc(dirp: Dir) ---;
	@(link_name="readdir_r")        _unix_readdir_r     :: proc(dirp: Dir, entry: ^Dirent, result: ^^Dirent) -> c.int ---;
	@(link_name="readlink")         _unix_readlink      :: proc(path: cstring, buf: ^byte, bufsiz: c.size_t) -> c.ssize_t ---;
	@(link_name="access")           _unix_access        :: proc(path: cstring, mask: c.int) -> c.int ---;

	@(link_name="getpwnam")         _unix_getpwnam      :: proc(name: cstring) -> ^passwd ---;
	@(link_name="getpwuid")         getpwuid            :: proc(uid: uid_t) -> ^passwd ---;
	@(link_name="readlink")         _unix_readlink      :: proc(pathname: cstring, buf: cstring, bufsiz: c.size_t) -> c.ssize_t ---;

	@(link_name="opendir")          _unix_opendir       :: proc(name: cstring) -> ^DIR ---; // TODO: Returns ^DIR (which is defined as __dirstream in dirent.h)
	@(link_name="readdir")          _unix_readdir       :: proc(dirp: ^DIR) -> ^dirent ---;
	@(link_name="closedir")         _unix_closedir      :: proc(dirp: ^DIR) -> c.int ---;

	@(link_name="malloc")           _unix_malloc        :: proc(size: c.size_t) -> rawptr ---;
	@(link_name="calloc")           _unix_calloc        :: proc(num, size: c.size_t) -> rawptr ---;
	@(link_name="free")             _unix_free          :: proc(ptr: rawptr) ---;
	@(link_name="realloc")          _unix_realloc       :: proc(ptr: rawptr, size: c.size_t) -> rawptr ---;
	@(link_name="getenv")           _unix_getenv        :: proc(cstring) -> cstring ---;
	@(link_name="setenv")           _unix_setenv        :: proc(name: cstring, value: cstring, overwrite: c.int) -> c.int ---;
	@(link_name="unsetenv")         _unix_unsetenv      :: proc(name: cstring) -> c.int ---;
	@(link_name="getcwd")           _unix_getcwd        :: proc(buf: cstring, len: c.size_t) -> cstring ---;
	@(link_name="chdir")            _unix_chdir         :: proc(buf: cstring) -> c.int ---;
	@(link_name="realpath")         _unix_realpath      :: proc(path: cstring, resolved_path: rawptr) -> rawptr ---;

	@(link_name="exit")             _unix_exit          :: proc(status: c.int) -> ! ---;
}
foreign dl {
	@(link_name="dlopen")           _unix_dlopen        :: proc(filename: cstring, flags: c.int) -> rawptr ---;
	@(link_name="dlsym")            _unix_dlsym         :: proc(handle: rawptr, symbol: cstring) -> rawptr ---;
	@(link_name="dlclose")          _unix_dlclose       :: proc(handle: rawptr) -> c.int ---;
	@(link_name="dlerror")          _unix_dlerror       :: proc() -> cstring ---;
}

is_path_separator :: proc(r: rune) -> bool {
	return r == '/';
}

get_last_error :: proc() -> int {
	return __errno_location()^;
}

open :: proc(path: string, flags: int = O_RDONLY, mode: int = 0) -> (Handle, Errno) {
	cstr := strings.clone_to_cstring(path);
	handle := _unix_open(cstr, c.int(flags), c.int(mode));
	delete(cstr);
	if handle == -1 {
		return INVALID_HANDLE, Errno(get_last_error());
	}
	return handle, ERROR_NONE;
}

close :: proc(fd: Handle) -> Errno {
	result := _unix_close(fd);
	if result == -1 {
		return Errno(get_last_error());
	}
	return ERROR_NONE;
}

read :: proc(fd: Handle, data: []byte) -> (int, Errno) {
	bytes_read := _unix_read(fd, &data[0], c.size_t(len(data)));
	if bytes_read == -1 {
		return -1, Errno(get_last_error());
	}
	return int(bytes_read), ERROR_NONE;
}

write :: proc(fd: Handle, data: []byte) -> (int, Errno) {
	if len(data) == 0 {
		return 0, ERROR_NONE;
	}
	bytes_written := _unix_write(fd, &data[0], c.size_t(len(data)));
	if bytes_written == -1 {
		return -1, Errno(get_last_error());
	}
	return int(bytes_written), ERROR_NONE;
}

seek :: proc(fd: Handle, offset: i64, whence: int) -> (i64, Errno) {
	res := _unix_seek(fd, offset, c.int(whence));
	if res == -1 {
		return -1, Errno(get_last_error());
	}
	return res, ERROR_NONE;
}

file_size :: proc(fd: Handle) -> (i64, Errno) {
	s, err := _fstat(fd);
	if err != ERROR_NONE {
		return 0, err;
	}
	return max(s.size, 0), ERROR_NONE;
}


// NOTE(bill): Uses startup to initialize it

stdin:  Handle = 0;
stdout: Handle = 1;
stderr: Handle = 2;

/* TODO(zangent): Implement these!
last_write_time :: proc(fd: Handle) -> File_Time {}
last_write_time_by_name :: proc(name: string) -> File_Time {}
*/
last_write_time :: proc(fd: Handle) -> (File_Time, Errno) {
	s, err := _fstat(fd);
	if err != ERROR_NONE {
		return 0, err;
	}
	modified := s.modified.seconds * 1_000_000_000 + s.modified.nanoseconds;
	return File_Time(modified), ERROR_NONE;
}

last_write_time_by_name :: proc(name: string) -> (File_Time, Errno) {
	s, err := _stat(name);
	if err != ERROR_NONE {
		return 0, err;
	}
	modified := s.modified.seconds * 1_000_000_000 + s.modified.nanoseconds;
	return File_Time(modified), ERROR_NONE;
}

@private
_stat :: proc(path: string) -> (OS_Stat, Errno) {
	cstr := strings.clone_to_cstring(path);
	defer delete(cstr);

	s: OS_Stat;
	result := _unix_stat(cstr, &s);
	if result == -1 {
		return s, Errno(get_last_error());
	}
	return s, ERROR_NONE;
}

@private
_lstat :: proc(path: string) -> (OS_Stat, Errno) {
	cstr := strings.clone_to_cstring(path);
	defer delete(cstr);

	s: OS_Stat;
	result := _unix_lstat(cstr, &s);
	if result == -1 {
		return s, Errno(get_last_error());
	}
	return s, ERROR_NONE;
}

@private
_fstat :: proc(fd: Handle) -> (OS_Stat, Errno) {
	s: OS_Stat;
	result := _unix_fstat(fd, &s);
	if result == -1 {
		return s, Errno(get_last_error());
	}
	return s, ERROR_NONE;
}

@private
_fdopendir :: proc(fd: Handle) -> (Dir, Errno) {
	dirp := _unix_fdopendir(fd);
	if dirp == cast(Dir)nil {
		return nil, Errno(get_last_error());
	}
	return dirp, ERROR_NONE;
}

@private
_closedir :: proc(dirp: Dir) -> Errno {
	rc := _unix_closedir(dirp);
	if rc != 0 {
		return Errno(get_last_error());
	}
	return ERROR_NONE;
}

@private
_rewinddir :: proc(dirp: Dir) {
	_unix_rewinddir(dirp);
}

@private
_readdir :: proc(dirp: Dir) -> (entry: Dirent, err: Errno, end_of_stream: bool) {
	result: ^Dirent;
	rc := _unix_readdir_r(dirp, &entry, &result);

	if rc != 0 {
		err = Errno(get_last_error());
		return;
	}
	err = ERROR_NONE;

	if result == nil {
		end_of_stream = true;
		return;
	}
	end_of_stream = false;

	return;
}

@private
_readlink :: proc(path: string) -> (string, Errno) {
	path_cstr := strings.clone_to_cstring(path);
	defer delete(path_cstr);

	bufsz : uint = 256;
	buf := make([]byte, bufsz);
	for {
		rc := _unix_readlink(path_cstr, &(buf[0]), bufsz);
		if rc == -1 {
			delete(buf);
			return "", Errno(get_last_error());
		} else if rc == int(bufsz) {
			// NOTE(laleksic, 2021-01-21): Any cleaner way to resize the slice?
			bufsz *= 2;
			delete(buf);
			buf = make([]byte, bufsz);
		} else {
			return strings.string_from_ptr(&buf[0], rc), ERROR_NONE;
		}
	}
}

absolute_path_from_handle :: proc(fd: Handle) -> (string, Errno) {
	buf : [256]byte;
	fd_str := strconv.itoa( buf[:], cast(int)fd );

	procfs_path := strings.concatenate( []string{ "/proc/self/fd/", fd_str } );
	defer delete(procfs_path);

	return _readlink(procfs_path);
}

absolute_path_from_relative :: proc(rel: string) -> (path: string, err: Errno) {
	rel := rel;
	if rel == "" {
		rel = ".";
	}

	rel_cstr := strings.clone_to_cstring(rel);
	defer delete(rel_cstr);

	path_ptr := _unix_realpath(rel_cstr, nil);
	if path_ptr == nil {
		return "", Errno(get_last_error());
	}
	defer _unix_free(path_ptr);

	path_cstr := transmute(cstring)path_ptr;
	path = strings.clone( string(path_cstr) );

	return path, ERROR_NONE;
}

access :: proc(path: string, mask: int) -> (bool, Errno) {
	cstr := strings.clone_to_cstring(path);
	defer delete(cstr);
	result := _unix_access(cstr, c.int(mask));
	if result == -1 {
		return false, Errno(get_last_error());
	}
	return true, ERROR_NONE;
}

getpwnam :: proc(name: string) -> ^passwd {
	cstr := strings.clone_to_cstring(name);
	defer delete(cstr);
	return _unix_getpwnam(cstr);
}

readlink :: proc(pathname: string, buf: []u8) -> (int, Errno) {
	cstr_pathname := strings.clone_to_cstring(pathname);
	defer delete(cstr_pathname);
	bytes_written := _unix_readlink(cstr_pathname, cstring(#no_bounds_check &buf[0]), c.size_t(len(buf)));
	if bytes_written == -1 {
		return -1, Errno(get_last_error());
	}
	return int(bytes_written), ERROR_NONE;
}

opendir :: proc(name: string) -> (^DIR, Errno) {
	cstr := strings.clone_to_cstring(name);
	defer delete(cstr);
	result := _unix_opendir(cstr);
	if result == nil {
		return nil, Errno(get_last_error());
	}
	return result, ERROR_NONE;
}

readdir :: proc(dirp: ^DIR) -> (^dirent, Errno) {
	previous := Errno(get_last_error());

	result := _unix_readdir(dirp);
	err := Errno(get_last_error());

	if result == nil && previous != err { // If nil and errno changed, err occured
		return nil, err;
	} else if result == nil { // If errno not changed, end of directory stream
		return nil, ERROR_NONE;
	}

	return result, ERROR_NONE;
}

closedir :: proc(dirp: ^DIR) -> Errno {
	result := _unix_closedir(dirp);
	if result == 0 {
		return ERROR_NONE;
	} else {
		return Errno(get_last_error());
	}
}

heap_alloc :: proc(size: int) -> rawptr {
	assert(size >= 0);
	return _unix_calloc(1, c.size_t(size));
}

heap_resize :: proc(ptr: rawptr, new_size: int) -> rawptr {
	return _unix_realloc(ptr, c.size_t(new_size));
}

heap_free :: proc(ptr: rawptr) {
	_unix_free(ptr);
}

getenv :: proc(name: string) -> (string, bool) {
	path_str := strings.clone_to_cstring(name);
	defer delete(path_str);
	cstr := _unix_getenv(path_str);
	if cstr == nil {
		return "", false;
	}
	return string(cstr), true;
}

setenv :: proc(name: string, value: string, overwrite: bool) -> Errno {
	name_str := strings.clone_to_cstring(name);
	defer delete(name_str);
	value_str := strings.clone_to_cstring(value);
	defer delete(value_str);
	result := _unix_setenv(name_str, value_str, overwrite ? 1 : 0);
	if result == -1 {
		return Errno(get_last_error());
	}

	return ERROR_NONE;
}

unsetenv :: proc(name: string) -> Errno {
	name_str := strings.clone_to_cstring(name);
	defer delete(name_str);
	result := _unix_unsetenv(name_str);
	if result == -1 {
		return Errno(get_last_error());
	}

	return ERROR_NONE;
}

get_current_directory :: proc() -> string {
	// NOTE(tetra): I would use PATH_MAX here, but I was not able to find
	// an authoritative value for it across all systems.
	// The largest value I could find was 4096, so might as well use the page size.
	page_size := get_page_size();
	buf := make([dynamic]u8, page_size);
	for {
		cwd := _unix_getcwd(cstring(#no_bounds_check &buf[0]), c.size_t(len(buf)));
		if cwd != nil {
			return string(cwd);
		}
		if Errno(get_last_error()) != ERANGE {
			return "";
		}
		resize(&buf, len(buf)+page_size);
	}
	unreachable();
}

set_current_directory :: proc(path: string) -> (err: Errno) {
	cstr := strings.clone_to_cstring(path, context.temp_allocator);
	res := _unix_chdir(cstr);
	if res == -1 {
		return Errno(get_last_error());
	}
	return ERROR_NONE;
}

exit :: proc "contextless" (code: int) -> ! {
	_unix_exit(c.int(code));
}

current_thread_id :: proc "contextless" () -> int {
	return cast(int)syscall(SYS_GETTID);
}

dlopen :: proc(filename: string, flags: int) -> rawptr {
	cstr := strings.clone_to_cstring(filename);
	defer delete(cstr);
	handle := _unix_dlopen(cstr, c.int(flags));
	return handle;
}
dlsym :: proc(handle: rawptr, symbol: string) -> rawptr {
	assert(handle != nil);
	cstr := strings.clone_to_cstring(symbol);
	defer delete(cstr);
	proc_handle := _unix_dlsym(handle, cstr);
	return proc_handle;
}
dlclose :: proc(handle: rawptr) -> bool {
	assert(handle != nil);
	return _unix_dlclose(handle) == 0;
}
dlerror :: proc() -> string {
	return string(_unix_dlerror());
}

get_page_size :: proc() -> int {
	// NOTE(tetra): The page size never changes, so why do anything complicated
	// if we don't have to.
	@static page_size := -1;
	if page_size != -1 {
		return page_size;
	}

	page_size = int(_unix_getpagesize());
	return page_size;
}


_alloc_command_line_arguments :: proc() -> []string {
	res := make([]string, len(runtime.args__));
	for arg, i in runtime.args__ {
		res[i] = string(arg);
	}
	return res;
}
