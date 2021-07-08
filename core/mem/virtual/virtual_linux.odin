package virtual

PROT_GROWSDOWN :: 0x01000000;
PROT_GROWSUP :: 0x02000000;


MAP_SHARED :: 0x01; // Share changes
MAP_PRIVATE :: 0x02; // Changes are private
MAP_SHARED_VALIDATE :: 0x03; // share + validate extension flags
MAP_TYPE :: 0x0f; // Mask for type of mapping

MAP_FIXED :: 0x10; // Interpret addr exactly
MAP_ANONYMOUS :: 0x20; // don't use a file

// MAP_ 0x0100 - 0x4000 flags are per architecture

MAP_POPULATE :: 0x8000; // populate (prefault) pagetables
MAP_NONBLOCK :: 0x10000; // do not block on IO
MAP_STACK :: 0x20000; // give out an address that is best suited for process/thread stacks
MAP_HUGETLB :: 0x40000; // create a huge page mapping
MAP_SYNC :: 0x80000; // perform synchronous page faults for the mapping
MAP_FIXED_NOREPLACE :: 0x100000; // MAP_FIXED which doesn't unmap underlying mapping
MAP_UNINITIALIZED :: 0x4000000; // For anonymous mmap, memory could be uninitialized


MADV_FREE :: 8;	/* Free pages only if memory pressure.  */
MADV_REMOVE :: 9;	/* Remove these pages and resources.  */
MADV_DONTFORK :: 10;	/* Do not inherit across fork.  */
MADV_DOFORK :: 11;	/* Do inherit across fork.  */
MADV_MERGEABLE :: 12;	/* KSM may merge identical pages.  */
MADV_UNMERGEABLE :: 13;	/* KSM may not merge identical pages.  */
MADV_HUGEPAGE :: 14;	/* Worth backing with hugepages.  */
MADV_NOHUGEPAGE :: 15;	/* Not worth backing with hugepages.  */
MADV_DONTDUMP :: 16;    /* Explicity exclude from the core dump,
                                   overrides the coredump filter bits.  */
MADV_DODUMP :: 17;	/* Clear the MADV_DONTDUMP flag.  */
MADV_WIPEONFORK :: 18;	/* Zero memory on fork, child only.  */
MADV_KEEPONFORK :: 19;	/* Undo MADV_WIPEONFORK.  */
MADV_HWPOISON :: 100;	/* Poison a page for testing.  */
