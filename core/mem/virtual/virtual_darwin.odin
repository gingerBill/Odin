package virtual

MAP_SHARED  :: 0x0001; /* [MF|SHM] share changes */
MAP_PRIVATE :: 0x0002; /* [MF|SHM] changes are private */

// NOTE(tetra): Not sure which of these should be here, so I just copied almost all of them
// from Darwin's mman.h.
MAP_FIXED        :: 0x0010; /* [MF|SHM] interpret addr exactly */
MAP_RENAME       :: 0x0020; /* Sun: rename private pages to file */
MAP_NORESERVE    :: 0x0040; /* Sun: don't reserve needed swap area */
MAP_NOEXTEND     :: 0x0100; /* for MAP_FILE, don't change file size */
MAP_HASSEMAPHORE :: 0x0200; /* region may contain semaphores */
MAP_NOCACHE      :: 0x0400; /* don't cache pages for this mapping */
MAP_JIT          :: 0x0800; /* Allocate a region that will be used for JIT purposes */

MAP_FILE      :: 0x0000; /* map from file (default) */
MAP_ANON      :: 0x1000; /* allocated from memory, swap space */
MAP_ANONYMOUS :: MAP_ANON;

MADV_FREE             :: 5; /* pages unneeded, discard contents */
MADV_ZERO_WIRED_PAGES :: 6; /* zero the wired pages that have not been unwired before the entry is deleted */
MADV_FREE_REUSABLE    :: 7; /* pages can be reused (by anyone) */
MADV_FREE_REUSE       :: 8; /* caller wants to reuse those pages */
MADV_CAN_REUSE        :: 9;
