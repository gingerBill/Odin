// +build windows
package win32

foreign import "system:kernel32.lib"

@(default_calling_convention = "std")
foreign kernel32 {
	@(link_name="CreateProcessA")		     create_process_a		      :: proc(application_name, command_line: cstring,
	                                                              				 process_attributes, thread_attributes: ^Security_Attributes,
	                                                              				 inherit_handle: Bool, creation_flags: u32, environment: rawptr,
	                                                              				 current_directory: cstring, startup_info: ^Startup_Info,
	                                                              				 process_information: ^Process_Information) -> Bool ---;
	@(link_name="CreateProcessW")            create_process_w             :: proc(application_name, command_line: Wstring,
	                                                                             process_attributes, thread_attributes: ^Security_Attributes,
	                                                                             inherit_handle: Bool, creation_flags: u32, environment: rawptr,
	                                                                             current_directory: Wstring, startup_info: ^Startup_Info,
	                                                                             process_information: ^Process_Information) -> Bool ---;
	@(link_name="GetExitCodeProcess")		 get_exit_code_process        :: proc(process: Handle, exit: ^u32) -> Bool ---;
	@(link_name="ExitProcess")               exit_process                 :: proc(exit_code: u32) ---;
	@(link_name="GetModuleHandleA")          get_module_handle_a          :: proc(module_name: cstring) -> Hmodule ---;
	@(link_name="GetModuleHandleW")          get_module_handle_w          :: proc(module_name: Wstring) -> Hmodule ---;

	@(link_name="GetModuleFileNameA")        get_module_file_name_a       :: proc(module: Hmodule, filename: cstring, size: u32) -> u32 ---;
	@(link_name="GetModuleFileNameW")        get_module_file_name_w       :: proc(module: Hmodule, filename: Wstring, size: u32) -> u32 ---;

	@(link_name="Sleep")                     sleep                        :: proc(ms: u32) ---;
	@(link_name="QueryPerformanceFrequency") query_performance_frequency  :: proc(result: ^i64) -> i32 ---;
	@(link_name="QueryPerformanceCounter")   query_performance_counter    :: proc(result: ^i64) -> i32 ---;
	@(link_name="OutputDebugStringA")        output_debug_string_a        :: proc(c_str: cstring) ---;

	@(link_name="GetCommandLineA")           get_command_line_a           :: proc() -> cstring ---;
	@(link_name="GetCommandLineW")           get_command_line_w           :: proc() -> Wstring ---;
	@(link_name="GetSystemMetrics")          get_system_metrics           :: proc(index: i32) -> i32 ---;
	@(link_name="GetSystemInfo")             get_system_info              :: proc(info: ^System_Info) ---;
	@(link_name="GetVersionExA")             get_version                  :: proc(osvi: ^OS_Version_Info_Ex_A) ---;
	@(link_name="GetCurrentThreadId")        get_current_thread_id        :: proc() -> u32 ---;

	// NOTE(tetra): Not thread safe with SetCurrentDirectory and GetFullPathName;
	// The current directory is stored as a global variable in the process.
	@(link_name="GetCurrentDirectoryW")       get_current_directory_w     :: proc(len: u32, buf: Wstring) -> u32 ---;
	@(link_name="SetCurrentDirectoryW")       set_current_directory_w     :: proc(buf: Wstring) -> u32 ---;

	@(link_name="GetSystemTimeAsFileTime")   get_system_time_as_file_time :: proc(system_time_as_file_time: ^Filetime) ---;
	@(link_name="FileTimeToLocalFileTime")   file_time_to_local_file_time :: proc(file_time: ^Filetime, local_file_time: ^Filetime) -> Bool ---;
	@(link_name="FileTimeToSystemTime")      file_time_to_system_time     :: proc(file_time: ^Filetime, system_time: ^Systemtime) -> Bool ---;
	@(link_name="SystemTimeToFileTime")      system_time_to_file_time     :: proc(system_time: ^Systemtime, file_time: ^Filetime) -> Bool ---;

	@(link_name="GetStdHandle")              get_std_handle               :: proc(h: i32) -> Handle ---;

	@(link_name="CreateFileA")
	create_file_a :: proc(filename: cstring, desired_access, share_module: u32,
	                      security: rawptr,
	                      creation, flags_and_attribs: u32, template_file: Handle) -> Handle ---;

	@(link_name="CreateFileW")
	create_file_w :: proc(filename: Wstring, desired_access, share_module: u32,
	                      security: rawptr,
	                      creation, flags_and_attribs: u32, template_file: Handle) -> Handle ---;


	@(link_name="ReadFile")  read_file  :: proc(h: Handle, buf: rawptr, to_read: u32, bytes_read: ^i32, overlapped: rawptr) -> Bool ---;
	@(link_name="WriteFile") write_file :: proc(h: Handle, buf: rawptr, len: i32, written_result: ^i32, overlapped: rawptr) -> Bool ---;

	@(link_name="GetFileSizeEx")              get_file_size_ex               :: proc(file_handle: Handle, file_size: ^i64) -> Bool ---;
	@(link_name="GetFileInformationByHandle") get_file_information_by_handle :: proc(file_handle: Handle, file_info: ^By_Handle_File_Information) -> Bool ---;

	@(link_name="CreateDirectoryA") 		  create_directory_a			 :: proc(path: cstring, security_attributes: ^Security_Attributes) -> Bool ---;
	@(link_name="CreateDirectoryW") 		  create_directory_w			 :: proc(path: Wstring, security_attributes: ^Security_Attributes) -> Bool ---;

	@(link_name="GetFileType")    get_file_type    :: proc(file_handle: Handle) -> u32 ---;
	@(link_name="SetFilePointer") set_file_pointer :: proc(file_handle: Handle, distance_to_move: i32, distance_to_move_high: ^i32, move_method: u32) -> u32 ---;

	@(link_name="SetHandleInformation") set_handle_information :: proc(obj: Handle, mask, flags: u32) -> Bool ---;

	@(link_name="FindFirstFileA") find_first_file_a :: proc(file_name: cstring, data: ^Find_Data_A) -> Handle ---;
	@(link_name="FindNextFileA")  find_next_file_a  :: proc(file: Handle, data: ^Find_Data_A) -> Bool ---;

	@(link_name="FindFirstFileW") find_first_file_w :: proc(file_name: Wstring, data: ^Find_Data_W) -> Handle ---;
	@(link_name="FindNextFileW")  find_next_file_w  :: proc(file: Handle, data: ^Find_Data_W) -> Bool ---;

	@(link_name="FindClose")      find_close        :: proc(file: Handle) -> Bool ---;

	@(link_name="MoveFileExA")    move_file_ex_a    :: proc(existing, new: cstring, flags: u32) -> Bool ---;
	@(link_name="DeleteFileA")    delete_file_a     :: proc(file_name: cstring) -> Bool ---;
	@(link_name="CopyFileA")      copy_file_a       :: proc(existing, new: cstring, fail_if_exists: Bool) -> Bool ---;

	@(link_name="MoveFileExW")    move_file_ex_w    :: proc(existing, new: Wstring, flags: u32) -> Bool ---;
	@(link_name="DeleteFileW")    delete_file_w     :: proc(file_name: Wstring) -> Bool ---;
	@(link_name="CopyFileW")      copy_file_w       :: proc(existing, new: Wstring, fail_if_exists: Bool) -> Bool ---;

	@(link_name="HeapAlloc")      heap_alloc       :: proc(h: Handle, flags: u32, bytes: int) -> rawptr ---;
	@(link_name="HeapReAlloc")    heap_realloc     :: proc(h: Handle, flags: u32, memory: rawptr, bytes: int) -> rawptr ---;
	@(link_name="HeapFree")       heap_free        :: proc(h: Handle, flags: u32, memory: rawptr) -> Bool ---;
	@(link_name="GetProcessHeap") get_process_heap :: proc() -> Handle ---;

	@(link_name="LocalAlloc")     local_alloc      :: proc(flags: u32, bytes: int) -> rawptr ---;
	@(link_name="LocalReAlloc")   local_realloc    :: proc(mem: rawptr, bytes: int, flags: uint) -> rawptr ---;
	@(link_name="LocalFree")      local_free       :: proc(mem: rawptr) -> rawptr ---;

	@(link_name="FindFirstChangeNotificationA") find_first_change_notification_a :: proc(path: cstring, watch_subtree: Bool, filter: u32) -> Handle ---;
	@(link_name="FindNextChangeNotification")   find_next_change_notification    :: proc(h: Handle) -> Bool ---;
	@(link_name="FindCloseChangeNotification")  find_close_change_notification   :: proc(h: Handle) -> Bool ---;

	@(link_name="ReadDirectoryChangesW") read_directory_changes_w :: proc(dir: Handle, buf: rawptr, buf_length: u32,
	                                                                      watch_subtree: Bool, notify_filter: u32,
	                                                                      bytes_returned: ^u32, overlapped: ^Overlapped,
	                                                                      completion: rawptr) -> Bool ---;

	@(link_name="WideCharToMultiByte") wide_char_to_multi_byte :: proc(code_page: u32, flags: u32,
	                                                                   wchar_str: Wstring, wchar: i32,
	                                                                   multi_str: cstring, multi: i32,
	                                                                   default_char: cstring, used_default_char: ^Bool) -> i32 ---;

	@(link_name="MultiByteToWideChar") multi_byte_to_wide_char :: proc(code_page: u32, flags: u32,
	                                                                   mb_str: cstring, mb: i32,
	                                                                   wc_str: Wstring, wc: i32) -> i32 ---;

	@(link_name="CreateSemaphoreA")    create_semaphore_a     :: proc(attributes: ^Security_Attributes, initial_count, maximum_count: i32, name: cstring) -> Handle ---;
	@(link_name="CreateSemaphoreW")    create_semaphore_w     :: proc(attributes: ^Security_Attributes, initial_count, maximum_count: i32, name: cstring) -> Handle ---;
	@(link_name="ReleaseSemaphore")    release_semaphore      :: proc(semaphore: Handle, release_count: i32, previous_count: ^i32) -> Bool ---;
	@(link_name="WaitForSingleObject") wait_for_single_object :: proc(handle: Handle, milliseconds: u32) -> u32 ---;
}

// @(default_calling_convention = "c")
foreign kernel32 {
	@(link_name="GetLastError")              get_last_error               :: proc() -> i32 ---;
	@(link_name="CloseHandle")               close_handle                 :: proc(h: Handle) -> i32 ---;

	@(link_name="GetFileAttributesA")         get_file_attributes_a          :: proc(filename: cstring) -> u32 ---;
	@(link_name="GetFileAttributesW")         get_file_attributes_w          :: proc(filename: Wstring) -> u32 ---;
	@(link_name="GetFileAttributesExA")       get_file_attributes_ex_a       :: proc(filename: cstring, info_level_id: GET_FILEEX_INFO_LEVELS, file_info: ^File_Attribute_Data) -> Bool ---;
	@(link_name="GetFileAttributesExW")       get_file_attributes_ex_w       :: proc(filename: Wstring, info_level_id: GET_FILEEX_INFO_LEVELS, file_info: ^File_Attribute_Data) -> Bool ---;
	@(link_name="CompareFileTime")            compare_file_time              :: proc(a, b: ^Filetime) -> i32 ---;
}

@(default_calling_convention = "c")
foreign kernel32 {
	@(link_name="InterlockedCompareExchange") interlocked_compare_exchange :: proc(dst: ^i32, exchange, comparand: i32) -> i32 ---;
	@(link_name="InterlockedExchange")        interlocked_exchange         :: proc(dst: ^i32, desired: i32) -> i32 ---;
	@(link_name="InterlockedExchangeAdd")     interlocked_exchange_add     :: proc(dst: ^i32, desired: i32) -> i32 ---;
	@(link_name="InterlockedAnd")             interlocked_and              :: proc(dst: ^i32, desired: i32) -> i32 ---;
	@(link_name="InterlockedOr")              interlocked_or               :: proc(dst: ^i32, desired: i32) -> i32 ---;

	@(link_name="InterlockedCompareExchange64") interlocked_compare_exchange64 :: proc(dst: ^i64, exchange, comparand: i64) -> i64 ---;
	@(link_name="InterlockedExchange64")        interlocked_exchange64         :: proc(dst: ^i64, desired: i64) -> i64 ---;
	@(link_name="InterlockedExchangeAdd64")     interlocked_exchange_add64     :: proc(dst: ^i64, desired: i64) -> i64 ---;
	@(link_name="InterlockedAnd64")             interlocked_and64              :: proc(dst: ^i64, desired: i64) -> i64 ---;
	@(link_name="InterlockedOr64")              interlocked_or64               :: proc(dst: ^i64, desired: i64) -> i64 ---;
}

@(default_calling_convention = "std")
foreign kernel32 {
	@(link_name="_mm_pause")        mm_pause           :: proc() ---;
	@(link_name="ReadWriteBarrier") read_write_barrier :: proc() ---;
	@(link_name="WriteBarrier")     write_barrier      :: proc() ---;
	@(link_name="ReadBarrier")      read_barrier       :: proc() ---;

	@(link_name="CreateThread")
	create_thread :: proc(thread_attributes: ^Security_Attributes, stack_size: uint, start_routine: proc "stdcall" (rawptr) -> u32,
	                      parameter: rawptr, creation_flags: u32, thread_id: ^u32) -> Handle ---;
	@(link_name="ResumeThread")      resume_thread        :: proc(thread: Handle) -> u32 ---;
	@(link_name="GetThreadPriority") get_thread_priority  :: proc(thread: Handle) -> i32 ---;
	@(link_name="SetThreadPriority") set_thread_priority  :: proc(thread: Handle, priority: i32) -> Bool ---;
    @(link_name="GetExitCodeThread") get_exit_code_thread :: proc(thread: Handle, exit_code: ^u32) -> Bool ---;
	@(link_name="TerminateThread")   terminate_thread     :: proc(thread: Handle, exit_code: u32) -> Bool ---;

	@(link_name="InitializeCriticalSection")             initialize_critical_section                :: proc(critical_section: ^Critical_Section) ---;
	@(link_name="InitializeCriticalSectionAndSpinCount") initialize_critical_section_and_spin_count :: proc(critical_section: ^Critical_Section, spin_count: u32) -> b32 ---;
	@(link_name="DeleteCriticalSection")                 delete_critical_section                    :: proc(critical_section: ^Critical_Section) ---;
	@(link_name="SetCriticalSectionSpinCount")           set_critical_section_spin_count            :: proc(critical_section: ^Critical_Section, spin_count: u32) -> u32 ---;
	@(link_name="TryEnterCriticalSection")               try_enter_critical_section                 :: proc(critical_section: ^Critical_Section) -> b8 ---;
	@(link_name="EnterCriticalSection")                  enter_critical_section                     :: proc(critical_section: ^Critical_Section) ---;
	@(link_name="LeaveCriticalSection")                  leave_critical_section                     :: proc(critical_section: ^Critical_Section) ---;

	@(link_name="CreateEventA") create_event_a :: proc(event_attributes: ^Security_Attributes, manual_reset, initial_state: Bool, name: cstring) -> Handle ---;
	@(link_name="CreateEventW") create_event_w :: proc(event_attributes: ^Security_Attributes, manual_reset, initial_state: Bool, name: Wstring) -> Handle ---;
	@(link_name="PulseEvent")   pulse_event    :: proc(event: Handle) -> Bool ---;
	@(link_name="SetEvent")     set_event      :: proc(event: Handle) -> Bool ---;
	@(link_name="ResetEvent")   reset_event    :: proc(event: Handle) -> Bool ---;

	@(link_name="LoadLibraryA")   load_library_a   :: proc(c_str: cstring)  -> Hmodule ---;
	@(link_name="LoadLibraryW")   load_library_w   :: proc(c_str: Wstring) -> Hmodule ---;
	@(link_name="FreeLibrary")    free_library     :: proc(h: Hmodule) -> Bool ---;
	@(link_name="GetProcAddress") get_proc_address :: proc(h: Hmodule, c_str: cstring) -> rawptr ---;

	@(link_name="GetFullPathNameA")  get_full_path_name_a  :: proc(filename: cstring, buffer_length: u32, buffer: cstring, file_part: ^Wstring) -> u32 ---;
	@(link_name="GetFullPathNameW")  get_full_path_name_w  :: proc(filename: Wstring, buffer_length: u32, buffer: Wstring, file_part: ^Wstring) -> u32 ---;
	@(link_name="GetLongPathNameA")  get_long_path_name_a  :: proc(short, long: cstring, len: u32) -> u32 ---;
	@(link_name="GetLongPathNameW")  get_long_path_name_w  :: proc(short, long: Wstring, len: u32) -> u32 ---;
	@(link_name="GetShortPathNameA") get_short_path_name_a :: proc(long, short: cstring, len: u32) -> u32 ---;
	@(link_name="GetShortPathNameW") get_short_path_name_w :: proc(long, short: Wstring, len: u32) -> u32 ---;

	@(link_name="GetCurrentDirectoryA") get_current_directory_a :: proc(buffer_length: u32, buffer: cstring) -> u32 ---;
}

Memory_Basic_Information :: struct {
	base_address:       rawptr,
	allocation_base:    rawptr,
	allocation_protect: u32,
	partition_size:     u32,
	region_size:        uint,
	state:              u32,
	protect:            u32,
	type:               u32,
}

@(default_calling_convention = "std")
foreign kernel32 {
	@(link_name="VirtualAlloc")   virtual_alloc    :: proc(address: rawptr, size: uint, allocation_type: u32, protect: u32) -> rawptr ---
	@(link_name="VirtualAllocEx") virtual_alloc_ex :: proc(process: Handle, address: rawptr, size: uint, allocation_type: u32, protect: u32) -> rawptr ---
	@(link_name="VirtualFree")    virtual_free     :: proc(address: rawptr, size: uint, free_type: u32) -> Bool ---
	@(link_name="VirtualLock")    virtual_lock     :: proc(address: rawptr, size: uint) -> Bool ---
	@(link_name="VirtualProtect") virtual_protect  :: proc(address: rawptr, size: uint, new_protect: u32, old_protect: ^u32) -> Bool ---
	@(link_name="VirtualQuery")   virtual_query    :: proc(address: rawptr, buffer: ^Memory_Basic_Information, length: uint) -> uint ---
}

MEM_COMMIT      :: 0x00001000;
MEM_RESERVE     :: 0x00002000;
MEM_DECOMMIT    :: 0x00004000;
MEM_RELEASE     :: 0x00008000;
MEM_RESET       :: 0x00080000;
MEM_RESET_UNDO  :: 0x01000000;

MEM_LARGE_PAGES :: 0x20000000;
MEM_PHYSICAL    :: 0x00400000;
MEM_TOP_DOWN    :: 0x00100000;
MEM_WRITE_WATCH :: 0x00200000;

PAGE_NOACCESS           :: 0x01;
PAGE_READONLY           :: 0x02;
PAGE_READWRITE          :: 0x04;
PAGE_WRITECOPY          :: 0x08;
PAGE_EXECUTE            :: 0x10;
PAGE_EXECUTE_READ       :: 0x20;
PAGE_EXECUTE_READWRITE  :: 0x40;
PAGE_EXECUTE_WRITECOPY  :: 0x80;
