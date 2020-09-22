#define IR_FILE_BUFFER_BUF_LEN (4096)

struct irFileBuffer {
	gbVirtualMemory vm;
	isize           offset;
	gbFile *        output;
	char            buf[IR_FILE_BUFFER_BUF_LEN];
};

void ir_file_buffer_init(irFileBuffer *f, gbFile *output) {
	isize size = 8*gb_virtual_memory_page_size(nullptr);
	f->vm = gb_vm_alloc(nullptr, size);
	f->offset = 0;
	f->output = output;
}

void ir_file_buffer_destroy(irFileBuffer *f) {
	if (f->offset > 0) {
		// NOTE(bill): finish writing buffered data
		gb_file_write(f->output, f->vm.data, f->offset);
	}

	gb_vm_free(f->vm);
}

void ir_file_buffer_write(irFileBuffer *f, void const *data, isize len) {
	if (len > f->vm.size) {
		//NOTE(thebirk): Flush the vm data before we print this directly
		//               otherwise we get out of order printing which is no good
		gb_file_write(f->output, f->vm.data, f->offset);
		f->offset = 0;

		gb_file_write(f->output, data, len);
		return;
	}

	if ((f->vm.size - f->offset) < len) {
		gb_file_write(f->output, f->vm.data, f->offset);
		f->offset = 0;
	}
	u8 *cursor = cast(u8 *)f->vm.data + f->offset;
	gb_memmove(cursor, data, len);
	f->offset += len;
}


void ir_fprintf(irFileBuffer *f, char const *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	isize len = gb_snprintf_va(f->buf, IR_FILE_BUFFER_BUF_LEN-1, fmt, va);
	ir_file_buffer_write(f, f->buf, len-1);
	va_end(va);
}
void ir_write_string(irFileBuffer *f, String s) {
	ir_file_buffer_write(f, s.text, s.len);
}

#if 1
#define ir_write_str_lit(f, s) ir_write_string((f), str_lit(s))
#else
void ir_write_str_lit(irFileBuffer *f, char const *s) {
	isize len = gb_strlen(s);
	ir_file_buffer_write(f, s, len);
}
#endif
void ir_write_byte(irFileBuffer *f, u8 c) {
	ir_file_buffer_write(f, &c, 1);
}
void ir_write_i64(irFileBuffer *f, i64 i) {
	String str = i64_to_string(i, f->buf, IR_FILE_BUFFER_BUF_LEN-1);
	ir_write_string(f, str);
}
void ir_write_u64(irFileBuffer *f, u64 i) {
	String str = u64_to_string(i, f->buf, IR_FILE_BUFFER_BUF_LEN-1);
	ir_write_string(f, str);
}
void ir_write_big_int(irFileBuffer *f, BigInt const &x, Type *type, bool swap_endian) {
	if (x.len == 2) {
		gbAllocator a = heap_allocator(); // TODO(bill): Change this allocator
		u64 words[2] = {};
		BigInt y = x;
		if (swap_endian) {
			gb_memmove(words, y.d.words, 16);
			u8 *bytes = cast(u8 *)words;
			for (isize i = 0; i < 8; i++) {
				bytes[i] = bytes[16-i]; // byte swap
			}
			y.d.words = words;
		}

		String s = big_int_to_string(a, &y, 10);
		ir_write_string(f, s);
		gb_free(a, s.text);
	} else {
		i64 i = 0;
		if (x.neg) {
			i = big_int_to_i64(&x);
		} else {
			i = cast(i64)big_int_to_u64(&x);
		}
		if (swap_endian) {
			i64 size = type_size_of(type);
			switch (size) {
			case 2: i = cast(i64)cast(i16)gb_endian_swap16(cast(u16)cast(i16)i); break;
			case 4: i = cast(i64)cast(i32)gb_endian_swap32(cast(u32)cast(i32)i); break;
			case 8: i = cast(i64)gb_endian_swap64(cast(u64)i); break;
			}
		}
		ir_write_i64(f, i);
	}
}

void ir_file_write(irFileBuffer *f, void *data, isize len) {
	ir_file_buffer_write(f, data, len);
}


bool ir_valid_char(u8 c) {
	if (c >= 0x80) {
		return false;
	}

	if (gb_char_is_alphanumeric(c)) {
		return true;
	}

	switch (c) {
	case '$':
	case '-':
	case '.':
	case '_':
		return true;
	}

	return false;
}

void ir_print_escape_string(irFileBuffer *f, String name, bool print_quotes, bool prefix_with_dot) {
	isize extra = 0;
	for (isize i = 0; i < name.len; i++) {
		u8 c = name[i];
		if (!ir_valid_char(c)) {
			extra += 2;
		}
	}

	if (extra == 0) {
		ir_write_string(f, name);
		return;
	}

	char const hex_table[] = "0123456789ABCDEF";
	isize buf_len = name.len + extra + 2 + 1;

	gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&string_buffer_arena);

	u8 *buf = gb_alloc_array(string_buffer_allocator, u8, buf_len);

	isize j = 0;

	if (print_quotes) {
		buf[j++] = '"';
	}

	if (prefix_with_dot) {
		buf[j++] = '.';
	}

	for (isize i = 0; i < name.len; i++) {
		u8 c = name[i];
		if (ir_valid_char(c)) {
			buf[j++] = c;
		} else {
			buf[j] = '\\';
			buf[j+1] = hex_table[c >> 4];
			buf[j+2] = hex_table[c & 0x0f];
			j += 3;
		}
	}

	if (print_quotes) {
		buf[j++] = '"';
	}

	ir_file_write(f, buf, j);

	gb_temp_arena_memory_end(tmp);
}


void ir_print_escape_path(irFileBuffer *f, String path) {
	isize extra = 0;
	for (isize i = 0; i < path.len; i++) {
		u8 c = path[i];
		if (!ir_valid_char(c)) {
			extra += 2;
		}
	}

	if (extra == 0) {
		ir_write_string(f, path);
		return;
	}


	char hex_table[] = "0123456789ABCDEF";
	isize buf_len = path.len + extra + 2 + 1;

	gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&string_buffer_arena);

	u8 *buf = gb_alloc_array(string_buffer_allocator, u8, buf_len);

	isize j = 0;

	for (isize i = 0; i < path.len; i++) {
		u8 c = path[i];
		if (ir_valid_char(c) || c == ':') {
			buf[j++] = c;
		} else if (c == '\\') {
			buf[j++] = '/';
		} else {
			buf[j] = '\\';
			buf[j+1] = hex_table[c >> 4];
			buf[j+2] = hex_table[c & 0x0f];
			j += 3;
		}
	}

	ir_file_write(f, buf, j);

	gb_temp_arena_memory_end(tmp);
}


void ir_print_encoded_local(irFileBuffer *f, String name) {
	ir_write_byte(f, '%');
	ir_print_escape_string(f, name, true, false);
}

void ir_print_encoded_global(irFileBuffer *f, String name, bool remove_prefix) {
	ir_write_byte(f, '@');
	ir_print_escape_string(f, name, true, !remove_prefix);
}


bool ir_print_debug_location(irFileBuffer *f, irModule *m, irValue *v) {
	if (!m->generate_debug_info) {
		return false;
	}

	GB_ASSERT_NOT_NULL(v);
	GB_ASSERT(v->kind == irValue_Instr);

	if (v->loc != nullptr) {
		if (v->loc->kind == irDebugInfo_Location) {
			ir_fprintf(f, ", !dbg !%d", v->loc->id);
			return true;
		}
	} else {
		irProcedure *proc = v->Instr.block->proc;
		if (proc->entity != nullptr) {
			// GB_ASSERT(proc->is_entry_point || (string_compare(proc->name, str_lit(IR_STARTUP_RUNTIME_PROC_NAME)) == 0));
		}
	}
	return false;
}

void ir_print_type(irFileBuffer *f, irModule *m, Type *t, bool in_struct = false);
void ir_print_value(irFileBuffer *f, irModule *m, irValue *value, Type *type_hint);


void ir_print_alignment_prefix_hack(irFileBuffer *f, i64 alignment) {
	// NOTE(bill): This is written like this as it may need to
	// changed for specific alignments
	switch (alignment) {
	case 1:
		ir_write_string(f, str_lit("[0 x i8]"));
		break;
	case 2:
		ir_write_string(f, str_lit("[0 x i16]"));
		break;
	case 4:
		ir_write_string(f, str_lit("[0 x i32]"));
		break;
	case 8:
		ir_write_string(f, str_lit("[0 x i64]"));
		break;
	case 16:
		ir_write_string(f, str_lit("[0 x <4 x i32>]"));
		break;
	default:
		GB_PANIC("Invalid alignment");
		break;
	}
}


void ir_print_proc_results(irFileBuffer *f, irModule *m, Type *t) {
	set_procedure_abi_types(heap_allocator(), t);

	GB_ASSERT(is_type_proc(t));
	t = base_type(t);
	isize result_count = t->Proc.result_count;
	if (result_count == 0 || t->Proc.return_by_pointer) {
		ir_write_string(f, str_lit("void"));
	} else {
		Type *rt = t->Proc.abi_compat_result_type;
		if (!is_type_tuple(rt)) {
			ir_print_type(f, m, rt);
		} else if (rt->Tuple.variables.count == 1) {
			ir_print_type(f, m, rt->Tuple.variables[0]->type);
		} else {
			ir_write_byte(f, '{');
			for_array(i, rt->Tuple.variables) {
				Entity *e = rt->Tuple.variables[i];
				if (i > 0) {
					ir_write_string(f, str_lit(", "));
				}
				ir_print_type(f, m, e->type);
			}
			ir_write_byte(f, '}');
		}
	}
}


void ir_print_proc_type_without_pointer(irFileBuffer *f, irModule *m, Type *t) {
	set_procedure_abi_types(heap_allocator(), t);

	i64 word_bits = 8*build_context.word_size;
	t = base_type(t);
	GB_ASSERT(is_type_proc(t));

	isize param_count = t->Proc.param_count;
	isize result_count = t->Proc.result_count;

	ir_print_proc_results(f, m, t);
	ir_write_string(f, str_lit(" ("));
	if (t->Proc.return_by_pointer) {
		ir_print_type(f, m, reduce_tuple_to_single_type(t->Proc.results));
		// ir_fprintf(f, "* sret noalias ");
		// ir_write_string(f, str_lit("* noalias "));
		ir_write_string(f, str_lit("*"));
		if (build_context.ODIN_OS == "darwin" ||
		    build_context.ODIN_OS == "linux") {
			ir_fprintf(f, " byval");
		}
		if (param_count > 0 || t->Proc.calling_convention == ProcCC_Odin)  {
			ir_write_string(f, str_lit(", "));
		}
	}
	isize param_index = 0;
	for (isize i = 0; i < param_count; i++) {
		Entity *e = t->Proc.params->Tuple.variables[i];
		if (e->kind != Entity_Variable) continue;
		if (param_index > 0) ir_write_string(f, str_lit(", "));

		if (i+1 == param_count && t->Proc.c_vararg) {
			ir_write_string(f, str_lit("..."));
		} else {
			Type *et = t->Proc.abi_compat_params[i];
			if (is_type_tuple(et)) {
				for_array(j, et->Tuple.variables) {
					if (j > 0) ir_write_str_lit(f, ", ");

					ir_print_type(f, m, et->Tuple.variables[j]->type);
					if (e->flags&EntityFlag_NoAlias) {
						ir_write_str_lit(f, " noalias");
					}
					if (e->flags&EntityFlag_ByVal) {
						ir_write_str_lit(f, " byval");
					}
					ir_write_byte(f, ' ');
					param_index++;
				}
				continue;
			} else {
				ir_print_type(f, m, et);
			}
		}

		param_index++;
	}
	if (t->Proc.calling_convention == ProcCC_Odin) {
		if (param_index > 0) ir_write_string(f, str_lit(", "));
		ir_print_type(f, m, t_context_ptr);
	}
	ir_write_byte(f, ')');
}

void ir_print_type(irFileBuffer *f, irModule *m, Type *t, bool in_struct) {
	i64 word_bits = 8*build_context.word_size;
	GB_ASSERT_NOT_NULL(t);
	t = default_type(t);
	GB_ASSERT(is_type_typed(t));

	switch (t->kind) {
	case Type_Basic:
		switch (t->Basic.kind) {
		case Basic_llvm_bool: ir_write_str_lit(f, "i1");  return;
		case Basic_bool:      ir_write_str_lit(f, "i8");  return;
		case Basic_b8:        ir_write_str_lit(f, "i8");  return;
		case Basic_b16:       ir_write_str_lit(f, "i16"); return;
		case Basic_b32:       ir_write_str_lit(f, "i32"); return;
		case Basic_b64:       ir_write_str_lit(f, "i64"); return;

		case Basic_i8:    ir_write_str_lit(f, "i8");   return;
		case Basic_u8:    ir_write_str_lit(f, "i8");   return;
		case Basic_i16:   ir_write_str_lit(f, "i16");  return;
		case Basic_u16:   ir_write_str_lit(f, "i16");  return;
		case Basic_i32:   ir_write_str_lit(f, "i32");  return;
		case Basic_u32:   ir_write_str_lit(f, "i32");  return;
		case Basic_i64:   ir_write_str_lit(f, "i64");  return;
		case Basic_u64:   ir_write_str_lit(f, "i64");  return;
		case Basic_i128:  ir_write_str_lit(f, "i128"); return;
		case Basic_u128:  ir_write_str_lit(f, "i128"); return;

		case Basic_i16le:  ir_write_str_lit(f, "i16");  return;
		case Basic_u16le:  ir_write_str_lit(f, "i16");  return;
		case Basic_i32le:  ir_write_str_lit(f, "i32");  return;
		case Basic_u32le:  ir_write_str_lit(f, "i32");  return;
		case Basic_i64le:  ir_write_str_lit(f, "i64");  return;
		case Basic_u64le:  ir_write_str_lit(f, "i64");  return;
		case Basic_i128le: ir_write_str_lit(f, "i128"); return;
		case Basic_u128le: ir_write_str_lit(f, "i128"); return;

		case Basic_i16be:  ir_write_str_lit(f, "i16");  return;
		case Basic_u16be:  ir_write_str_lit(f, "i16");  return;
		case Basic_i32be:  ir_write_str_lit(f, "i32");  return;
		case Basic_u32be:  ir_write_str_lit(f, "i32");  return;
		case Basic_i64be:  ir_write_str_lit(f, "i64");  return;
		case Basic_u64be:  ir_write_str_lit(f, "i64");  return;
		case Basic_i128be: ir_write_str_lit(f, "i128"); return;
		case Basic_u128be: ir_write_str_lit(f, "i128"); return;

		case Basic_rune: ir_write_str_lit(f, "i32"); return;

		case Basic_int:
		case Basic_uint:
		case Basic_uintptr:
			switch (word_bits) {
			case 32: ir_write_str_lit(f, "i32"); break;
			case 64: ir_write_str_lit(f, "i64"); break;
			default: GB_PANIC("Unhandled word size: %td bits", word_bits); break;
			}
			return;

		// case Basic_f16:    ir_write_str_lit(f, "half");                 return;
		case Basic_f32:    ir_write_str_lit(f, "float");                   return;
		case Basic_f64:    ir_write_str_lit(f, "double");                  return;


		case Basic_f32le:    ir_write_str_lit(f, "float");  return;
		case Basic_f64le:    ir_write_str_lit(f, "double"); return;

		case Basic_f32be:    ir_write_str_lit(f, "float");   return;
		case Basic_f64be:    ir_write_str_lit(f, "double");  return;

		// case Basic_complex32:  ir_write_str_lit(f, "%%..complex32");    return;
		case Basic_complex64:  ir_write_str_lit(f, "%..complex64");        return;
		case Basic_complex128: ir_write_str_lit(f, "%..complex128");       return;

		case Basic_quaternion128: ir_write_str_lit(f, "%..quaternion128"); return;
		case Basic_quaternion256: ir_write_str_lit(f, "%..quaternion256"); return;

		case Basic_any:     ir_write_str_lit(f, "%..any");                 return;
		case Basic_rawptr:  ir_write_str_lit(f, "%..rawptr");              return;
		case Basic_string:  ir_write_str_lit(f, "%..string");              return;
		case Basic_cstring: ir_write_str_lit(f, "i8*");                    return;

		case Basic_typeid:  ir_write_str_lit(f, "%..typeid");              return;
		}
		break;

	case Type_Pointer:
		// TODO(bill, 2019-12-22): Why did I make add this edge case? Is this some weird LLVM issue?
		// if (!is_type_named(t->Pointer.elem) && is_type_empty_struct(t->Pointer.elem)) {
		// 	ir_print_type(f, m, t_rawptr);
		// } else {
			ir_print_type(f, m, t->Pointer.elem);
			ir_write_byte(f, '*');
		// }
		return;
	case Type_Array:
		ir_write_byte(f, '[');
		ir_write_i64(f, t->Array.count);
		ir_write_str_lit(f, " x ");
		ir_print_type(f, m, t->Array.elem);
		ir_write_byte(f, ']');
		return;
	case Type_EnumeratedArray:
		ir_write_byte(f, '[');
		ir_write_i64(f, t->EnumeratedArray.count);
		ir_write_str_lit(f, " x ");
		ir_print_type(f, m, t->EnumeratedArray.elem);
		ir_write_byte(f, ']');
		return;

	case Type_Slice:
		ir_write_byte(f, '{');
		ir_print_type(f, m, t->Slice.elem); ir_write_str_lit(f, "*, ");
		ir_print_type(f, m, t_int);
		ir_write_byte(f, '}');
		return;
	case Type_DynamicArray:
		ir_write_byte(f, '{');
		ir_print_type(f, m, t->DynamicArray.elem); ir_write_str_lit(f, "*, ");
		ir_print_type(f, m, t_int); ir_write_str_lit(f, ", ");
		ir_print_type(f, m, t_int); ir_write_str_lit(f, ", ");
		ir_print_type(f, m, t_allocator);
		ir_write_byte(f, '}');
		return;

	case Type_Enum:
		ir_print_type(f, m, base_enum_type(t));
		return;

	case Type_Union:
		if (t->Union.variants.count == 0) {
			ir_print_encoded_local(f, str_lit("..opaque"));
		} else {
			// NOTE(bill): The zero size array is used to fix the alignment used in a structure as
			// LLVM takes the first element's alignment as the entire alignment (like C)
			i64 align = type_align_of(t);

			if (is_type_union_maybe_pointer_original_alignment(t)) {
				ir_write_byte(f, '{');
				ir_print_type(f, m, t->Union.variants[0]);
				ir_write_byte(f, '}');
				return;
			}

			i64 block_size =  t->Union.variant_block_size;

			ir_write_byte(f, '{');
			ir_print_alignment_prefix_hack(f, align);
			if (is_type_union_maybe_pointer(t)) {
				ir_fprintf(f, ", ");
				ir_print_type(f, m, t->Union.variants[0]);
			} else {
				ir_fprintf(f, ", [%lld x i8], ", block_size);
				ir_print_type(f, m, union_tag_type(t));
			}
			ir_write_byte(f, '}');
		}
		return;

	case Type_Struct: {
		if (t->Struct.is_raw_union) {
			// NOTE(bill): The zero size array is used to fix the alignment used in a structure as
			// LLVM takes the first element's alignment as the entire alignment (like C)
			i64 size_of_union  = type_size_of(t);
			i64 align_of_union = type_align_of(t);
			ir_write_byte(f, '{');
			ir_print_alignment_prefix_hack(f, align_of_union);
			ir_fprintf(f, ", [%lld x i8]}", size_of_union);
			return;
		} else {
			if (t->Struct.is_packed) {
				ir_write_byte(f, '<');
			}
			ir_write_byte(f, '{');
			if (t->Struct.custom_align > 0) {
				ir_print_alignment_prefix_hack(f, t->Struct.custom_align);
				if (t->Struct.fields.count > 0) {
					ir_write_string(f, str_lit(", "));
				}
			}
			for_array(i, t->Struct.fields) {
				if (i > 0) {
					ir_write_string(f, str_lit(", "));
				}
				ir_print_type(f, m, t->Struct.fields[i]->type, true);
			}
			ir_write_byte(f, '}');
			if (t->Struct.is_packed) {
				ir_write_byte(f, '>');
			}
			return;
		}
		break;
	}

	case Type_Named: {
		Type *bt = base_type(t);
		switch (bt->kind) {
		case Type_Struct:
		case Type_Union: {
			Entity *e = t->Named.type_name;
			GB_ASSERT(e != nullptr);
			GB_ASSERT(e->kind == Entity_TypeName);

			String name = e->TypeName.ir_mangled_name;
			if (name.len > 0) {
				ir_print_encoded_local(f, name);
			} else {
				// TODO(bill): Is this correct behaviour?!
				// GB_ASSERT_MSG(name.len > 0, "%.*s %p", LIT(t->Named.name), e);
				// gb_printf_err("%.*s %p\n", LIT(t->Named.name), t->Named.type_name);
				ir_print_type(f, m, bt);
			}
			break;
		}
		default:
			ir_print_type(f, m, bt);
			break;
		}
		return;
	}

	case Type_Tuple:
		if (t->Tuple.variables.count == 1) {
			ir_print_type(f, m, t->Tuple.variables[0]->type);
		} else {
			ir_write_byte(f, '{');
			isize index = 0;
			for_array(i, t->Tuple.variables) {
				if (i > 0) ir_write_string(f, str_lit(", "));
				Entity *e = t->Tuple.variables[i];
				if (e->kind == Entity_Variable) {
					ir_print_type(f, m, e->type);
					index++;
				}
			}
			ir_write_byte(f, '}');
		}
		return;

	case Type_Proc:
		ir_print_proc_type_without_pointer(f, m, t);
		ir_write_byte(f, '*');
		return;

	case Type_Map:
		init_map_internal_types(t);
		GB_ASSERT(t->Map.internal_type != nullptr);
		ir_print_type(f, m, t->Map.internal_type);
		break;

	case Type_BitField: {
		i64 align = type_align_of(t);
		i64 size  = type_size_of(t);
		ir_write_string(f, str_lit("<{"));
		ir_print_alignment_prefix_hack(f, align);
		ir_fprintf(f, ", [%lld x i8]}>", size);
		break;
	}

	case Type_BitSet: {
		i64 size = type_size_of(t);
		if (size == 0) {
			ir_write_str_lit(f, "{}");
			return;
		}
		ir_print_type(f, m, bit_set_to_int(t));
		return;
	}

	case Type_Opaque:
		ir_print_type(f, m, strip_opaque_type(t));
		return;

	case Type_SimdVector:
		if (t->SimdVector.is_x86_mmx) {
			ir_write_str_lit(f, "x86_mmx");
		} else {
			ir_fprintf(f, "<%lld x ", t->SimdVector.count);;
			ir_print_type(f, m, t->SimdVector.elem);
			ir_write_byte(f, '>');
		}
		return;

	case Type_RelativePointer:
		ir_print_type(f, m, t->RelativePointer.base_integer);
		return;

	case Type_RelativeSlice:
		ir_write_byte(f, '{');
		ir_print_type(f, m, t->RelativePointer.base_integer);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, t->RelativePointer.base_integer);
		ir_write_byte(f, '}');
		return;
	}
}

void ir_print_debug_encoding(irFileBuffer *f, irDebugInfoKind kind, irDebugEncoding encoding) {
	switch (kind) {
	case irDebugInfo_BasicType:
		switch (encoding) {
		case irDebugBasicEncoding_address:       ir_write_str_lit(f, "DW_ATE_address");       return;
		case irDebugBasicEncoding_boolean:       ir_write_str_lit(f, "DW_ATE_boolean");       return;
		case irDebugBasicEncoding_float:         ir_write_str_lit(f, "DW_ATE_float");         return;
		case irDebugBasicEncoding_signed:        ir_write_str_lit(f, "DW_ATE_signed");        return;
		case irDebugBasicEncoding_signed_char:   ir_write_str_lit(f, "DW_ATE_signed_char");   return;
		case irDebugBasicEncoding_unsigned:      ir_write_str_lit(f, "DW_ATE_unsigned");      return;
		case irDebugBasicEncoding_unsigned_char: ir_write_str_lit(f, "DW_ATE_unsigned_char"); return;
		}
	case irDebugInfo_DerivedType:
		switch (encoding) {
		case irDebugBasicEncoding_member:       ir_write_str_lit(f, "DW_TAG_member");       return;
		case irDebugBasicEncoding_pointer_type: ir_write_str_lit(f, "DW_TAG_pointer_type"); return;
		case irDebugBasicEncoding_typedef:      ir_write_str_lit(f, "DW_TAG_typedef");      return;
		}
	case irDebugInfo_CompositeType:
		switch (encoding) {
		case irDebugBasicEncoding_array_type:       ir_write_str_lit(f, "DW_TAG_array_type");       return;
		case irDebugBasicEncoding_enumeration_type: ir_write_str_lit(f, "DW_TAG_enumeration_type"); return;
		case irDebugBasicEncoding_structure_type:   ir_write_str_lit(f, "DW_TAG_structure_type");   return;
		case irDebugBasicEncoding_union_type:       ir_write_str_lit(f, "DW_TAG_union_type");       return;
		}
	}

	GB_PANIC("Unreachable");
}

void ir_print_exact_value(irFileBuffer *f, irModule *m, ExactValue value, Type *type);

void ir_print_compound_element(irFileBuffer *f, irModule *m, ExactValue v, Type *elem_type) {
	ir_print_type(f, m, elem_type);
	ir_write_byte(f, ' ');

	if (v.kind == ExactValue_Invalid || !elem_type_can_be_constant(elem_type)) {
		ir_fprintf(f, "zeroinitializer");
	} else {
		ir_print_exact_value(f, m, v, elem_type);
	}
}

void ir_print_exact_value(irFileBuffer *f, irModule *m, ExactValue value, Type *type) {
	Type *original_type = type;
	type = core_type(type);
	value = convert_exact_value_for_type(value, type);

	// NOTE(bill): Is this correct? Does this handle all cases regarding arrays?
	if (is_type_array(type) && value.kind == ExactValue_String && !is_type_u8(core_array_type(type))) {
		i64 count  = type->Array.count;
		Type *elem = type->Array.elem;
		ir_write_byte(f, '[');

		for (i64 i = 0; i < count; i++) {
			if (i > 0) ir_write_str_lit(f, ", ");
			ir_print_type(f, m, elem);
			ir_write_byte(f, ' ');
			ir_print_exact_value(f, m, value, elem);
		}

		ir_write_byte(f, ']');
		return;
	} else if (is_type_array(type) &&
	    value.kind != ExactValue_Invalid &&
	    value.kind != ExactValue_String &&
	    value.kind != ExactValue_Compound) {
		i64 count  = type->Array.count;
		Type *elem = type->Array.elem;
		ir_write_byte(f, '[');

		for (i64 i = 0; i < count; i++) {
			if (i > 0) ir_write_str_lit(f, ", ");
			ir_print_type(f, m, elem);
			ir_write_byte(f, ' ');
			ir_print_exact_value(f, m, value, elem);
		}

		ir_write_byte(f, ']');
		return;
	}

	switch (value.kind) {
	case ExactValue_Bool:
		if (value.value_bool) {
			ir_write_string(f, are_types_identical(type, t_llvm_bool) ? str_lit("true") : str_lit("1"));
		} else {
			ir_write_string(f, are_types_identical(type, t_llvm_bool) ? str_lit("false") : str_lit("0"));
		}
		break;
	case ExactValue_String: {
		String str = value.value_string;
		Type *t = core_type(type);
		if (str.len == 0 && !is_type_cstring(t)) {
			ir_write_str_lit(f, "zeroinitializer");
			break;
		}
		if (is_type_u8_slice(type)) {
			irValue *str_array = ir_add_global_string_array(m, str);
			ir_write_str_lit(f, "{i8* getelementptr inbounds (");
			ir_print_type(f, m, str_array->Global.entity->type);
			ir_write_str_lit(f, ", ");
			ir_print_type(f, m, str_array->Global.entity->type);
			ir_write_str_lit(f, "* ");
			ir_print_encoded_global(f, str_array->Global.entity->token.string, false);
			ir_write_str_lit(f, ", ");
			ir_print_type(f, m, t_i32);
			ir_write_str_lit(f, " 0, i32 0), ");
			ir_print_type(f, m, t_int);
			ir_fprintf(f, " %lld}", cast(i64)str.len);
		} else if (!is_type_string(type)) {
			GB_ASSERT(is_type_array(type));
			ir_write_str_lit(f, "c\"");
			ir_print_escape_string(f, str, false, false);
			ir_write_str_lit(f, "\\00\"");
		} else if (is_type_cstring(t)) {
			// HACK NOTE(bill): This is a hack but it works because strings are created at the very end
			// of the .ll file
			irValue *str_array = ir_add_global_string_array(m, str);
			ir_write_str_lit(f, "getelementptr inbounds (");
			ir_print_type(f, m, str_array->Global.entity->type);
			ir_write_str_lit(f, ", ");
			ir_print_type(f, m, str_array->Global.entity->type);
			ir_write_str_lit(f, "* ");
			ir_print_encoded_global(f, str_array->Global.entity->token.string, false);
			ir_write_str_lit(f, ", ");
			ir_print_type(f, m, t_i32);
			ir_write_str_lit(f, " 0, i32 0)");
		}else {
			// HACK NOTE(bill): This is a hack but it works because strings are created at the very end
			// of the .ll file
			irValue *str_array = ir_add_global_string_array(m, str);
			ir_write_str_lit(f, "{i8* getelementptr inbounds (");
			ir_print_type(f, m, str_array->Global.entity->type);
			ir_write_str_lit(f, ", ");
			ir_print_type(f, m, str_array->Global.entity->type);
			ir_write_str_lit(f, "* ");
			ir_print_encoded_global(f, str_array->Global.entity->token.string, false);
			ir_write_str_lit(f, ", ");
			ir_print_type(f, m, t_i32);
			ir_write_str_lit(f, " 0, i32 0), ");
			ir_print_type(f, m, t_int);
			ir_fprintf(f, " %lld}", cast(i64)str.len);
		}
		break;
	}
	case ExactValue_Integer: {
		if (is_type_pointer(type)) {
			if (big_int_is_zero(&value.value_integer)) {
				ir_write_str_lit(f, "null");
			} else {
				ir_write_str_lit(f, "inttoptr (");
				ir_print_type(f, m, t_int);
				ir_write_byte(f, ' ');
				ir_write_big_int(f, value.value_integer, type, is_type_different_to_arch_endianness(type));
				ir_write_str_lit(f, " to ");
				ir_print_type(f, m, t_rawptr);
				ir_write_str_lit(f, ")");
			}
		} else {
			ir_write_big_int(f, value.value_integer, type, is_type_different_to_arch_endianness(type));
		}
		break;
	}
	case ExactValue_Float: {
		GB_ASSERT_MSG(is_type_float(type), "%s", type_to_string(type));
		type = core_type(type);
		u64 u_64 = bit_cast<u64>(value.value_float);
		u32 u_32 = bit_cast<u32>(cast(f32)value.value_float);


	#if 0
		switch (type->Basic.kind) {
		case Basic_f32:
			// IMPORTANT NOTE(bill): LLVM requires all floating point constants to be
			// a 64 bit number if bits_of(float type) <= 64.
			// https://groups.google.com/forum/#!topic/llvm-dev/IlqV3TbSk6M
			// 64 bit mantissa: 52 bits ==> 52-52 ==  0
			// 32 bit mantissa: 23 bits ==> 52-23 == 29
			// 16 bit mantissa: 10 bits ==> 52=10 == 42
			// 29 == 52-23
			u >>= 29;
			u <<= 29;
			break;
		// case Basic_f16:
			// u >>= 42;
			// u <<= 42;
			// break;
		}

		switch (type->Basic.kind) {
		case 0: break;
		default:
			ir_fprintf(f, "0x%016llx", u);
			break;
		}
	#else
		switch (type->Basic.kind) {
		case Basic_f32:
			ir_fprintf(f, "bitcast (i32 %u to float)", u_32);
			break;
		case Basic_f32le:
			if (build_context.endian_kind != TargetEndian_Little) {
				u_32 = gb_endian_swap32(u_32);
			}
			ir_fprintf(f, "bitcast (i32 %u to float)", u_32);
			break;
		case Basic_f32be:
			if (build_context.endian_kind != TargetEndian_Big) {
				u_32 = gb_endian_swap32(u_32);
			}
			ir_fprintf(f, "bitcast (i32 %u to float)", u_32);
			break;

		case Basic_f64:
			ir_fprintf(f, "0x%016llx", u_64);
			break;
		case Basic_f64le:
			if (build_context.endian_kind != TargetEndian_Little) {
				u_64 = gb_endian_swap64(u_64);
			}
			ir_fprintf(f, "0x%016llx", u_64);
			break;
		case Basic_f64be:
			if (build_context.endian_kind != TargetEndian_Big) {
				u_64 = gb_endian_swap64(u_64);
			}
			ir_fprintf(f, "0x%016llx", u_64);
			break;

		default:
			ir_fprintf(f, "0x%016llx", u_64);
			break;
		}
	#endif
		break;
	}
	case ExactValue_Complex: {
		// xy/ri format
		type = core_type(type);
		GB_ASSERT_MSG(is_type_complex(type), "%s", type_to_string(type));
		Type *ft = base_complex_elem_type(type);
		ir_write_byte(f, ' ');
		ir_write_byte(f, '{');
		ir_print_type(f, m, ft); ir_write_byte(f, ' ');
		ir_print_exact_value(f, m, exact_value_float(value.value_complex.real), ft);
		ir_write_str_lit(f, ", "); ir_print_type(f, m, ft); ir_write_byte(f, ' ');
		ir_print_exact_value(f, m, exact_value_float(value.value_complex.imag), ft);
		ir_write_byte(f, '}');
		break;
	}

	case ExactValue_Quaternion: {
		// xyzw/ijkr format
		type = core_type(type);
		GB_ASSERT_MSG(is_type_quaternion(type), "%s", type_to_string(type));
		Type *ft = base_complex_elem_type(type);
		ir_write_byte(f, ' ');
		ir_write_byte(f, '{');
		ir_print_type(f, m, ft); ir_write_byte(f, ' ');
		ir_print_exact_value(f, m, exact_value_float(value.value_quaternion.imag), ft);
		ir_write_str_lit(f, ", "); ir_print_type(f, m, ft); ir_write_byte(f, ' ');
		ir_print_exact_value(f, m, exact_value_float(value.value_quaternion.jmag), ft);
		ir_write_str_lit(f, ", "); ir_print_type(f, m, ft); ir_write_byte(f, ' ');
		ir_print_exact_value(f, m, exact_value_float(value.value_quaternion.kmag), ft);
		ir_write_str_lit(f, ", "); ir_print_type(f, m, ft); ir_write_byte(f, ' ');
		ir_print_exact_value(f, m, exact_value_float(value.value_quaternion.real), ft);
		ir_write_byte(f, '}');
		break;
	}

	case ExactValue_Pointer:
		if (value.value_pointer == 0) {
			if (is_type_typeid(type)) {
				ir_write_str_lit(f, "0");
			} else {
				ir_write_str_lit(f, "null");
			}
		} else {
			ir_write_str_lit(f, "inttoptr (");
			ir_print_type(f, m, t_int);
			ir_fprintf(f, " %llu to ", cast(u64)cast(uintptr)value.value_pointer);
			ir_print_type(f, m, t_rawptr);
			ir_write_byte(f, ')');
		}
		break;
	case ExactValue_Compound: {
		type = base_type(type);
		if (is_type_slice(type)) {
			irValue *s = ir_add_module_constant(m, type, value);
			ir_print_value(f, m, s, type);
		} else if (is_type_array(type)) {
			ast_node(cl, CompoundLit, value.value_compound);

			Type *elem_type = type->Array.elem;
			isize elem_count = cl->elems.count;
			if (elem_count == 0) {
				ir_write_str_lit(f, "zeroinitializer");
				break;
			}
			if (cl->elems[0]->kind == Ast_FieldValue) {
				// TODO(bill): This is O(N*M) and will be quite slow; it should probably be sorted before hand
				ir_write_byte(f, '[');
				for (i64 i = 0; i < type->Array.count; i++) {
					if (i > 0) ir_write_str_lit(f, ", ");

					bool found = false;

					for (isize j = 0; j < elem_count; j++) {
						Ast *elem = cl->elems[j];
						ast_node(fv, FieldValue, elem);
						if (is_ast_range(fv->field)) {
							ast_node(ie, BinaryExpr, fv->field);
							TypeAndValue lo_tav = ie->left->tav;
							TypeAndValue hi_tav = ie->right->tav;
							GB_ASSERT(lo_tav.mode == Addressing_Constant);
							GB_ASSERT(hi_tav.mode == Addressing_Constant);

							TokenKind op = ie->op.kind;
							i64 lo = exact_value_to_i64(lo_tav.value);
							i64 hi = exact_value_to_i64(hi_tav.value);
							if (op == Token_Ellipsis) {
								hi += 1;
							}
							if (lo == i) {
								TypeAndValue tav = fv->value->tav;
								if (tav.mode != Addressing_Constant) {
									break;
								}
								for (i64 k = lo; k < hi; k++) {
									if (k > lo) ir_write_str_lit(f, ", ");

									ir_print_compound_element(f, m, tav.value, elem_type);
								}

								found = true;
								i += (hi-lo-1);
								break;
							}
						} else {
							TypeAndValue index_tav = fv->field->tav;
							GB_ASSERT(index_tav.mode == Addressing_Constant);
							i64 index = exact_value_to_i64(index_tav.value);
							if (index == i) {
								TypeAndValue tav = fv->value->tav;
								if (tav.mode != Addressing_Constant) {
									break;
								}
								ir_print_compound_element(f, m, tav.value, elem_type);
								found = true;
								break;
							}
						}
					}

					if (!found) {
						ir_print_type(f, m, elem_type);
						ir_write_byte(f, ' ');
						ir_write_str_lit(f, "zeroinitializer");
					}
				}
				ir_write_byte(f, ']');
			} else {
				GB_ASSERT_MSG(elem_count == type->Array.count, "%td != %td", elem_count, type->Array.count);

				ir_write_byte(f, '[');

				for (isize i = 0; i < elem_count; i++) {
					if (i > 0) ir_write_str_lit(f, ", ");
					TypeAndValue tav = cl->elems[i]->tav;
					GB_ASSERT(tav.mode != Addressing_Invalid);
					ir_print_compound_element(f, m, tav.value, elem_type);
				}
				for (isize i = elem_count; i < type->Array.count; i++) {
					if (i >= elem_count) ir_write_str_lit(f, ", ");
					ir_print_compound_element(f, m, empty_exact_value, elem_type);
				}

				ir_write_byte(f, ']');
			}
		} else if (is_type_enumerated_array(type)) {
			ast_node(cl, CompoundLit, value.value_compound);

			Type *elem_type = type->EnumeratedArray.elem;
			isize elem_count = cl->elems.count;
			if (elem_count == 0) {
				ir_write_str_lit(f, "zeroinitializer");
				break;
			}
			if (cl->elems[0]->kind == Ast_FieldValue) {
				// TODO(bill): This is O(N*M) and will be quite slow; it should probably be sorted before hand
				ir_write_byte(f, '[');

				i64 total_lo = exact_value_to_i64(type->EnumeratedArray.min_value);
				i64 total_hi = exact_value_to_i64(type->EnumeratedArray.max_value);

				for (i64 i = total_lo; i <= total_hi; i++) {
					if (i > total_lo) ir_write_str_lit(f, ", ");

					bool found = false;

					for (isize j = 0; j < elem_count; j++) {
						Ast *elem = cl->elems[j];
						ast_node(fv, FieldValue, elem);
						if (is_ast_range(fv->field)) {
							ast_node(ie, BinaryExpr, fv->field);
							TypeAndValue lo_tav = ie->left->tav;
							TypeAndValue hi_tav = ie->right->tav;
							GB_ASSERT(lo_tav.mode == Addressing_Constant);
							GB_ASSERT(hi_tav.mode == Addressing_Constant);

							TokenKind op = ie->op.kind;
							i64 lo = exact_value_to_i64(lo_tav.value);
							i64 hi = exact_value_to_i64(hi_tav.value);
							if (op == Token_Ellipsis) {
								hi += 1;
							}
							if (lo == i) {
								TypeAndValue tav = fv->value->tav;
								if (tav.mode != Addressing_Constant) {
									break;
								}
								for (i64 k = lo; k < hi; k++) {
									if (k > lo) ir_write_str_lit(f, ", ");

									ir_print_compound_element(f, m, tav.value, elem_type);
								}

								found = true;
								i += (hi-lo-1);
								break;
							}
						} else {
							TypeAndValue index_tav = fv->field->tav;
							GB_ASSERT(index_tav.mode == Addressing_Constant);
							i64 index = exact_value_to_i64(index_tav.value);
							if (index == i) {
								TypeAndValue tav = fv->value->tav;
								if (tav.mode != Addressing_Constant) {
									break;
								}
								ir_print_compound_element(f, m, tav.value, elem_type);
								found = true;
								break;
							}
						}
					}

					if (!found) {
						ir_print_type(f, m, elem_type);
						ir_write_byte(f, ' ');
						ir_write_str_lit(f, "zeroinitializer");
					}
				}
				ir_write_byte(f, ']');
			} else {
				GB_ASSERT_MSG(elem_count == type->EnumeratedArray.count, "%td != %td", elem_count, type->EnumeratedArray.count);

				ir_write_byte(f, '[');

				for (isize i = 0; i < elem_count; i++) {
					if (i > 0) ir_write_str_lit(f, ", ");
					TypeAndValue tav = cl->elems[i]->tav;
					GB_ASSERT(tav.mode != Addressing_Invalid);
					ir_print_compound_element(f, m, tav.value, elem_type);
				}
				for (isize i = elem_count; i < type->EnumeratedArray.count; i++) {
					if (i >= elem_count) ir_write_str_lit(f, ", ");
					ir_print_compound_element(f, m, empty_exact_value, elem_type);
				}

				ir_write_byte(f, ']');
			}
		} else if (is_type_simd_vector(type)) {
			ast_node(cl, CompoundLit, value.value_compound);

			Type *elem_type = type->SimdVector.elem;
			isize elem_count = cl->elems.count;
			if (elem_count == 0) {
				ir_write_str_lit(f, "zeroinitializer");
				break;
			}
			GB_ASSERT_MSG(elem_count == type->SimdVector.count, "%td != %td", elem_count, type->SimdVector.count);

			ir_write_byte(f, '<');

			for (isize i = 0; i < elem_count; i++) {
				if (i > 0) ir_write_str_lit(f, ", ");
				TypeAndValue tav = cl->elems[i]->tav;
				GB_ASSERT(tav.mode != Addressing_Invalid);
				ir_print_compound_element(f, m, tav.value, elem_type);
			}
			for (isize i = elem_count; i < type->SimdVector.count; i++) {
				if (i >= elem_count) ir_write_str_lit(f, ", ");
				ir_print_compound_element(f, m, empty_exact_value, elem_type);
			}

			ir_write_byte(f, '>');
		} else if (is_type_struct(type)) {
			gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&m->tmp_arena);
			defer (gb_temp_arena_memory_end(tmp));

			ast_node(cl, CompoundLit, value.value_compound);

			if (cl->elems.count == 0) {
				ir_write_str_lit(f, "zeroinitializer");
				break;
			}

			String tstr = make_string_c(type_to_string(original_type));

			isize value_count = type->Struct.fields.count;
			ExactValue *values = gb_alloc_array(m->tmp_allocator, ExactValue, value_count);
			bool *visited = gb_alloc_array(m->tmp_allocator, bool, value_count);

			if (cl->elems.count > 0) {
				if (cl->elems[0]->kind == Ast_FieldValue) {
					isize elem_count = cl->elems.count;
					for (isize i = 0; i < elem_count; i++) {
						ast_node(fv, FieldValue, cl->elems[i]);
						String name = fv->field->Ident.token.string;

						TypeAndValue tav = fv->value->tav;
						GB_ASSERT(tav.mode != Addressing_Invalid);

						Selection sel = lookup_field(type, name, false);
						Entity *f = type->Struct.fields[sel.index[0]];

						values[f->Variable.field_src_index] = tav.value;
						visited[f->Variable.field_src_index] = true;
					}
				} else {
					for_array(i, cl->elems) {
						Entity *f = type->Struct.fields[i];
						TypeAndValue tav = cl->elems[i]->tav;
						ExactValue val = {};
						if (tav.mode != Addressing_Invalid) {
							val = tav.value;
						}
						values[f->Variable.field_src_index]  = val;
						visited[f->Variable.field_src_index] = true;
					}
				}
			}

			if (type->Struct.is_packed) ir_write_byte(f, '<');
			ir_write_byte(f, '{');
			if (type->Struct.custom_align > 0) {
				ir_print_alignment_prefix_hack(f, cast(i64)type->Struct.custom_align);
				ir_write_str_lit(f, " zeroinitializer");
				if (value_count > 0) {
					ir_write_string(f, str_lit(", "));
				}
			}


			for (isize i = 0; i < value_count; i++) {
				if (i > 0) ir_write_string(f, str_lit(", "));
				Entity *e = type->Struct.fields[i];
				GB_ASSERT(e->kind == Entity_Variable);
				ir_print_compound_element(f, m, values[e->Variable.field_src_index], e->type);
			}


			ir_write_byte(f, '}');
			if (type->Struct.is_packed) ir_write_byte(f, '>');
		} else if (is_type_bit_set(type)) {
			ast_node(cl, CompoundLit, value.value_compound);
			if (cl->elems.count == 0) {
				ir_write_str_lit(f, "zeroinitializer");
				break;
			}

			i64 sz = type_size_of(type);
			if (sz == 0) {
				ir_write_str_lit(f, "zeroinitializer");
				break;
			}

			u64 bits = 0;
			for_array(i, cl->elems) {
				Ast *e = cl->elems[i];
				GB_ASSERT(e->kind != Ast_FieldValue);

				TypeAndValue tav = e->tav;
				if (tav.mode != Addressing_Constant) {
					continue;
				}
				GB_ASSERT(tav.value.kind == ExactValue_Integer);
				i64 v = big_int_to_i64(&tav.value.value_integer);
				i64 lower = type->BitSet.lower;
				bits |= 1ull<<cast(u64)(v-lower);
			}
			if (is_type_different_to_arch_endianness(type)) {
				i64 size = type_size_of(type);
				switch (size) {
				case 2: bits = cast(u64)gb_endian_swap16(cast(u16)bits); break;
				case 4: bits = cast(u64)gb_endian_swap32(cast(u32)bits); break;
				case 8: bits = cast(u64)gb_endian_swap64(cast(u64)bits); break;
				}
			}
			ir_write_u64(f, bits);
		} else {
			ir_write_str_lit(f, "zeroinitializer");
		}

		break;
	}
	case ExactValue_Procedure: {
		irValue **found = nullptr;
		Ast *expr = unparen_expr(value.value_procedure);
		GB_ASSERT(expr != nullptr);

		if (expr->kind == Ast_ProcLit) {
			found = map_get(&m->anonymous_proc_lits, hash_pointer(expr));
		} else {
			Entity *e = strip_entity_wrapping(expr);
			GB_ASSERT(e != nullptr);
			found = map_get(&m->values, hash_entity(e));
		}
		GB_ASSERT_MSG(found != nullptr, "%s", expr_to_string(expr));
		irValue *val = *found;
		ir_print_value(f, m, val, type);
		break;
	}
	default:
		ir_write_str_lit(f, "zeroinitializer");
		break;
	}
}

void ir_print_block_name(irFileBuffer *f, irBlock *b) {
	if (b != nullptr) {
		ir_print_escape_string(f, b->label, false, false);
		ir_fprintf(f, "-%td", b->index);
	} else {
		ir_write_str_lit(f, "<INVALID-BLOCK>");
	}
}

bool ir_print_is_proc_global(irModule *m, irProcedure *proc) {
	if (proc->entity != nullptr &&
	    proc->entity->kind == Entity_Procedure) {
		if (m->entry_point_entity == proc->entity) {
			// gb_printf("%.*s\n", LIT(proc->entity->token.string));
			// TODO(bill): This may not be needed during windows
			return true;
		}
		if (proc->entity->Procedure.link_name.len > 0) {
		    return true;
		}
	}
	return proc->is_foreign || proc->is_export;
}

void ir_print_value(irFileBuffer *f, irModule *m, irValue *value, Type *type_hint) {
	if (value == nullptr) {
		ir_write_str_lit(f, "!!!nullptr_VALUE");
		return;
	}
	switch (value->kind) {
	default: GB_PANIC("Unknown irValue kind"); break;

	case irValue_Constant:
		ir_print_exact_value(f, m, value->Constant.value, type_hint);
		break;

	case irValue_ConstantSlice: {
		irValueConstantSlice *cs = &value->ConstantSlice;
		if (cs->backing_array == nullptr || cs->count == 0) {
			ir_write_str_lit(f, "zeroinitializer");
		} else {
			Type *at = base_type(type_deref(ir_type(cs->backing_array)));
			Type *et = at->Array.elem;
			ir_write_byte(f, '{');
			ir_print_type(f, m, et);
			ir_write_str_lit(f, "* getelementptr inbounds (");
			ir_print_type(f, m, at);
			ir_write_str_lit(f, ", ");
			ir_print_type(f, m, at);
			ir_write_str_lit(f, "* ");
			ir_print_value(f, m, cs->backing_array, at);
			ir_write_str_lit(f, ", i32 0, i32 0), ");
			ir_print_type(f, m, t_int);
			ir_fprintf(f, " %lld}", cs->count);
		}
		break;
	}

	case irValue_Nil:
		ir_write_str_lit(f, "zeroinitializer");
		break;

	case irValue_Undef:
		ir_write_str_lit(f, "undef");
		break;

	case irValue_TypeName:
		ir_print_encoded_local(f, value->TypeName.name);
		break;
	case irValue_Global: {
		Entity *e = value->Global.entity;
		Scope *scope = e->scope;
		bool in_global_scope = false;
		if (scope != nullptr) {
			in_global_scope = (scope->flags & ScopeFlag_Global) != 0;
		}
		ir_print_encoded_global(f, ir_get_global_name(m, value), in_global_scope);
		break;
	}
	case irValue_Param:
		if (value->Param.index >= 0) {
			ir_fprintf(f, "%%_.%d", value->Param.index);
		} else {
			ir_print_encoded_local(f, value->Param.entity->token.string);
		}
		break;
	case irValue_SourceCodeLocation: {
		irValue *file      = value->SourceCodeLocation.file;
		irValue *line      = value->SourceCodeLocation.line;
		irValue *column    = value->SourceCodeLocation.column;
		irValue *procedure = value->SourceCodeLocation.procedure;
		u64      hash      = value->SourceCodeLocation.hash;

		ir_write_byte(f, '{');
		ir_print_type(f, m, t_string); ir_write_byte(f, ' '); ir_print_value(f, m, file, t_string);
		ir_write_string(f, str_lit(", "));
		ir_print_type(f, m, t_int);    ir_write_byte(f, ' '); ir_print_value(f, m, line, t_int);
		ir_write_string(f, str_lit(", "));
		ir_print_type(f, m, t_int);    ir_write_byte(f, ' '); ir_print_value(f, m, column, t_int);
		ir_write_string(f, str_lit(", "));
		ir_print_type(f, m, t_string); ir_write_byte(f, ' '); ir_print_value(f, m, procedure, t_string);
		ir_write_string(f, str_lit(", "));
		ir_print_type(f, m, t_u64);    ir_write_byte(f, ' '); ir_write_u64(f, hash);
		ir_write_byte(f, '}');
		break;
	}
	case irValue_Proc:
		ir_print_encoded_global(f, value->Proc.name, ir_print_is_proc_global(m, &value->Proc));
		break;
	case irValue_Instr:
		ir_fprintf(f, "%%%d", value->index);
		break;
	}
}

void ir_print_calling_convention(irFileBuffer *f, irModule *m, ProcCallingConvention cc) {
	switch (cc) {
	case ProcCC_Odin:        ir_write_str_lit(f, "");       break;
	case ProcCC_Contextless: ir_write_str_lit(f, "");       break;
	case ProcCC_Pure:        ir_write_str_lit(f, "");       break;
	// case ProcCC_CDecl:       ir_write_str_lit(f, "ccc ");   break;
	case ProcCC_CDecl:       ir_write_str_lit(f, "");   break;
	case ProcCC_StdCall:     ir_write_str_lit(f, "cc 64 "); break;
	case ProcCC_FastCall:    ir_write_str_lit(f, "cc 65 "); break;
	case ProcCC_None:        ir_write_str_lit(f, "");       break;
	case ProcCC_PureNone:    ir_write_str_lit(f, "");       break;
	default: GB_PANIC("unknown calling convention: %d", cc);
	}
}

void ir_print_context_parameter_prefix(irFileBuffer *f, irModule *m) {
	ir_print_type(f, m, t_context_ptr);
	// ir_write_str_lit(f, " noalias nonnull nocapture inreg ");
	ir_write_str_lit(f, " noalias nonnull nocapture ");
}

void ir_print_instr(irFileBuffer *f, irModule *m, irValue *value) {
	GB_ASSERT(value->kind == irValue_Instr);
	irInstr *instr = &value->Instr;

	ir_write_byte(f, '\t');
	defer (ir_write_byte(f, '\n'));

	switch (instr->kind) {
	default: {
		GB_PANIC("<unknown instr> %d\n", instr->kind);
		ir_fprintf(f, "; <unknown instr> %d\n", instr->kind);
		break;
	}

	case irInstr_StartupRuntime: {
		ir_write_str_lit(f, "call void ");
		ir_print_encoded_global(f, str_lit(IR_STARTUP_RUNTIME_PROC_NAME), false);
		ir_write_str_lit(f, "()");
		break;
	}

	case irInstr_Comment:
		ir_write_str_lit(f, "; ");
		ir_write_string(f, instr->Comment.text);
		break;

	case irInstr_Local: {
		Type *type = instr->Local.entity->type;
		i64 align = instr->Local.alignment;
		if (align <= 0) {
			align = type_align_of(type);
		}
		ir_fprintf(f, "%%%d = alloca ", value->index);
		ir_print_type(f, m, type);
		ir_fprintf(f, ", align %lld", align);
		break;
	}

	case irInstr_ZeroInit: {
		Type *type = type_deref(ir_type(instr->ZeroInit.address));
		ir_write_str_lit(f, "; ZeroInit\n\t");
		ir_write_str_lit(f, "store ");
		ir_print_type(f, m, type);
		ir_write_byte(f, ' ');
		ir_print_exact_value(f, m, empty_exact_value, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type);
		ir_fprintf(f, "* %%%d, align 1", instr->ZeroInit.address->index);
		// ir_fprintf(f, "* %%%d", instr->ZeroInit.address->index);
		break;
	}

	case irInstr_Store: {
		Type *type = type_deref(ir_type(instr->Store.address));
		ir_write_str_lit(f, "store ");
		if (instr->Store.is_volatile) {
			ir_write_str_lit(f, "volatile ");
		}
		ir_print_type(f, m, type);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->Store.value, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type);
		ir_write_str_lit(f, "* ");
		ir_print_value(f, m, instr->Store.address, type);
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_Load: {
		Type *type = instr->Load.type;
		ir_fprintf(f, "%%%d = load ", value->index);
		ir_print_type(f, m, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type);
		ir_write_str_lit(f, "* ");
		ir_print_value(f, m, instr->Load.address, type);
		if (instr->Load.custom_align > 0) {
			ir_fprintf(f, ", align %lld", instr->Load.custom_align);
		} else {
			ir_fprintf(f, ", align %lld", type_align_of(type));
		}
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_InlineCode:
		{
			switch (instr->InlineCode.id) {
			case BuiltinProc_alloca:
				ir_fprintf(f, "%%%d = ", value->index);
				ir_write_str_lit(f, "alloca i8, ");
				ir_print_type(f, m, ir_type(instr->InlineCode.operands[0]));
				ir_write_str_lit(f, " ");
				ir_print_value(f, m, instr->InlineCode.operands[0], ir_type(instr->InlineCode.operands[0]));
				ir_write_str_lit(f, ", align ");
				ir_print_value(f, m, instr->InlineCode.operands[1], t_i32);
				break;

			case BuiltinProc_cpu_relax:
#if defined(GB_CPU_RISCV)
				ir_write_str_lit(f, "call void asm sideeffect \"nop\", \"\"()");
#else
				ir_write_str_lit(f, "call void asm sideeffect \"pause\", \"\"()");
#endif
				break;
			default: GB_PANIC("Unknown inline code %d", instr->InlineCode.id); break;
			}
		}
		break;


	case irInstr_AtomicFence:
		ir_write_str_lit(f, "fence ");
		switch (instr->AtomicFence.id) {
		case BuiltinProc_atomic_fence:        ir_write_str_lit(f, "seq_cst"); break;
		case BuiltinProc_atomic_fence_acq:    ir_write_str_lit(f, "acquire"); break;
		case BuiltinProc_atomic_fence_rel:    ir_write_str_lit(f, "release"); break;
		case BuiltinProc_atomic_fence_acqrel: ir_write_str_lit(f, "acq_rel"); break;
		default: GB_PANIC("Unknown atomic fence"); break;
		}
		break;

	case irInstr_AtomicStore: {
		Type *type = type_deref(ir_type(instr->AtomicStore.address));
		ir_write_str_lit(f, "store atomic ");
		ir_print_type(f, m, type);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->AtomicStore.value, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type);
		ir_write_str_lit(f, "* ");
		ir_print_value(f, m, instr->AtomicStore.address, type);

		switch (instr->AtomicStore.id) {
		case BuiltinProc_atomic_store:           ir_write_str_lit(f, " seq_cst ");  break;
		case BuiltinProc_atomic_store_rel:       ir_write_str_lit(f, " release");   break;
		case BuiltinProc_atomic_store_relaxed:   ir_write_str_lit(f, " monotonic"); break;
		case BuiltinProc_atomic_store_unordered: ir_write_str_lit(f, " unordered"); break;
		default: GB_PANIC("Unknown atomic store"); break;
		}

		ir_fprintf(f, ", align %lld", type_align_of(type));

		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_AtomicLoad: {
		Type *type = instr->AtomicLoad.type;
		ir_fprintf(f, "%%%d = load atomic ", value->index);
		ir_print_type(f, m, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type);
		ir_write_str_lit(f, "* ");
		ir_print_value(f, m, instr->AtomicLoad.address, type);

		switch (instr->AtomicLoad.id) {
		case BuiltinProc_atomic_load:           ir_fprintf(f, " seq_cst");   break;
		case BuiltinProc_atomic_load_acq:       ir_fprintf(f, " acquire");   break;
		case BuiltinProc_atomic_load_relaxed:   ir_fprintf(f, " monotonic"); break;
		case BuiltinProc_atomic_load_unordered: ir_fprintf(f, " unordered"); break;
		default: GB_PANIC("Unknown atomic load"); break;
		}

		ir_fprintf(f, ", align %lld", type_align_of(type));
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_AtomicRmw: {
		String operation = {};
		String ordering = {};
		switch (instr->AtomicRmw.id) {
		case BuiltinProc_atomic_add:
		case BuiltinProc_atomic_add_acq:
		case BuiltinProc_atomic_add_rel:
		case BuiltinProc_atomic_add_acqrel:
		case BuiltinProc_atomic_add_relaxed:
			operation = str_lit("add");
			break;
		case BuiltinProc_atomic_sub:
		case BuiltinProc_atomic_sub_acq:
		case BuiltinProc_atomic_sub_rel:
		case BuiltinProc_atomic_sub_acqrel:
		case BuiltinProc_atomic_sub_relaxed:
			operation = str_lit("sub");
			break;
		case BuiltinProc_atomic_and:
		case BuiltinProc_atomic_and_acq:
		case BuiltinProc_atomic_and_rel:
		case BuiltinProc_atomic_and_acqrel:
		case BuiltinProc_atomic_and_relaxed:
			operation = str_lit("and");
			break;
		case BuiltinProc_atomic_nand:
		case BuiltinProc_atomic_nand_acq:
		case BuiltinProc_atomic_nand_rel:
		case BuiltinProc_atomic_nand_acqrel:
		case BuiltinProc_atomic_nand_relaxed:
			operation = str_lit("nand");
			break;
		case BuiltinProc_atomic_or:
		case BuiltinProc_atomic_or_acq:
		case BuiltinProc_atomic_or_rel:
		case BuiltinProc_atomic_or_acqrel:
		case BuiltinProc_atomic_or_relaxed:
			operation = str_lit("or");
			break;
		case BuiltinProc_atomic_xor:
		case BuiltinProc_atomic_xor_acq:
		case BuiltinProc_atomic_xor_rel:
		case BuiltinProc_atomic_xor_acqrel:
		case BuiltinProc_atomic_xor_relaxed:
			operation = str_lit("xor");
			break;
		case BuiltinProc_atomic_xchg:
		case BuiltinProc_atomic_xchg_acq:
		case BuiltinProc_atomic_xchg_rel:
		case BuiltinProc_atomic_xchg_acqrel:
		case BuiltinProc_atomic_xchg_relaxed:
			operation = str_lit("xchg");
			break;
		}

		switch (instr->AtomicRmw.id) {
		case BuiltinProc_atomic_add:
		case BuiltinProc_atomic_sub:
		case BuiltinProc_atomic_and:
		case BuiltinProc_atomic_nand:
		case BuiltinProc_atomic_or:
		case BuiltinProc_atomic_xor:
		case BuiltinProc_atomic_xchg:
			ordering = str_lit("seq_cst");
			break;
		case BuiltinProc_atomic_add_acq:
		case BuiltinProc_atomic_sub_acq:
		case BuiltinProc_atomic_and_acq:
		case BuiltinProc_atomic_nand_acq:
		case BuiltinProc_atomic_or_acq:
		case BuiltinProc_atomic_xor_acq:
		case BuiltinProc_atomic_xchg_acq:
			ordering = str_lit("acquire");
			break;
		case BuiltinProc_atomic_add_rel:
		case BuiltinProc_atomic_sub_rel:
		case BuiltinProc_atomic_and_rel:
		case BuiltinProc_atomic_nand_rel:
		case BuiltinProc_atomic_or_rel:
		case BuiltinProc_atomic_xor_rel:
		case BuiltinProc_atomic_xchg_rel:
			ordering = str_lit("release");
			break;
		case BuiltinProc_atomic_add_acqrel:
		case BuiltinProc_atomic_sub_acqrel:
		case BuiltinProc_atomic_and_acqrel:
		case BuiltinProc_atomic_nand_acqrel:
		case BuiltinProc_atomic_or_acqrel:
		case BuiltinProc_atomic_xor_acqrel:
		case BuiltinProc_atomic_xchg_acqrel:
			ordering = str_lit("acq_rel");
			break;
		case BuiltinProc_atomic_add_relaxed:
		case BuiltinProc_atomic_sub_relaxed:
		case BuiltinProc_atomic_and_relaxed:
		case BuiltinProc_atomic_nand_relaxed:
		case BuiltinProc_atomic_or_relaxed:
		case BuiltinProc_atomic_xor_relaxed:
		case BuiltinProc_atomic_xchg_relaxed:
			ordering = str_lit("monotonic");
			break;
		}

		Type *type = type_deref(ir_type(instr->AtomicRmw.address));
		ir_write_str_lit(f, "atomicrmw ");
		ir_write_string(f, operation);
		ir_write_byte(f, ' ');
		ir_print_type(f, m, type);
		ir_write_str_lit(f, "* ");
		ir_print_value(f, m, instr->AtomicRmw.address, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->AtomicRmw.value, type);

		ir_write_byte(f, ' ');
		ir_write_string(f, ordering);

		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_AtomicCxchg: {
		Type *type = type_deref(ir_type(instr->AtomicCxchg.address));
		bool weak = false;
		String success = {};
		String failure = {};

		switch (instr->AtomicCxchg.id) {
		case BuiltinProc_atomic_cxchgweak:
		case BuiltinProc_atomic_cxchgweak_acq:
		case BuiltinProc_atomic_cxchgweak_rel:
		case BuiltinProc_atomic_cxchgweak_acqrel:
		case BuiltinProc_atomic_cxchgweak_relaxed:
		case BuiltinProc_atomic_cxchgweak_failrelaxed:
		case BuiltinProc_atomic_cxchgweak_failacq:
		case BuiltinProc_atomic_cxchgweak_acq_failrelaxed:
		case BuiltinProc_atomic_cxchgweak_acqrel_failrelaxed:
			weak = true;
			break;
		}

		switch (instr->AtomicCxchg.id) {
		case BuiltinProc_atomic_cxchg:
		case BuiltinProc_atomic_cxchgweak:
			success = str_lit("seq_cst");
			failure = str_lit("seq_cst");
			break;
		case BuiltinProc_atomic_cxchg_acq:
		case BuiltinProc_atomic_cxchgweak_acq:
			success = str_lit("acquire");
			failure = str_lit("seq_cst");
			break;
		case BuiltinProc_atomic_cxchg_rel:
		case BuiltinProc_atomic_cxchgweak_rel:
			success = str_lit("release");
			failure = str_lit("seq_cst");
			break;
		case BuiltinProc_atomic_cxchg_acqrel:
		case BuiltinProc_atomic_cxchgweak_acqrel:
			success = str_lit("acq_rel");
			failure = str_lit("seq_cst");
			break;
		case BuiltinProc_atomic_cxchg_relaxed:
		case BuiltinProc_atomic_cxchgweak_relaxed:
			success = str_lit("monotonic");
			failure = str_lit("monotonic");
			break;
		case BuiltinProc_atomic_cxchg_failrelaxed:
		case BuiltinProc_atomic_cxchgweak_failrelaxed:
			success = str_lit("seq_cst");
			failure = str_lit("monotonic");
			break;
		case BuiltinProc_atomic_cxchg_failacq:
		case BuiltinProc_atomic_cxchgweak_failacq:
			success = str_lit("seq_cst");
			failure = str_lit("acquire");
			break;
		case BuiltinProc_atomic_cxchg_acq_failrelaxed:
		case BuiltinProc_atomic_cxchgweak_acq_failrelaxed:
			success = str_lit("acquire");
			failure = str_lit("monotonic");
			break;
		case BuiltinProc_atomic_cxchg_acqrel_failrelaxed:
		case BuiltinProc_atomic_cxchgweak_acqrel_failrelaxed:
			success = str_lit("acq_rel");
			failure = str_lit("monotonic");
			break;
		}

		ir_fprintf(f, "%%%d = cmpxchg ", value->index);
		if (weak) {
			ir_write_str_lit(f, "weak ");
		}
		ir_print_type(f, m, type);
		ir_write_str_lit(f, "* ");
		ir_print_value(f, m, instr->AtomicCxchg.address, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type); ir_write_str_lit(f, " ");
		ir_print_value(f, m, instr->AtomicCxchg.old_value, type);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, type); ir_write_str_lit(f, " ");
		ir_print_value(f, m, instr->AtomicCxchg.new_value, type);
		ir_write_str_lit(f, " ");
		ir_write_string(f, success);
		ir_write_str_lit(f, " ");
		ir_write_string(f, failure);
		break;
	}

	case irInstr_ArrayElementPtr: {
		Type *et = ir_type(instr->ArrayElementPtr.address);
		ir_fprintf(f, "%%%d = getelementptr inbounds ", value->index);

		ir_print_type(f, m, type_deref(et));
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, et);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->ArrayElementPtr.address, et);
		ir_write_str_lit(f, ", i32 0, ");

		irValue *index =instr->ArrayElementPtr.elem_index;
		Type *t = ir_type(index);
		ir_print_type(f, m, t);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, index, t);
		break;
	}

	case irInstr_StructElementPtr: {
		Type *et = ir_type(instr->StructElementPtr.address);
		ir_fprintf(f, "%%%d = getelementptr inbounds ", value->index);
		i32 index = instr->StructElementPtr.elem_index;
		Type *st = base_type(type_deref(et));
		if (is_type_struct(st)) {
			if (st->Struct.custom_align > 0) {
				index += 1;
			}
		} else if (is_type_union(st)) {
			index += 1;
		}

		ir_print_type(f, m, type_deref(et));
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, et);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->StructElementPtr.address, et);
		ir_write_str_lit(f, ", i32 0, ");
		ir_print_type(f, m, t_i32);
		ir_fprintf(f, " %d", index);
		break;
	}

	case irInstr_PtrOffset: {
		Type *pt = ir_type(instr->PtrOffset.address);
		ir_fprintf(f, "%%%d = getelementptr inbounds ", value->index);
		ir_print_type(f, m, type_deref(pt));
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, pt);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->PtrOffset.address, pt);

		irValue *offset = instr->PtrOffset.offset;
		Type *t = ir_type(offset);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, t);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, offset, t);
		break;
	}

	case irInstr_Phi: {
		ir_fprintf(f, "%%%d = phi ", value->index);
		ir_print_type(f, m, instr->Phi.type);
		// ir_fprintf(f, " ", value->index);
		ir_write_byte(f, ' ');

		for (isize i = 0; i < instr->Phi.edges.count; i++) {
			if (i > 0) {
				ir_write_string(f, str_lit(", "));
			}

			irValue *edge = instr->Phi.edges[i];
			irBlock *block = nullptr;
			if (instr->block != nullptr &&
			    i < instr->block->preds.count) {
				block = instr->block->preds[i];
			}

			ir_write_str_lit(f, "[ ");
			ir_print_value(f, m, edge, instr->Phi.type);
			ir_write_str_lit(f, ", %");
			ir_print_block_name(f, block);
			ir_write_str_lit(f, " ]");
		}
		break;
	}

	case irInstr_StructExtractValue: {
		Type *et = ir_type(instr->StructExtractValue.address);
		ir_fprintf(f, "%%%d = extractvalue ", value->index);
		i32 index = instr->StructExtractValue.index;
		Type *st = base_type(et);
		if (is_type_struct(st)) {
			if (st->Struct.custom_align > 0) {
				index += 1;
			}
		} else if (is_type_union(st)) {
			index += 1;
		}


		ir_print_type(f, m, et);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->StructExtractValue.address, et);
		ir_fprintf(f, ", %d", index);
		break;
	}

	case irInstr_UnionTagPtr: {
		Type *et = ir_type(instr->UnionTagPtr.address);

		Type *ut = type_deref(et);
		if (is_type_union_maybe_pointer(ut)) {
			GB_PANIC("union #maybe UnionTagPtr");
		}

		ir_fprintf(f, "%%%d = getelementptr inbounds ", value->index);
		Type *t = base_type(type_deref(et));
		GB_ASSERT(is_type_union(t));

		ir_print_type(f, m, type_deref(et));
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, et);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->UnionTagPtr.address, et);
		ir_write_str_lit(f, ", ");
		ir_print_type(f, m, t_int);
		ir_write_str_lit(f, " 0, ");
		ir_print_type(f, m, t_i32);
		ir_fprintf(f, " 2 ; UnionTagPtr");
		break;
	}

	case irInstr_UnionTagValue: {
		Type *et = ir_type(instr->UnionTagValue.address);
		Type *t = base_type(et);

		if (is_type_union_maybe_pointer(t)) {
			GB_PANIC("union #maybe UnionTagValue");
		}

		ir_fprintf(f, "%%%d = extractvalue ", value->index);
		GB_ASSERT(is_type_union(t));


		ir_print_type(f, m, et);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->UnionTagValue.address, et);
		ir_fprintf(f, ", 2 ; UnionTagValue");
		break;
	}

	case irInstr_Jump: {
		ir_write_str_lit(f, "br label %");
		ir_print_block_name(f, instr->Jump.block);
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_If: {
		ir_write_str_lit(f, "br i1");
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->If.cond, t_bool);
		ir_write_str_lit(f, ", ");
		ir_write_str_lit(f, "label %");   ir_print_block_name(f, instr->If.true_block);
		ir_write_str_lit(f, ", label %"); ir_print_block_name(f, instr->If.false_block);
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_Return: {
		irInstrReturn *ret = &instr->Return;
		ir_write_str_lit(f, "ret ");
		if (ret->value == nullptr) {
			ir_write_str_lit(f, "void");
		} else {
			Type *t = ir_type(ret->value);
			ir_print_type(f, m, t);
			ir_write_byte(f, ' ');
			ir_print_value(f, m, ret->value, t);
		}
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_Conv: {
		irInstrConv *c = &instr->Conv;
		if (c->kind == irConv_byteswap) {
			int sz = cast(int)(8*type_size_of(c->from));
			ir_fprintf(f, "%%%d = call i%d @llvm.bswap.i%d(", value->index, sz, sz);
			ir_print_type(f, m, c->from);
			ir_write_byte(f, ' ');
			ir_print_value(f, m, c->value, c->from);
			ir_write_byte(f, ')');
			ir_print_debug_location(f, m, value);
		} else {
			ir_fprintf(f, "%%%d = ", value->index);
			ir_write_string(f, ir_conv_strings[c->kind]);
			ir_write_byte(f, ' ');
			ir_print_type(f, m, c->from);
			ir_write_byte(f, ' ');
			ir_print_value(f, m, c->value, c->from);
			ir_write_str_lit(f, " to ");
			ir_print_type(f, m, c->to);
			ir_print_debug_location(f, m, value);
		}
		break;
	}

	case irInstr_Unreachable: {
		ir_write_str_lit(f, "unreachable");
		break;
	}

	case irInstr_UnaryOp: {
		irInstrUnaryOp *uo = &value->Instr.UnaryOp;
		Type *type =  base_type(ir_type(uo->expr));
		Type *elem_type = type;

		ir_fprintf(f, "%%%d = ", value->index);
		switch (uo->op) {
		case Token_Sub:
			if (is_type_float(elem_type)) {
				ir_write_str_lit(f, "fsub");
			} else {
				ir_write_str_lit(f, "sub");
			}
			break;
		case Token_Xor:
		case Token_Not:
			GB_ASSERT(is_type_integer(type) || is_type_boolean(type) || is_type_bit_set(type));
			ir_write_str_lit(f, "xor");
			break;
		default:
			GB_PANIC("Unknown unary operator");
			break;
		}

		ir_write_byte(f, ' ');
		ir_print_type(f, m, type);
		ir_write_byte(f, ' ');
		switch (uo->op) {
		case Token_Sub:
			if (is_type_float(elem_type)) {
				ir_print_exact_value(f, m, exact_value_float(0), elem_type);
			} else {
				ir_write_byte(f, '0');
			}
			break;
		case Token_Xor:
		case Token_Not:
			GB_ASSERT(is_type_integer(type) || is_type_boolean(type) || is_type_bit_set(type));
			ir_write_str_lit(f, "-1");
			break;
		}
		ir_write_str_lit(f, ", ");
		ir_print_value(f, m, uo->expr, type);
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_BinaryOp: {
		irInstrBinaryOp *bo = &value->Instr.BinaryOp;
		Type *type = base_type(ir_type(bo->left));
		Type *elem_type = base_array_type(type);

		ir_fprintf(f, "%%%d = ", value->index);

		if (gb_is_between(bo->op, Token__ComparisonBegin+1, Token__ComparisonEnd-1)) {
			if (is_type_string(elem_type)) {
				GB_PANIC("Unhandled string type");
			} else if (is_type_float(elem_type)) {
				ir_write_str_lit(f, "fcmp ");
				switch (bo->op) {
				case Token_CmpEq: ir_write_str_lit(f, "oeq"); break;
				case Token_NotEq: ir_write_str_lit(f, "one"); break;
				case Token_Lt:    ir_write_str_lit(f, "olt"); break;
				case Token_Gt:    ir_write_str_lit(f, "ogt"); break;
				case Token_LtEq:  ir_write_str_lit(f, "ole"); break;
				case Token_GtEq:  ir_write_str_lit(f, "oge"); break;
				}
			} else if (is_type_complex(elem_type)) {
				GB_PANIC("Unhandled complex type");
				return;
			} else {
				ir_write_str_lit(f, "icmp ");
				if (bo->op != Token_CmpEq &&
				    bo->op != Token_NotEq) {
					if (is_type_unsigned(elem_type)) {
						ir_write_str_lit(f, "u");
					} else {
						ir_write_str_lit(f, "s");
					}
				}
				switch (bo->op) {
				case Token_CmpEq: ir_write_str_lit(f, "eq"); break;
				case Token_NotEq: ir_write_str_lit(f, "ne"); break;
				case Token_Lt:    ir_write_str_lit(f, "lt"); break;
				case Token_Gt:    ir_write_str_lit(f, "gt"); break;
				case Token_LtEq:  ir_write_str_lit(f, "le"); break;
				case Token_GtEq:  ir_write_str_lit(f, "ge"); break;
				default: GB_PANIC("invalid comparison");break;
				}
			}
		} else {
			if (is_type_float(elem_type)) {
				ir_write_str_lit(f, "f");
			}

			switch (bo->op) {
			case Token_Add:    ir_write_str_lit(f, "add");  break;
			case Token_Sub:    ir_write_str_lit(f, "sub");  break;
			case Token_And:    ir_write_str_lit(f, "and");  break;
			case Token_Or:     ir_write_str_lit(f, "or");   break;
			case Token_Xor:    ir_write_str_lit(f, "xor");  break;
			case Token_Shl:    ir_write_str_lit(f, "shl");  break;
			case Token_Mul:    ir_write_str_lit(f, "mul");  break;
			case Token_Not:    ir_write_str_lit(f, "xor");  break;
			case Token_Shr:
				if (is_type_unsigned(elem_type)) {
					ir_write_str_lit(f, "lshr");
				} else {
					ir_write_str_lit(f, "ashr");
				}
				break;

			case Token_AndNot: GB_PANIC("Token_AndNot Should never be called");

			default: {
				if (!is_type_float(elem_type)) {
					if (is_type_unsigned(elem_type)) {
						ir_write_str_lit(f, "u");
					} else {
						ir_write_str_lit(f, "s");
					}
				}

				switch (bo->op) {
				case Token_Quo: ir_write_str_lit(f, "div"); break;
				case Token_Mod: ir_write_str_lit(f, "rem"); break;
				}
				break;
			}
			}
		}

		ir_write_byte(f, ' ');
		ir_print_type(f, m, type);
		ir_write_byte(f, ' ');
		ir_print_value(f, m, bo->left, type);
		ir_write_str_lit(f, ", ");
		ir_print_value(f, m, bo->right, type);

		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_Call: {
		irInstrCall *call = &instr->Call;
		Type *proc_type = base_type(ir_type(call->value));
		GB_ASSERT(is_type_proc(proc_type));
		set_procedure_abi_types(heap_allocator(), proc_type);

		bool is_c_vararg = proc_type->Proc.c_vararg;
		Type *result_type = call->type;
		if (result_type) {
			ir_fprintf(f, "%%%d = ", value->index);
		}
		ir_write_str_lit(f, "call ");
		ir_print_calling_convention(f, m, proc_type->Proc.calling_convention);
		if (is_c_vararg) {
			ir_print_proc_type_without_pointer(f, m, proc_type);
		} else if (result_type && !proc_type->Proc.return_by_pointer) {
			ir_print_proc_results(f, m, proc_type);
		} else {
			ir_write_str_lit(f, "void");
		}
		ir_write_byte(f, ' ');
		ir_print_value(f, m, call->value, call->type);

		bool return_by_pointer = proc_type->Proc.return_by_pointer;

		isize param_index = 0;

		ir_write_byte(f, '(');
		if (return_by_pointer) {
			GB_ASSERT(call->return_ptr != nullptr);
			ir_print_type(f, m, proc_type->Proc.results);
			ir_write_str_lit(f, "* ");
			ir_print_value(f, m, call->return_ptr, ir_type(call->return_ptr));
			param_index += 1;
		}


		if (call->args.count > 0) {
			TypeTuple *params = &proc_type->Proc.params->Tuple;
			if (proc_type->Proc.c_vararg) {
				isize i = 0;
				isize arg_index = 0;
				for (; i < params->variables.count-1; i++) {
					Entity *e = params->variables[i];
					GB_ASSERT(e != nullptr);
					if (e->kind != Entity_Variable) {
						arg_index++;
						continue;
					}

					if (param_index > 0) ir_write_str_lit(f, ", ");

					Type *t = proc_type->Proc.abi_compat_params[i];
					if (is_type_tuple(t)) {
						for_array(j, t->Tuple.variables) {
							if (j > 0) ir_write_str_lit(f, ", ");

							irValue *arg = call->args[arg_index++];

							ir_print_type(f, m, t->Tuple.variables[j]->type);
							if (e->flags&EntityFlag_NoAlias) {
								ir_write_str_lit(f, " noalias");
							}
							if (e->flags&EntityFlag_ByVal) {
								ir_write_str_lit(f, " byval");
							}
							ir_write_byte(f, ' ');
							ir_print_value(f, m, arg, t);
							param_index++;
						}
					} else {
						ir_print_type(f, m, t);
						if (e->flags&EntityFlag_NoAlias) {
							ir_write_str_lit(f, " noalias");
						}
						if (e->flags&EntityFlag_ImplicitReference) {
							ir_write_str_lit(f, " nonnull dereferenceable");
						}
						if (e->flags&EntityFlag_ByVal) {
							ir_write_str_lit(f, " byval");
						}
						ir_write_byte(f, ' ');
						irValue *arg = call->args[arg_index++];
						ir_print_value(f, m, arg, t);
						param_index++;
					}
				}
				while (arg_index < call->args.count) {
					if (param_index > 0) ir_write_str_lit(f, ", ");

					irValue *arg = call->args[arg_index];
					Type *t = ir_type(arg);
					ir_print_type(f, m, t);
					ir_write_byte(f, ' ');
					ir_print_value(f, m, arg, t);
					param_index++;
				}
			} else {
				// GB_ASSERT(call->args.count == params->variables.count);
				isize arg_index = 0;
				for_array(i, params->variables) {
					Entity *e = params->variables[i];
					GB_ASSERT(e != nullptr);
					if (e->kind != Entity_Variable) {
						arg_index++;
						continue;
					}

					if (param_index > 0) ir_write_str_lit(f, ", ");

					Type *t = proc_type->Proc.abi_compat_params[i];
					if (is_type_tuple(t)) {
						for_array(j, t->Tuple.variables) {
							if (j > 0) ir_write_str_lit(f, ", ");

							irValue *arg = call->args[arg_index++];

							ir_print_type(f, m, t->Tuple.variables[j]->type);
							if (e->flags&EntityFlag_NoAlias) {
								ir_write_str_lit(f, " noalias");
							}
							if (e->flags&EntityFlag_ByVal) {
								ir_write_str_lit(f, " byval");
							}
							ir_write_byte(f, ' ');
							ir_print_value(f, m, arg, t);
							param_index++;
						}
					} else {
						irValue *arg = call->args[arg_index++];
						ir_print_type(f, m, t);
						if (e->flags&EntityFlag_NoAlias) {
							ir_write_str_lit(f, " noalias");
						}
						if (e->flags&EntityFlag_ByVal) {
							ir_write_str_lit(f, " byval");
						}
						ir_write_byte(f, ' ');
						ir_print_value(f, m, arg, t);
						param_index++;
					}
				}
			}
		}
		if (proc_type->Proc.calling_convention == ProcCC_Odin) {
			if (param_index > 0) ir_write_str_lit(f, ", ");

			ir_print_context_parameter_prefix(f, m);
			ir_print_value(f, m, call->context_ptr, t_context_ptr);
		}
		ir_write_str_lit(f, ")");

		if (proc_type->Proc.diverging) {
			ir_write_str_lit(f, " noreturn");
		}
		switch (call->inlining) {
		case ProcInlining_inline:    ir_write_str_lit(f, " alwaysinline"); break;
		case ProcInlining_no_inline: ir_write_str_lit(f, " noinline");     break;
		}
		ir_print_debug_location(f, m, value);

		break;
	}

	case irInstr_Select: {
		ir_fprintf(f, "%%%d = select i1 ", value->index);
		ir_print_value(f, m, instr->Select.cond, t_bool);
		ir_write_string(f, str_lit(", "));
		ir_print_type(f, m, ir_type(instr->Select.true_value));
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->Select.true_value, ir_type(instr->Select.true_value));
		ir_write_string(f, str_lit(", "));
		ir_print_type(f, m, ir_type(instr->Select.false_value));
		ir_write_byte(f, ' ');
		ir_print_value(f, m, instr->Select.false_value, ir_type(instr->Select.false_value));
		ir_print_debug_location(f, m, value);
		break;
	}

	case irInstr_DebugDeclare: {
		if (!m->generate_debug_info) {
			break;
		}

		irInstrDebugDeclare *dd = &instr->DebugDeclare;
		Type *vt = ir_type(dd->value);
		Entity *e = dd->entity;
		String name = e->token.string;
		TokenPos pos = e->token.pos;

		irDebugInfo **lookup_di = map_get(&m->debug_info, hash_entity(e));
		if (lookup_di != nullptr) {
			GB_ASSERT_NOT_NULL(*lookup_di);
			irDebugInfo* local_var_di = *lookup_di;

			ir_write_str_lit(f, "call void @llvm.dbg.declare(");
			ir_write_str_lit(f, "metadata ");
			ir_print_type(f, m, vt);
			ir_write_byte(f, ' ');
			ir_print_value(f, m, dd->value, vt);
			ir_fprintf(f, ", metadata !%d", local_var_di->id);
			ir_write_str_lit(f, ", metadata !DIExpression())");
			ir_print_debug_location(f, m, value);
		}
		break;
	}
	}
}


void ir_print_proc(irFileBuffer *f, irModule *m, irProcedure *proc) {
	set_procedure_abi_types(heap_allocator(), proc->type);

	if (proc->body == nullptr) {
		ir_write_str_lit(f, "declare ");
		// if (proc->tags & ProcTag_dll_import) {
			// ir_write_string(f, "dllimport ");
		// }
	} else {
		ir_write_byte(f, '\n');
		ir_write_str_lit(f, "define ");
		if (proc->is_export) {
			ir_write_str_lit(f, "dllexport ");
		}
		// if (!proc->is_export && !proc->is_foreign && !proc->is_entry_point) {
			// ir_write_string(f, "internal ");
		// }
	}

	TypeProc *proc_type = &proc->type->Proc;

	ir_print_calling_convention(f, m, proc_type->calling_convention);

	isize param_count = proc_type->param_count;
	isize result_count = proc_type->result_count;
	ir_print_proc_results(f, m, proc->type);
	ir_write_byte(f, ' ');

// #ifndef GB_SYSTEM_WINDOWS
#if 0
	if(uses_args)
		ir_write_string(f, "@.nix_argpatch_main");
	else
#endif
	ir_print_encoded_global(f, proc->name, ir_print_is_proc_global(m, proc));


	isize param_index = 0;

	ir_write_byte(f, '(');
	if (proc_type->return_by_pointer) {
		ir_print_type(f, m, reduce_tuple_to_single_type(proc_type->results));
		ir_write_str_lit(f, "* sret noalias ");
		if (build_context.ODIN_OS == "darwin" ||
		    build_context.ODIN_OS == "linux") {
			ir_fprintf(f, "byval ");
		}
		ir_write_str_lit(f, "%agg.result");
		param_index += 1;
	}

	if (param_count > 0) {
		TypeTuple *params = &proc_type->params->Tuple;
		isize parameter_index = 0;
		for (isize i = 0; i < param_count; i++, parameter_index++) {
			Entity *e = params->variables[i];
			Type *original_type = e->type;
			Type *abi_type = proc_type->abi_compat_params[i];
			if (e->kind != Entity_Variable) continue;
			if (param_index > 0) ir_write_string(f, str_lit(", "));

			if (i+1 == params->variables.count && proc_type->c_vararg) {
				ir_write_str_lit(f, " ...");
			} else {
				if (is_type_tuple(abi_type)) {
					for_array(j, abi_type->Tuple.variables) {
						if (j > 0) ir_write_string(f, str_lit(", "));

						Type *tft = abi_type->Tuple.variables[j]->type;
						ir_print_type(f, m, tft);
						if (e->flags&EntityFlag_NoAlias) {
							ir_write_str_lit(f, " noalias");
						}
						if (e->flags&EntityFlag_ByVal) {
							ir_write_str_lit(f, " byval");
						}


						if (proc->body != nullptr) {
							ir_fprintf(f, " %%_.%td", parameter_index+j);
						}
					}
					parameter_index += abi_type->Tuple.variables.count-1;
					param_index += abi_type->Tuple.variables.count-1;
				} else {
					ir_print_type(f, m, abi_type);
					if (e->flags&EntityFlag_NoAlias) {
						ir_write_str_lit(f, " noalias");
					}
					if (e->flags&EntityFlag_ByVal) {
						ir_write_str_lit(f, " byval");
					}
					if (proc->body != nullptr) {
						ir_fprintf(f, " %%_.%td", parameter_index);
					}
				}
			}

			param_index++;
		}
	}
	if (proc_type->calling_convention == ProcCC_Odin) {
		if (param_index > 0) ir_write_str_lit(f, ", ");

		ir_print_context_parameter_prefix(f, m);
		ir_write_str_lit(f, "%__.context_ptr");
	}

	ir_write_str_lit(f, ") ");

	switch (proc->inlining) {
	default:
		ir_fprintf(f, "#0 ");
		break;
	case ProcInlining_inline:
		ir_write_str_lit(f, "alwaysinline ");
		ir_fprintf(f, "#1 ");
		break;
	case ProcInlining_no_inline:
		ir_write_str_lit(f, "noinline ");
		ir_fprintf(f, "#2 ");
		break;
	}

	if (proc_type->diverging) {
		ir_write_str_lit(f, "noreturn ");
	}

	if (m->generate_debug_info && proc->entity != nullptr && proc->body != nullptr) {
		irDebugInfo **di_ = map_get(&proc->module->debug_info, hash_pointer(proc->entity));
		if (di_ != nullptr) {
			irDebugInfo *di = *di_;
			GB_ASSERT(di->kind == irDebugInfo_Proc);
			ir_fprintf(f, "!dbg !%d ", di->id); // TODO(lachsinc): !dbg
		}
	}



	if (proc->body != nullptr) {
		// ir_fprintf(f, "nounwind uwtable {\n");

		ir_write_str_lit(f, "{\n");
		for_array(i, proc->blocks) {
			irBlock *block = proc->blocks[i];

			if (i > 0) ir_write_byte(f, '\n');
			ir_print_block_name(f, block);
			ir_write_str_lit(f, ":\n");

			for_array(j, block->instrs) {
				irValue *value = block->instrs[j];
				ir_print_instr(f, m, value);
			}
		}
		ir_write_str_lit(f, "}\n");
	} else {
		ir_write_byte(f, '\n');
	}

	for_array(i, proc->children) {
		ir_print_proc(f, m, proc->children[i]);
	}
}


void ir_print_type_name(irFileBuffer *f, irModule *m, irValue *v) {
	GB_ASSERT(v->kind == irValue_TypeName);
	Type *t = base_type(v->TypeName.type);

	ir_print_encoded_local(f, v->TypeName.name);
	ir_write_string(f, str_lit(" = type "));


	switch (t->kind) {
	case Type_Union:
		if (t->Union.variants.count == 0) {
			ir_write_string(f, str_lit("{}"));
		} else {
			ir_print_type(f, m, t);
		}
		break;
	case Type_Struct:
		if (t->Struct.fields.count == 0) {
			if (t->Struct.is_packed) {
				ir_write_byte(f, '<');
			}
			ir_write_byte(f, '{');
			if (t->Struct.custom_align > 0) {
				ir_print_alignment_prefix_hack(f, t->Struct.custom_align);
			}
			ir_write_byte(f, '}');
			if (t->Struct.is_packed) {
				ir_write_byte(f, '>');
			}
		} else {
			ir_print_type(f, m, t);
		}
		break;
	default:
		ir_print_type(f, m, t);
		break;
	}

	ir_write_byte(f, '\n');
}

bool ir_print_global_type_allowed(Type *t) {
	if (t == nullptr) {
		return true;
	}
	t = core_type(t);
	switch (t->kind) {
	case Type_DynamicArray:
	case Type_Map:
	case Type_Union:
	case Type_BitField:
		return false;
	}

	return true;
}

void print_llvm_ir(irGen *ir) {
	irModule *m = &ir->module;

	irFileBuffer buf = {}, *f = &buf;
	ir_file_buffer_init(f, &ir->output_file);
	defer (ir_file_buffer_destroy(f));

	i32 word_bits = cast(i32)(8*build_context.word_size);
	if (build_context.ODIN_OS == "darwin") {
		GB_ASSERT(word_bits == 64);
		ir_write_str_lit(f, "target datalayout = \"e-m:o-i64:64-f80:128-n8:16:32:64-S128\"\n");
		ir_write_str_lit(f, "target triple = \"x86_64-apple-macosx10.8\"\n");
		ir_write_str_lit(f, "\n");
	} else if (build_context.ODIN_OS == "windows") {
		ir_fprintf(f, "target triple = \"x86%s-pc-windows-msvc\"\n\n", word_bits == 64 ? "_64" : "");
		if (word_bits == 64 && build_context.metrics.arch == TargetArch_amd64) {
			ir_fprintf(f, "target datalayout = \"e-m:w-i64:64-f80:128-n8:16:32:64-S128\"\n\n");
		}
	}

	ir_print_encoded_local(f, str_lit("..opaque"));
	ir_write_str_lit(f, " = type {};\n");
	ir_print_encoded_local(f, str_lit("..string"));
	ir_write_str_lit(f, " = type {i8*, ");
	ir_print_type(f, m, t_int);
	ir_write_str_lit(f, "} ; Basic_string\n");
	ir_print_encoded_local(f, str_lit("..rawptr"));
	ir_write_str_lit(f, " = type i8* ; Basic_rawptr\n");

	ir_print_encoded_local(f, str_lit("..complex32"));
	ir_write_str_lit(f, " = type {half, half} ; Basic_complex32\n");
	ir_print_encoded_local(f, str_lit("..complex64"));
	ir_write_str_lit(f, " = type {float, float} ; Basic_complex64\n");
	ir_print_encoded_local(f, str_lit("..complex128"));
	ir_write_str_lit(f, " = type {double, double} ; Basic_complex128\n");

	ir_print_encoded_local(f, str_lit("..quaternion64"));
	ir_write_str_lit(f, " = type {half, half, half, half} ; Basic_quaternion64\n");
	ir_print_encoded_local(f, str_lit("..quaternion128"));
	ir_write_str_lit(f, " = type {float, float, float, float} ; Basic_quaternion128\n");
	ir_print_encoded_local(f, str_lit("..quaternion256"));
	ir_write_str_lit(f, " = type {double, double, double, double} ; Basic_quaternion256\n");

	ir_print_encoded_local(f, str_lit("..typeid"));
	ir_write_str_lit(f, " = type ");
	ir_print_type(f, m, t_uintptr);
	ir_write_str_lit(f, " ; Basic_typeid\n");

	ir_print_encoded_local(f, str_lit("..any"));
	ir_write_str_lit(f, " = type {");
	ir_print_type(f, m, t_rawptr);
	ir_write_str_lit(f, ", ");
	ir_print_type(f, m, t_typeid);
	ir_write_str_lit(f, "} ; Basic_any\n");

	ir_write_str_lit(f, "declare void @llvm.dbg.declare(metadata, metadata, metadata) #3 \n");

	if (string_map_get(&m->members, str_lit("llvm.bswap.i16")) == nullptr) {
		ir_write_str_lit(f, "declare i16 @llvm.bswap.i16(i16) \n");
	}
	if (string_map_get(&m->members, str_lit("llvm.bswap.i32")) == nullptr) {
		ir_write_str_lit(f, "declare i32 @llvm.bswap.i32(i32) \n");
	}
	if (string_map_get(&m->members, str_lit("llvm.bswap.i64")) == nullptr) {
		ir_write_str_lit(f, "declare i64 @llvm.bswap.i64(i64) \n");
	}
	if (string_map_get(&m->members, str_lit("llvm.bswap.i128")) == nullptr) {
		ir_write_str_lit(f, "declare i128 @llvm.bswap.i128(i128) \n");
	}
	ir_write_byte(f, '\n');


	for_array(member_index, m->members.entries) {
		auto *entry = &m->members.entries[member_index];
		irValue *v = entry->value;
		if (v->kind != irValue_TypeName) {
			continue;
		}
		ir_print_type_name(f, m, v);
	}

	ir_write_byte(f, '\n');

	bool dll_main_found = false;

	// NOTE(bill): Print foreign prototypes first
	for_array(member_index, m->members.entries) {
		auto *entry = &m->members.entries[member_index];
		irValue *v = entry->value;
		if (v->kind != irValue_Proc) {
			continue;
		}

		if (v->Proc.body == nullptr) {
			ir_print_proc(f, m, &v->Proc);
		}
	}

	if (ir->print_chkstk) {
		// TODO(bill): Clean up this code
		ir_write_str_lit(f, "\n\n");
		ir_write_str_lit(f, "define void @__chkstk() #0 {\n");
		ir_write_str_lit(f, "\tcall void asm sideeffect \"push   %rcx \\09\\0Apush   %rax \\09\\0Acmp    $$0x1000,%rax \\09\\0Alea    24(%rsp),%rcx \\09\\0Ajb     1f \\09\\0A2: \\09\\0Asub    $$0x1000,%rcx \\09\\0Aorl    $$0,(%rcx) \\09\\0Asub    $$0x1000,%rax \\09\\0Acmp    $$0x1000,%rax \\09\\0Aja     2b \\09\\0A1: \\09\\0Asub    %rax,%rcx \\09\\0Aorl    $$0,(%rcx) \\09\\0Apop    %rax \\09\\0Apop    %rcx \\09\\0Aret \\09\\0A\", \"~{dirflag},~{fpsr},~{flags}\"()\n");
		ir_write_str_lit(f, "\tret void\n");
		ir_write_str_lit(f, "}\n\n");
	}

	// NOTE(bill): Print procedures with bodies next
	for_array(member_index, m->members.entries) {
		auto *entry = &m->members.entries[member_index];
		irValue *v = entry->value;
		if (v->kind != irValue_Proc) {
			continue;
		}

		if (v->Proc.body != nullptr) {
			ir_print_proc(f, m, &v->Proc);
		}
	}

	for_array(member_index, m->members.entries) {
		auto *entry = &m->members.entries[member_index];
		irValue *v = entry->value;
		if (v->kind != irValue_Global) {
			continue;
		}
		irValueGlobal *g = &v->Global;
		Scope *scope = g->entity->scope;
		bool in_global_scope = false;
		if (scope != nullptr) {
			// TODO(bill): Fix this rule. What should it be?
			in_global_scope = (scope->flags & ScopeFlag_Global) != 0;
		}

		ir_print_encoded_global(f, ir_get_global_name(m, v), in_global_scope);
		ir_write_string(f, str_lit(" = "));
		if (g->is_foreign) {
			ir_write_string(f, str_lit("external "));
		}
		if (g->is_export) {
			ir_write_string(f, str_lit("dllexport "));
		}

		if (g->is_private) {
			ir_write_string(f, str_lit("private "));
		} else if (g->is_internal) {
			ir_write_string(f, str_lit("internal "));
		}
		if (g->thread_local_model.len > 0) {
			String model = g->thread_local_model;
			if (model == "default") {
				ir_write_string(f, str_lit("thread_local "));
			} else {
				ir_fprintf(f, "thread_local(%.*s) ", LIT(model));

			}
		}
		if (g->is_constant) {
			if (g->is_unnamed_addr) {
				ir_write_string(f, str_lit("unnamed_addr "));
			}
			ir_write_string(f, str_lit("constant "));
		} else {
			ir_write_string(f, str_lit("global "));
		}


		ir_print_type(f, m, g->entity->type);
		ir_write_byte(f, ' ');
		if (!g->is_foreign) {
			if (g->value != nullptr && ir_print_global_type_allowed(g->entity->type)) {
				ir_print_value(f, m, g->value, g->entity->type);
			} else {
				ir_write_string(f, str_lit("zeroinitializer"));
			}
			if (m->generate_debug_info) {
				irDebugInfo **di_lookup = map_get(&m->debug_info, hash_entity(g->entity));
				if (di_lookup != nullptr) {
					irDebugInfo *di = *di_lookup;
					GB_ASSERT(di);
					GB_ASSERT(di->kind == irDebugInfo_GlobalVariableExpression);
					ir_fprintf(f, ", !dbg !%d", di->id);
				}
			}
		}
		ir_write_byte(f, '\n');
	}

	// TODO(lachsinc): Attribute map inside ir module?
	ir_fprintf(f, "attributes #0 = {nounwind uwtable}\n");
	ir_fprintf(f, "attributes #1 = {nounwind alwaysinline uwtable}\n");
	ir_fprintf(f, "attributes #2 = {nounwind noinline optnone uwtable}\n");
	ir_fprintf(f, "attributes #3 = {nounwind readnone}\n");

	if (m->generate_debug_info) {
		ir_write_byte(f, '\n');

		i32 diec = cast(i32)m->debug_info.entries.count;

		i32 di_version    = diec+1;
		i32 di_debug_info = diec+2;
		i32 di_code_view  = diec+3;
		i32 di_wchar_size = diec+4;



		ir_fprintf(f, "!llvm.dbg.cu = !{!%d}\n", m->debug_compile_unit->id);
		ir_fprintf(f, "!llvm.ident = !{!%d}\n", di_version);
		ir_fprintf(f, "!llvm.module.flags = !{!%d, !%d, !%d}\n", di_debug_info, di_code_view, di_wchar_size);

		ir_fprintf(f, "!0 = !{}\n");

		for_array(di_index, m->debug_info.entries) {
			irDebugInfo *di = m->debug_info.entries[di_index].value;
			GB_ASSERT_MSG(di != nullptr, "Invalid irDebugInfo");
			ir_fprintf(f, "!%d = ", di->id);
			switch (di->kind) {
			case irDebugInfo_CompileUnit: {
				irDebugInfo **found = map_get(&m->debug_info, hash_pointer(di->CompileUnit.file));
				GB_ASSERT_MSG(found != nullptr, "Missing debug info for: %.*s\n", LIT(di->CompileUnit.file->fullpath));
				irDebugInfo *file = *found;
				ir_fprintf(f,
				            "distinct !DICompileUnit("
				              "language: DW_LANG_C_plus_plus" // Is this good enough?
				            ", file: !%d"
				            ", producer: \"Odin %.*s\""
				            ", runtimeVersion: 0"
				            ", isOptimized: false"
				            ", emissionKind: FullDebug"
				            ", retainedTypes: !0" // TODO(lachsinc)
				            ", enums: !%d"
				            ", globals: !%d"
				            ")",
				            file->id,
				            LIT(build_context.ODIN_VERSION),
				            m->debug_compile_unit->CompileUnit.enums->id,
				            m->debug_compile_unit->CompileUnit.globals->id);
				break;
			}
			case irDebugInfo_File:
				ir_fprintf(f, "!DIFile(filename: \""); ir_print_escape_path(f, di->File.filename);
				ir_fprintf(f, "\", directory: \""); ir_print_escape_path(f, di->File.directory);
				ir_fprintf(f, "\"");
				ir_fprintf(f, ")");
				break;
			case irDebugInfo_Proc:
				// TODO(lachsinc): We need to store scope info inside di, not just file info, for procs.
				// Should all subprograms have distinct ??
				ir_fprintf(f, "distinct !DISubprogram("
				              "name: \"%.*s\""
				            ", linkageName: \"%.*s\""
				            ", scope: !%d"
				            ", file: !%d"
				            ", line: %td"
				            ", scopeLine: %td"
				            ", isDefinition: true"
				            ", isLocal: false" // TODO(lachsinc): Is this fine?
				            ", flags: DIFlagPrototyped"
				            ", isOptimized: false"
				            ", unit: !%d"
				            ", type: !%d",
				            LIT(di->Proc.entity->token.string),
				            LIT(di->Proc.name),
				            di->Proc.file->id, // TODO(lachsinc): HACK For now lets pretend all procs scope's == file.
				            di->Proc.file->id,
				            di->Proc.pos.line,
				            di->Proc.pos.line, // NOTE(lachsinc): Assume scopeLine always same as line.
				            m->debug_compile_unit->id,
							di->Proc.type->id);
				ir_write_byte(f, ')'); // !DISubprogram(
				break;
			case irDebugInfo_ProcType:
				ir_fprintf(f, "!DISubroutineType(types: !%d)",
				            di->ProcType.types->id);
				break;
			case irDebugInfo_Location:
				GB_ASSERT_NOT_NULL(di->Location.scope);
				ir_fprintf(f, "!DILocation("
				              "line: %td"
				            ", column: %td"
				            ", scope: !%d)",
				            di->Location.pos.line,
				            di->Location.pos.column,
				            di->Location.scope->id);
				break;
			case irDebugInfo_LexicalBlock:
				GB_ASSERT_NOT_NULL(di->LexicalBlock.file);
				GB_ASSERT_NOT_NULL(di->LexicalBlock.scope);
				ir_fprintf(f, "distinct !DILexicalBlock("
				              "line: %td"
				            ", column: %td"
				            ", file: !%d"
				            ", scope: !%d)",
				            di->LexicalBlock.pos.line,
				            di->LexicalBlock.pos.column,
				            di->LexicalBlock.file->id,
				            di->LexicalBlock.scope->id);
				break;
			case irDebugInfo_GlobalVariableExpression: {
				ir_fprintf(f, "!DIGlobalVariableExpression("
				              "var: !%d"
				            ", expr: !DIExpression(",
				           di->GlobalVariableExpression.var->id);
				if (di->GlobalVariableExpression.var->GlobalVariable.variable->Global.is_constant) {
					ir_write_str_lit(f, "DW_OP_constu, ");
					ir_print_value(f, m, di->GlobalVariable.variable, ir_type(di->GlobalVariable.variable));
					ir_write_str_lit(f, ", DW_OP_stack_value");
				} else {
					// NOTE(lachsinc): non-const globals expect empty "!DIExpression()"
				}
				ir_write_byte(f, ')'); // !DIExpression(
				ir_write_byte(f, ')'); // !DIGlobalVariableExpression(
				break;
			}
			case irDebugInfo_GlobalVariable: {
				ir_fprintf(f, "distinct !DIGlobalVariable("
				              "name: \"%.*s\""
				            ", scope: !%d"
				            ", file: !%d"
				            ", line: %d"
				            ", type: !%d"
				            ", isLocal: true"        // TODO(lachsinc): Check locality ??
				            ", isDefinition: true)", // TODO(lachsinc): ??
				            LIT(di->GlobalVariable.name),
				            di->GlobalVariable.scope->id,
				            di->GlobalVariable.file->id,
				            di->GlobalVariable.pos.line,
				            di->GlobalVariable.type->id);
				break;
			}
			case irDebugInfo_LocalVariable: {
				ir_fprintf(f, "!DILocalVariable("
				              "scope: !%d"
				            ", file: !%d"
				            ", line: %d"
				            ", type: !%d",
				            di->LocalVariable.scope->id,
				            di->LocalVariable.file->id,
				            di->LocalVariable.pos.line,
				            di->LocalVariable.type->id);
				if (di->DerivedType.name.len > 0) {
					ir_fprintf(f, ", name: \"%.*s\"", LIT(di->LocalVariable.name));
				}
				if (di->LocalVariable.arg > 0) {
					ir_fprintf(f, ", arg: %d", di->LocalVariable.arg);
				}
				ir_write_byte(f, ')');
				break;
			}
			case irDebugInfo_BasicType:
				ir_fprintf(f, "!DIBasicType("
				              "name: \"%.*s\""
				            ", size: %d"
				            ", encoding: ",
				            LIT(di->BasicType.name),
				            di->BasicType.size);
				ir_print_debug_encoding(f, irDebugInfo_BasicType, di->BasicType.encoding);
				ir_write_byte(f, ')');
				break;
			case irDebugInfo_DerivedType: {
				if (di->DerivedType.tag == irDebugBasicEncoding_member) {
					// NOTE(lachsinc): We crash llvm super hard if we don't specify a name :)
					Type *t = di->DerivedType.type;
					GB_ASSERT_MSG(di->DerivedType.name.len > 0, "%s", type_to_string(di->DerivedType.type));
				}
				ir_write_str_lit(f, "!DIDerivedType(tag: ");
				ir_print_debug_encoding(f, irDebugInfo_DerivedType, di->DerivedType.tag);
				if (di->DerivedType.name.len > 0) {
					ir_fprintf(f, ", name: \"%.*s\"", LIT(di->DerivedType.name));
				}
				if (di->DerivedType.base_type != nullptr) {
					ir_fprintf(f, ", baseType: !%d", di->DerivedType.base_type->id);
				} else {
					ir_write_str_lit(f, ", baseType: null"); // Valid/required for rawptr
				}
				if (di->DerivedType.size > 0)   ir_fprintf(f, ", size: %d", di->DerivedType.size);
				if (di->DerivedType.align > 0)  ir_fprintf(f, ", align: %d", di->DerivedType.align);
				if (di->DerivedType.offset > 0) ir_fprintf(f, ", offset: %d", di->DerivedType.offset);
				if (di->DerivedType.flags > 0) {
					// TODO(lachsinc): Handle in a more generic manner.
					if (di->DerivedType.flags & irDebugInfoFlag_Bitfield) ir_write_str_lit(f, ", flags: DIFlagBitField, extraData: i64 0");
				}
				ir_write_byte(f, ')');
				break;
			}
			case irDebugInfo_CompositeType: {
				if (di->CompositeType.tag == irDebugBasicEncoding_array_type) {
					GB_ASSERT_NOT_NULL(di->CompositeType.base_type);
					GB_ASSERT(di->CompositeType.array_count >= 0);
					GB_ASSERT(di->CompositeType.name.len == 0);
					GB_ASSERT(di->CompositeType.size >= 0);
				}

				if (di->CompositeType.tag == irDebugBasicEncoding_union_type) {
					GB_ASSERT_NOT_NULL(di->CompositeType.file); // Union _requires_ file to be valid.
				}

				ir_write_str_lit(f, "!DICompositeType(tag: ");
				ir_print_debug_encoding(f, irDebugInfo_CompositeType, di->CompositeType.tag);
				if (di->CompositeType.name.len > 0) {
					ir_fprintf(f, ", name: \"%.*s\"", LIT(di->CompositeType.name));
				}
				if (di->CompositeType.scope != nullptr) {
					ir_fprintf(f, ", scope: !%d", di->CompositeType.scope->id);
				}
				if (di->CompositeType.file != nullptr) {
					ir_fprintf(f, ", file: !%d"
					              ", line: %td",
					              di->CompositeType.file->id,
					              di->CompositeType.pos.line);
				}
				if (di->CompositeType.size > 0)  ir_fprintf(f, ", size: %d", di->CompositeType.size);
				if (di->CompositeType.align > 0) ir_fprintf(f, ", align: %d", di->CompositeType.align);
				if (di->CompositeType.base_type != nullptr) {
					GB_ASSERT(di->CompositeType.tag != irDebugBasicEncoding_structure_type);
					GB_ASSERT(di->CompositeType.tag != irDebugBasicEncoding_union_type);
					ir_fprintf(f, ", baseType: !%d", di->CompositeType.base_type->id);
				}
				if (di->CompositeType.tag == irDebugBasicEncoding_array_type) {
					ir_fprintf(f, ", elements: !{!DISubrange(count: %d)}", di->CompositeType.array_count);
				} else {
					if (di->CompositeType.elements != nullptr) {
						ir_fprintf(f, ", elements: !%d", di->CompositeType.elements->id);
					}
				}
				ir_write_byte(f, ')');
				break;
			}
			case irDebugInfo_Enumerator: {
				ir_fprintf(f, "!DIEnumerator("
				              "name: \"%.*s\""
				            ", value: %lld)",
				            LIT(di->Enumerator.name),
				            di->Enumerator.value);
				break;
			}
			case irDebugInfo_DebugInfoArray:
				ir_fprintf(f, "!{");
				for_array(element_index, di->DebugInfoArray.elements) {
					irDebugInfo *elem = di->DebugInfoArray.elements[element_index];
					if (element_index > 0) ir_write_str_lit(f, ", ");
					if (elem != nullptr) {
						ir_fprintf(f, "!%d", elem->id);
					} else {
						ir_fprintf(f, "null"); // NOTE(lachsinc): Proc's can contain "nullptr" entries to represent void return values.
					}
				}
				ir_write_byte(f, '}');
				break;

			default:
				GB_PANIC("Unhandled irDebugInfo kind %d", di->kind);
				break;
			}

			ir_write_byte(f, '\n');
		}


		ir_fprintf(f, "!%d = !{!\"Odin version %.*s \"}\n", di_version, LIT(build_context.ODIN_VERSION));
		ir_fprintf(f, "!%d = !{i32 2, !\"Debug Info Version\", i32 3}\n", di_debug_info);
		ir_fprintf(f, "!%d = !{i32 2, !\"CodeView\", i32 1}\n",           di_code_view);
		ir_fprintf(f, "!%d = !{i32 1, !\"wchar_size\", i32 2}\n",         di_wchar_size);
	}
}
