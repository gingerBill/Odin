package os2


// TODO(rytc): temp stub
_create_temp :: proc(dir, pattern: string) -> (Handle, Error) {
    return 0, General_Error.Invalid_Argument;	
}

// TODO(rytc): temp stub
_mkdir_temp :: proc(dir, pattern: string, allocator := context.allocator) -> (string, Error) {
    return "", General_Error.Invalid_Argument;	
}

// TODO(rytc): temp stub
_temp_dir :: proc(allocator := context.allocator) -> string {
    return "/tmp/";
}
