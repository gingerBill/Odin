package c

import b "core:builtin"
import "core:os"

CHAR_BIT :: 8;

bool   :: b.bool;
char   :: b.u8;
byte   :: b.byte;
schar  :: b.i8;
uchar  :: b.u8;
short  :: b.i16;
ushort :: b.u16;
int    :: b.i32;
uint   :: b.u32;

long  :: (os.OS == "windows" || size_of(b.rawptr) == 4) ? b.i32 : b.i64;
ulong :: (os.OS == "windows" || size_of(b.rawptr) == 4) ? b.u32 : b.u64;

longlong       :: b.i64;
ulonglong      :: b.u64;
float          :: b.f32;
double         :: b.f64;
complex_float  :: b.complex64;
complex_double :: b.complex128;

#assert(size_of(b.uintptr) == size_of(b.int));

size_t    :: b.uint;
ssize_t   :: b.int;
ptrdiff_t :: b.int;
uintptr_t :: b.uintptr;
intptr_t  :: b.int;
