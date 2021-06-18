package os2

import "core:runtime"

// TODO(rytc): temporary stub
_heap_allocator_proc :: proc(allocator_data: rawptr, mode: runtime.Allocator_Mode, 
                             size, alignment: int,
                             old_memory: rawptr, old_size: int, loc := #caller_location) -> ([]byte, runtime.Allocator_Error) {

    return nil,nil;
}
                             
