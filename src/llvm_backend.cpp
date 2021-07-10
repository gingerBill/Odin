#define MULTITHREAD_OBJECT_GENERATION 1

#ifndef USE_SEPARTE_MODULES
#define USE_SEPARTE_MODULES build_context.use_separate_modules
#endif

#ifndef MULTITHREAD_OBJECT_GENERATION
#define MULTITHREAD_OBJECT_GENERATION 0
#endif


#include "llvm_backend.hpp"
#include "llvm_abi.cpp"
#include "llvm_backend_opt.cpp"

gb_global ThreadPool lb_thread_pool = {};

gb_global Entity *lb_global_type_info_data_entity   = {};
gb_global lbAddr lb_global_type_info_member_types   = {};
gb_global lbAddr lb_global_type_info_member_names   = {};
gb_global lbAddr lb_global_type_info_member_offsets = {};
gb_global lbAddr lb_global_type_info_member_usings  = {};
gb_global lbAddr lb_global_type_info_member_tags    = {};

gb_global isize lb_global_type_info_data_index           = 0;
gb_global isize lb_global_type_info_member_types_index   = 0;
gb_global isize lb_global_type_info_member_names_index   = 0;
gb_global isize lb_global_type_info_member_offsets_index = 0;
gb_global isize lb_global_type_info_member_usings_index  = 0;
gb_global isize lb_global_type_info_member_tags_index    = 0;


lbValue lb_global_type_info_data_ptr(lbModule *m) {
	lbValue v = lb_find_value_from_entity(m, lb_global_type_info_data_entity);
	return v;
}


struct lbLoopData {
	lbAddr idx_addr;
	lbValue idx;
	lbBlock *body;
	lbBlock *done;
	lbBlock *loop;
};

struct lbCompoundLitElemTempData {
	Ast *   expr;
	lbValue value;
	i32     elem_index;
	lbValue gep;
};

lbLoopData lb_loop_start(lbProcedure *p, isize count, Type *index_type=t_i32);
void lb_loop_end(lbProcedure *p, lbLoopData const &data);

LLVMValueRef llvm_zero(lbModule *m) {
	return LLVMConstInt(lb_type(m, t_int), 0, false);
}
LLVMValueRef llvm_zero32(lbModule *m) {
	return LLVMConstInt(lb_type(m, t_i32), 0, false);
}
LLVMValueRef llvm_one(lbModule *m) {
	return LLVMConstInt(lb_type(m, t_i32), 1, false);
}

lbValue lb_zero(lbModule *m, Type *t) {
	lbValue v = {};
	v.value = LLVMConstInt(lb_type(m, t), 0, false);
	v.type = t;
	return v;
}

LLVMValueRef llvm_cstring(lbModule *m, String const &str) {
	lbValue v = lb_find_or_add_entity_string(m, str);
	unsigned indices[1] = {0};
	return LLVMConstExtractValue(v.value, indices, gb_count_of(indices));
}

bool lb_is_instr_terminating(LLVMValueRef instr) {
	if (instr != nullptr) {
		LLVMOpcode op = LLVMGetInstructionOpcode(instr);
		switch (op) {
		case LLVMRet:
		case LLVMBr:
		case LLVMSwitch:
		case LLVMIndirectBr:
		case LLVMInvoke:
		case LLVMUnreachable:
		case LLVMCallBr:
			return true;
		}
	}
	return false;
}



lbModule *lb_pkg_module(lbGenerator *gen, AstPackage *pkg) {
	auto *found = map_get(&gen->modules, hash_pointer(pkg));
	if (found) {
		return *found;
	}
	return &gen->default_module;
}


lbAddr lb_addr(lbValue addr) {
	lbAddr v = {lbAddr_Default, addr};
	if (addr.type != nullptr && is_type_relative_pointer(type_deref(addr.type))) {
		GB_ASSERT(is_type_pointer(addr.type));
		v.kind = lbAddr_RelativePointer;
	} else if (addr.type != nullptr && is_type_relative_slice(type_deref(addr.type))) {
		GB_ASSERT(is_type_pointer(addr.type));
		v.kind = lbAddr_RelativeSlice;
	}
	return v;
}


lbAddr lb_addr_map(lbValue addr, lbValue map_key, Type *map_type, Type *map_result) {
	lbAddr v = {lbAddr_Map, addr};
	v.map.key    = map_key;
	v.map.type   = map_type;
	v.map.result = map_result;
	return v;
}


lbAddr lb_addr_soa_variable(lbValue addr, lbValue index, Ast *index_expr) {
	lbAddr v = {lbAddr_SoaVariable, addr};
	v.soa.index = index;
	v.soa.index_expr = index_expr;
	return v;
}

lbAddr lb_addr_swizzle(lbValue addr, Type *array_type, u8 swizzle_count, u8 swizzle_indices[4]) {
	GB_ASSERT(is_type_array(array_type));
	GB_ASSERT(1 < swizzle_count && swizzle_count <= 4);
	lbAddr v = {lbAddr_Swizzle, addr};
	v.swizzle.type = array_type;
	v.swizzle.count = swizzle_count;
	gb_memmove(v.swizzle.indices, swizzle_indices, swizzle_count);
	return v;
}

Type *lb_addr_type(lbAddr const &addr) {
	if (addr.addr.value == nullptr) {
		return nullptr;
	}
	if (addr.kind == lbAddr_Map) {
		Type *t = base_type(addr.map.type);
		GB_ASSERT(is_type_map(t));
		return t->Map.value;
	}
	if (addr.kind == lbAddr_Swizzle) {
		return addr.swizzle.type;
	}
	return type_deref(addr.addr.type);
}
LLVMTypeRef lb_addr_lb_type(lbAddr const &addr) {
	return LLVMGetElementType(LLVMTypeOf(addr.addr.value));
}

lbValue lb_addr_get_ptr(lbProcedure *p, lbAddr const &addr) {
	if (addr.addr.value == nullptr) {
		GB_PANIC("Illegal addr -> nullptr");
		return {};
	}

	switch (addr.kind) {
	case lbAddr_Map: {
		Type *map_type = base_type(addr.map.type);
		lbValue h = lb_gen_map_header(p, addr.addr, map_type);
		lbValue key = lb_gen_map_hash(p, addr.map.key, map_type->Map.key);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = h;
		args[1] = key;

		lbValue ptr = lb_emit_runtime_call(p, "__dynamic_map_get", args);

		return lb_emit_conv(p, ptr, alloc_type_pointer(map_type->Map.value));
	}

	case lbAddr_RelativePointer: {
		Type *rel_ptr = base_type(lb_addr_type(addr));
		GB_ASSERT(rel_ptr->kind == Type_RelativePointer);

		lbValue ptr = lb_emit_conv(p, addr.addr, t_uintptr);
		lbValue offset = lb_emit_conv(p, ptr, alloc_type_pointer(rel_ptr->RelativePointer.base_integer));
		offset = lb_emit_load(p, offset);

		if (!is_type_unsigned(rel_ptr->RelativePointer.base_integer)) {
		offset = lb_emit_conv(p, offset, t_i64);
		}
		offset = lb_emit_conv(p, offset, t_uintptr);
		lbValue absolute_ptr = lb_emit_arith(p, Token_Add, ptr, offset, t_uintptr);
		absolute_ptr = lb_emit_conv(p, absolute_ptr, rel_ptr->RelativePointer.pointer_type);

		lbValue cond = lb_emit_comp(p, Token_CmpEq, offset, lb_const_nil(p->module, rel_ptr->RelativePointer.base_integer));

		// NOTE(bill): nil check
		lbValue nil_ptr = lb_const_nil(p->module, rel_ptr->RelativePointer.pointer_type);
		lbValue final_ptr = lb_emit_select(p, cond, nil_ptr, absolute_ptr);
		return final_ptr;
	}

	case lbAddr_SoaVariable:
		// TODO(bill): FIX THIS HACK
		return lb_address_from_load(p, lb_addr_load(p, addr));

	case lbAddr_Context:
		GB_PANIC("lbAddr_Context should be handled elsewhere");
		break;

	case lbAddr_Swizzle:
		// TOOD(bill): is this good enough logic?
		break;
	}

	return addr.addr;
}


lbValue lb_build_addr_ptr(lbProcedure *p, Ast *expr) {
	lbAddr addr = lb_build_addr(p, expr);
	return lb_addr_get_ptr(p, addr);
}

void lb_emit_bounds_check(lbProcedure *p, Token token, lbValue index, lbValue len) {
	if (build_context.no_bounds_check) {
		return;
	}
	if ((p->state_flags & StateFlag_no_bounds_check) != 0) {
		return;
	}

	index = lb_emit_conv(p, index, t_int);
	len = lb_emit_conv(p, len, t_int);

	lbValue file = lb_find_or_add_entity_string(p->module, get_file_path_string(token.pos.file_id));
	lbValue line = lb_const_int(p->module, t_i32, token.pos.line);
	lbValue column = lb_const_int(p->module, t_i32, token.pos.column);

	auto args = array_make<lbValue>(permanent_allocator(), 5);
	args[0] = file;
	args[1] = line;
	args[2] = column;
	args[3] = index;
	args[4] = len;

	lb_emit_runtime_call(p, "bounds_check_error", args);
}

void lb_emit_slice_bounds_check(lbProcedure *p, Token token, lbValue low, lbValue high, lbValue len, bool lower_value_used) {
	if (build_context.no_bounds_check) {
		return;
	}
	if ((p->state_flags & StateFlag_no_bounds_check) != 0) {
		return;
	}

	lbValue file = lb_find_or_add_entity_string(p->module, get_file_path_string(token.pos.file_id));
	lbValue line = lb_const_int(p->module, t_i32, token.pos.line);
	lbValue column = lb_const_int(p->module, t_i32, token.pos.column);
	high = lb_emit_conv(p, high, t_int);

	if (!lower_value_used) {
		auto args = array_make<lbValue>(permanent_allocator(), 5);
		args[0] = file;
		args[1] = line;
		args[2] = column;
		args[3] = high;
		args[4] = len;

		lb_emit_runtime_call(p, "slice_expr_error_hi", args);
	} else {
		// No need to convert unless used
		low  = lb_emit_conv(p, low, t_int);

		auto args = array_make<lbValue>(permanent_allocator(), 6);
		args[0] = file;
		args[1] = line;
		args[2] = column;
		args[3] = low;
		args[4] = high;
		args[5] = len;

		lb_emit_runtime_call(p, "slice_expr_error_lo_hi", args);
	}
}

bool lb_try_update_alignment(lbValue ptr, unsigned alignment)  {
	LLVMValueRef addr_ptr = ptr.value;
	if (LLVMIsAGlobalValue(addr_ptr) || LLVMIsAAllocaInst(addr_ptr) || LLVMIsALoadInst(addr_ptr)) {
		if (LLVMGetAlignment(addr_ptr) < alignment) {
			if (LLVMIsAAllocaInst(addr_ptr) || LLVMIsAGlobalValue(addr_ptr)) {
				LLVMSetAlignment(addr_ptr, alignment);
			}
		}
		return LLVMGetAlignment(addr_ptr) >= alignment;
	}
	return false;
}

bool lb_try_vector_cast(lbModule *m, lbValue ptr, LLVMTypeRef *vector_type_) {
	Type *array_type = base_type(type_deref(ptr.type));
	GB_ASSERT(is_type_array_like(array_type));
	i64 count = get_array_type_count(array_type);
	Type *elem_type = base_array_type(array_type);

	// TODO(bill): Determine what is the correct limit for doing vector arithmetic
	if (type_size_of(array_type) <= build_context.max_align &&
	    is_type_valid_vector_elem(elem_type)) {
		// Try to treat it like a vector if possible
		bool possible = false;
		LLVMTypeRef vector_type = LLVMVectorType(lb_type(m, elem_type), cast(unsigned)count);
		unsigned vector_alignment = cast(unsigned)lb_alignof(vector_type);

		LLVMValueRef addr_ptr = ptr.value;
		if (LLVMIsAAllocaInst(addr_ptr) || LLVMIsAGlobalValue(addr_ptr)) {
			unsigned alignment = LLVMGetAlignment(addr_ptr);
			alignment = gb_max(alignment, vector_alignment);
			possible = true;
			LLVMSetAlignment(addr_ptr, alignment);
		} else if (LLVMIsALoadInst(addr_ptr)) {
			unsigned alignment = LLVMGetAlignment(addr_ptr);
			possible = alignment >= vector_alignment;
		}

		// NOTE: Due to alignment requirements, if the pointer is not correctly aligned
		// then it cannot be treated as a vector
		if (possible) {
			if (vector_type_) *vector_type_ =vector_type;
			return true;
		}
	}
	return false;
}

void lb_addr_store(lbProcedure *p, lbAddr addr, lbValue value) {
	if (addr.addr.value == nullptr) {
		return;
	}
	GB_ASSERT(value.type != nullptr);
	if (is_type_untyped_undef(value.type)) {
		Type *t = lb_addr_type(addr);
		value.type = t;
		value.value = LLVMGetUndef(lb_type(p->module, t));
	} else if (is_type_untyped_nil(value.type)) {
		Type *t = lb_addr_type(addr);
		value.type = t;
		value.value = LLVMConstNull(lb_type(p->module, t));
	}

	if (addr.kind == lbAddr_RelativePointer && addr.relative.deref) {
		addr = lb_addr(lb_address_from_load(p, lb_addr_load(p, addr)));
	}

	if (addr.kind == lbAddr_RelativePointer) {
		Type *rel_ptr = base_type(lb_addr_type(addr));
		GB_ASSERT(rel_ptr->kind == Type_RelativePointer);

		value = lb_emit_conv(p, value, rel_ptr->RelativePointer.pointer_type);

		GB_ASSERT(is_type_pointer(addr.addr.type));
		lbValue ptr = lb_emit_conv(p, addr.addr, t_uintptr);
		lbValue val_ptr = lb_emit_conv(p, value, t_uintptr);
		lbValue offset = {};
		offset.value = LLVMBuildSub(p->builder, val_ptr.value, ptr.value, "");
		offset.type = t_uintptr;

		if (!is_type_unsigned(rel_ptr->RelativePointer.base_integer)) {
			offset = lb_emit_conv(p, offset, t_i64);
		}
		offset = lb_emit_conv(p, offset, rel_ptr->RelativePointer.base_integer);

		lbValue offset_ptr = lb_emit_conv(p, addr.addr, alloc_type_pointer(rel_ptr->RelativePointer.base_integer));
		offset = lb_emit_select(p,
			lb_emit_comp(p, Token_CmpEq, val_ptr, lb_const_nil(p->module, t_uintptr)),
			lb_const_nil(p->module, rel_ptr->RelativePointer.base_integer),
			offset
		);
		LLVMBuildStore(p->builder, offset.value, offset_ptr.value);
		return;

	} else if (addr.kind == lbAddr_RelativeSlice) {
		Type *rel_ptr = base_type(lb_addr_type(addr));
		GB_ASSERT(rel_ptr->kind == Type_RelativeSlice);

		value = lb_emit_conv(p, value, rel_ptr->RelativeSlice.slice_type);

		GB_ASSERT(is_type_pointer(addr.addr.type));
		lbValue ptr = lb_emit_conv(p, lb_emit_struct_ep(p, addr.addr, 0), t_uintptr);
		lbValue val_ptr = lb_emit_conv(p, lb_slice_elem(p, value), t_uintptr);
		lbValue offset = {};
		offset.value = LLVMBuildSub(p->builder, val_ptr.value, ptr.value, "");
		offset.type = t_uintptr;

		if (!is_type_unsigned(rel_ptr->RelativePointer.base_integer)) {
			offset = lb_emit_conv(p, offset, t_i64);
		}
		offset = lb_emit_conv(p, offset, rel_ptr->RelativePointer.base_integer);


		lbValue offset_ptr = lb_emit_conv(p, addr.addr, alloc_type_pointer(rel_ptr->RelativePointer.base_integer));
		offset = lb_emit_select(p,
			lb_emit_comp(p, Token_CmpEq, val_ptr, lb_const_nil(p->module, t_uintptr)),
			lb_const_nil(p->module, rel_ptr->RelativePointer.base_integer),
			offset
		);
		LLVMBuildStore(p->builder, offset.value, offset_ptr.value);

		lbValue len = lb_slice_len(p, value);
		len = lb_emit_conv(p, len, rel_ptr->RelativePointer.base_integer);

		lbValue len_ptr = lb_emit_struct_ep(p, addr.addr, 1);
		LLVMBuildStore(p->builder, len.value, len_ptr.value);

		return;
	} else if (addr.kind == lbAddr_Map) {
		lb_insert_dynamic_map_key_and_value(p, addr, addr.map.type, addr.map.key, value, p->curr_stmt);
		return;
	} else if (addr.kind == lbAddr_Context) {
		lbAddr old_addr = lb_find_or_generate_context_ptr(p);


		// IMPORTANT NOTE(bill, 2021-04-22): reuse unused 'context' variables to minimize stack usage
		// This has to be done manually since the optimizer cannot determine when this is possible
		bool create_new = true;
		for_array(i, p->context_stack) {
			lbContextData *ctx_data = &p->context_stack[i];
			if (ctx_data->ctx.addr.value == old_addr.addr.value) {
				if (ctx_data->uses > 0) {
					create_new = true;
				} else if (p->scope_index > ctx_data->scope_index) {
					create_new = true;
				} else {
					// gb_printf_err("%.*s (curr:%td) (ctx:%td) (uses:%td)\n", LIT(p->name), p->scope_index, ctx_data->scope_index, ctx_data->uses);
					create_new = false;
				}
				break;
			}
		}

		lbValue next = {};
		if (create_new) {
			lbValue old = lb_addr_load(p, old_addr);
			lbAddr next_addr = lb_add_local_generated(p, t_context, true);
			lb_addr_store(p, next_addr, old);
			lb_push_context_onto_stack(p, next_addr);
			next = next_addr.addr;
		} else {
			next = old_addr.addr;
		}

		if (addr.ctx.sel.index.count > 0) {
			lbValue lhs = lb_emit_deep_field_gep(p, next, addr.ctx.sel);
			lbValue rhs = lb_emit_conv(p, value, type_deref(lhs.type));
			lb_emit_store(p, lhs, rhs);
		} else {
			lbValue lhs = next;
			lbValue rhs = lb_emit_conv(p, value, lb_addr_type(addr));
			lb_emit_store(p, lhs, rhs);
		}

		return;
	} else if (addr.kind == lbAddr_SoaVariable) {
		Type *t = type_deref(addr.addr.type);
		t = base_type(t);
		GB_ASSERT(t->kind == Type_Struct && t->Struct.soa_kind != StructSoa_None);
		Type *elem_type = t->Struct.soa_elem;
		value = lb_emit_conv(p, value, elem_type);
		elem_type = base_type(elem_type);

		lbValue index = addr.soa.index;
		if (!lb_is_const(index) || t->Struct.soa_kind != StructSoa_Fixed) {
			Type *t = base_type(type_deref(addr.addr.type));
			GB_ASSERT(t->kind == Type_Struct && t->Struct.soa_kind != StructSoa_None);
			lbValue len = lb_soa_struct_len(p, addr.addr);
			if (addr.soa.index_expr != nullptr) {
				lb_emit_bounds_check(p, ast_token(addr.soa.index_expr), index, len);
			}
		}

		isize field_count = 0;

		switch (elem_type->kind) {
		case Type_Struct:
			field_count = elem_type->Struct.fields.count;
			break;
		case Type_Array:
			field_count = elem_type->Array.count;
			break;
		}
		for (isize i = 0; i < field_count; i++) {
			lbValue dst = lb_emit_struct_ep(p, addr.addr, cast(i32)i);
			lbValue src = lb_emit_struct_ev(p, value, cast(i32)i);
			if (t->Struct.soa_kind == StructSoa_Fixed) {
				dst = lb_emit_array_ep(p, dst, index);
				lb_emit_store(p, dst, src);
			} else {
				lbValue field = lb_emit_load(p, dst);
				dst = lb_emit_ptr_offset(p, field, index);
				lb_emit_store(p, dst, src);
			}
		}
		return;
	} else if (addr.kind == lbAddr_Swizzle) {
		GB_ASSERT(addr.swizzle.count <= 4);

		GB_ASSERT(value.value != nullptr);
		value = lb_emit_conv(p, value, lb_addr_type(addr));

		lbValue dst = lb_addr_get_ptr(p, addr);
		lbValue src = lb_address_from_load_or_generate_local(p, value);
		{
			lbValue src_ptrs[4] = {};
			lbValue src_loads[4] = {};
			lbValue dst_ptrs[4] = {};

			for (u8 i = 0; i < addr.swizzle.count; i++) {
				src_ptrs[i] = lb_emit_array_epi(p, src, i);
			}
			for (u8 i = 0; i < addr.swizzle.count; i++) {
				dst_ptrs[i] = lb_emit_array_epi(p, dst, addr.swizzle.indices[i]);
			}
			for (u8 i = 0; i < addr.swizzle.count; i++) {
				src_loads[i] = lb_emit_load(p, src_ptrs[i]);
			}

			for (u8 i = 0; i < addr.swizzle.count; i++) {
				lb_emit_store(p, dst_ptrs[i], src_loads[i]);
			}
		}
		return;
	}

	GB_ASSERT(value.value != nullptr);
	value = lb_emit_conv(p, value, lb_addr_type(addr));

	// if (lb_is_const_or_global(value)) {
	// 	// NOTE(bill): Just bypass the actual storage and set the initializer
	// 	if (LLVMGetValueKind(addr.addr.value) == LLVMGlobalVariableValueKind) {
	// 		LLVMValueRef dst = addr.addr.value;
	// 		LLVMValueRef src = value.value;
	// 		LLVMSetInitializer(dst, src);
	// 		return;
	// 	}
	// }

	lb_emit_store(p, addr.addr, value);
}

void lb_const_store(lbValue ptr, lbValue value) {
	GB_ASSERT(lb_is_const(ptr));
	GB_ASSERT(lb_is_const(value));
	GB_ASSERT(is_type_pointer(ptr.type));
	LLVMSetInitializer(ptr.value, value.value);
}


bool lb_is_type_proc_recursive(Type *t) {
	for (;;) {
		if (t == nullptr) {
			return false;
		}
		switch (t->kind) {
		case Type_Named:
			t = t->Named.base;
			break;
		case Type_Pointer:
			t = t->Pointer.elem;
			break;
		case Type_Array:
			t = t->Array.elem;
			break;
		case Type_EnumeratedArray:
			t = t->EnumeratedArray.elem;
			break;
		case Type_Slice:
			t = t->Slice.elem;
			break;
		case Type_DynamicArray:
			t = t->DynamicArray.elem;
			break;
		case Type_Proc:
			return true;
		default:
			return false;
		}
	}
}

void lb_emit_store(lbProcedure *p, lbValue ptr, lbValue value) {
	GB_ASSERT(value.value != nullptr);
	Type *a = type_deref(ptr.type);
	if (is_type_boolean(a)) {
		// NOTE(bill): There are multiple sized booleans, thus force a conversion (if necessarily)
		value = lb_emit_conv(p, value, a);
	}
	Type *ca = core_type(a);
	if (ca->kind == Type_Basic) {
		GB_ASSERT_MSG(are_types_identical(ca, core_type(value.type)), "%s != %s", type_to_string(a), type_to_string(value.type));
	}

	if (lb_is_type_proc_recursive(a)) {
		// NOTE(bill, 2020-11-11): Because of certain LLVM rules, a procedure value may be
		// stored as regular pointer with no procedure information

		LLVMTypeRef src_t = LLVMGetElementType(LLVMTypeOf(ptr.value));
		LLVMValueRef v = LLVMBuildPointerCast(p->builder, value.value, src_t, "");
		LLVMBuildStore(p->builder, v, ptr.value);
	} else {
		Type *ca = core_type(a);
		if (ca->kind == Type_Basic || ca->kind == Type_Proc) {
			GB_ASSERT_MSG(are_types_identical(ca, core_type(value.type)), "%s != %s", type_to_string(a), type_to_string(value.type));
		} else {
			GB_ASSERT_MSG(are_types_identical(a, value.type), "%s != %s", type_to_string(a), type_to_string(value.type));
		}

		LLVMBuildStore(p->builder, value.value, ptr.value);
	}
}

LLVMTypeRef llvm_addr_type(lbValue addr_val) {
	return LLVMGetElementType(LLVMTypeOf(addr_val.value));
}

lbValue lb_emit_load(lbProcedure *p, lbValue value) {
	lbModule *m = p->module;
	GB_ASSERT(value.value != nullptr);
	GB_ASSERT(is_type_pointer(value.type));
	Type *t = type_deref(value.type);
	LLVMValueRef v = LLVMBuildLoad2(p->builder, llvm_addr_type(value), value.value, "");
	return lbValue{v, t};
}

lbValue lb_addr_load(lbProcedure *p, lbAddr const &addr) {
	GB_ASSERT(addr.addr.value != nullptr);


	if (addr.kind == lbAddr_RelativePointer) {
		Type *rel_ptr = base_type(lb_addr_type(addr));
		GB_ASSERT(rel_ptr->kind == Type_RelativePointer);

		lbValue ptr = lb_emit_conv(p, addr.addr, t_uintptr);
		lbValue offset = lb_emit_conv(p, ptr, alloc_type_pointer(rel_ptr->RelativePointer.base_integer));
		offset = lb_emit_load(p, offset);


		if (!is_type_unsigned(rel_ptr->RelativePointer.base_integer)) {
			offset = lb_emit_conv(p, offset, t_i64);
		}
		offset = lb_emit_conv(p, offset, t_uintptr);
		lbValue absolute_ptr = lb_emit_arith(p, Token_Add, ptr, offset, t_uintptr);
		absolute_ptr = lb_emit_conv(p, absolute_ptr, rel_ptr->RelativePointer.pointer_type);

		lbValue cond = lb_emit_comp(p, Token_CmpEq, offset, lb_const_nil(p->module, rel_ptr->RelativePointer.base_integer));

		// NOTE(bill): nil check
		lbValue nil_ptr = lb_const_nil(p->module, rel_ptr->RelativePointer.pointer_type);
		lbValue final_ptr = {};
		final_ptr.type = absolute_ptr.type;
		final_ptr.value = LLVMBuildSelect(p->builder, cond.value, nil_ptr.value, absolute_ptr.value, "");

		return lb_emit_load(p, final_ptr);

	} else if (addr.kind == lbAddr_RelativeSlice) {
		Type *rel_ptr = base_type(lb_addr_type(addr));
		GB_ASSERT(rel_ptr->kind == Type_RelativeSlice);

		lbValue offset_ptr = lb_emit_struct_ep(p, addr.addr, 0);
		lbValue ptr = lb_emit_conv(p, offset_ptr, t_uintptr);
		lbValue offset = lb_emit_load(p, offset_ptr);


		if (!is_type_unsigned(rel_ptr->RelativeSlice.base_integer)) {
			offset = lb_emit_conv(p, offset, t_i64);
		}
		offset = lb_emit_conv(p, offset, t_uintptr);
		lbValue absolute_ptr = lb_emit_arith(p, Token_Add, ptr, offset, t_uintptr);

		Type *slice_type = base_type(rel_ptr->RelativeSlice.slice_type);
		GB_ASSERT(rel_ptr->RelativeSlice.slice_type->kind == Type_Slice);
		Type *slice_elem = slice_type->Slice.elem;
		Type *slice_elem_ptr = alloc_type_pointer(slice_elem);

		absolute_ptr = lb_emit_conv(p, absolute_ptr, slice_elem_ptr);

		lbValue cond = lb_emit_comp(p, Token_CmpEq, offset, lb_const_nil(p->module, rel_ptr->RelativeSlice.base_integer));

		// NOTE(bill): nil check
		lbValue nil_ptr = lb_const_nil(p->module, slice_elem_ptr);
		lbValue data = {};
		data.type = absolute_ptr.type;
		data.value = LLVMBuildSelect(p->builder, cond.value, nil_ptr.value, absolute_ptr.value, "");

		lbValue len = lb_emit_load(p, lb_emit_struct_ep(p, addr.addr, 1));
		len = lb_emit_conv(p, len, t_int);

		lbAddr slice = lb_add_local_generated(p, slice_type, false);
		lb_fill_slice(p, slice, data, len);
		return lb_addr_load(p, slice);


	} else if (addr.kind == lbAddr_Map) {
		Type *map_type = base_type(addr.map.type);
		lbAddr v = lb_add_local_generated(p, map_type->Map.lookup_result_type, true);
		lbValue h = lb_gen_map_header(p, addr.addr, map_type);
		lbValue key = lb_gen_map_hash(p, addr.map.key, map_type->Map.key);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = h;
		args[1] = key;

		lbValue ptr = lb_emit_runtime_call(p, "__dynamic_map_get", args);
		lbValue ok = lb_emit_conv(p, lb_emit_comp_against_nil(p, Token_NotEq, ptr), t_bool);
		lb_emit_store(p, lb_emit_struct_ep(p, v.addr, 1), ok);

		lbBlock *then = lb_create_block(p, "map.get.then");
		lbBlock *done = lb_create_block(p, "map.get.done");
		lb_emit_if(p, ok, then, done);
		lb_start_block(p, then);
		{
			// TODO(bill): mem copy it instead?
			lbValue gep0 = lb_emit_struct_ep(p, v.addr, 0);
			lbValue value = lb_emit_conv(p, ptr, gep0.type);
			lb_emit_store(p, gep0, lb_emit_load(p, value));
		}
		lb_emit_jump(p, done);
		lb_start_block(p, done);


		if (is_type_tuple(addr.map.result)) {
			return lb_addr_load(p, v);
		} else {
			lbValue single = lb_emit_struct_ep(p, v.addr, 0);
			return lb_emit_load(p, single);
		}
	} else if (addr.kind == lbAddr_Context) {
		lbValue a = addr.addr;
		for_array(i, p->context_stack) {
			lbContextData *ctx_data = &p->context_stack[i];
			if (ctx_data->ctx.addr.value == a.value) {
				ctx_data->uses += 1;
				break;
			}
		}
		a.value = LLVMBuildPointerCast(p->builder, a.value, lb_type(p->module, t_context_ptr), "");

		if (addr.ctx.sel.index.count > 0) {
			lbValue b = lb_emit_deep_field_gep(p, a, addr.ctx.sel);
			return lb_emit_load(p, b);
		} else {
			return lb_emit_load(p, a);
		}
	} else if (addr.kind == lbAddr_SoaVariable) {
		Type *t = type_deref(addr.addr.type);
		t = base_type(t);
		GB_ASSERT(t->kind == Type_Struct && t->Struct.soa_kind != StructSoa_None);
		Type *elem = t->Struct.soa_elem;

		lbValue len = {};
		if (t->Struct.soa_kind == StructSoa_Fixed) {
			len = lb_const_int(p->module, t_int, t->Struct.soa_count);
		} else {
			lbValue v = lb_emit_load(p, addr.addr);
			len = lb_soa_struct_len(p, v);
		}

		lbAddr res = lb_add_local_generated(p, elem, true);

		if (addr.soa.index_expr != nullptr && (!lb_is_const(addr.soa.index) || t->Struct.soa_kind != StructSoa_Fixed)) {
			lb_emit_bounds_check(p, ast_token(addr.soa.index_expr), addr.soa.index, len);
		}

		if (t->Struct.soa_kind == StructSoa_Fixed) {
			for_array(i, t->Struct.fields) {
				Entity *field = t->Struct.fields[i];
				Type *base_type = field->type;
				GB_ASSERT(base_type->kind == Type_Array);

				lbValue dst = lb_emit_struct_ep(p, res.addr, cast(i32)i);
				lbValue src_ptr = lb_emit_struct_ep(p, addr.addr, cast(i32)i);
				src_ptr = lb_emit_array_ep(p, src_ptr, addr.soa.index);
				lbValue src = lb_emit_load(p, src_ptr);
				lb_emit_store(p, dst, src);
			}
		} else {
			isize field_count = t->Struct.fields.count;
			if (t->Struct.soa_kind == StructSoa_Slice) {
				field_count -= 1;
			} else if (t->Struct.soa_kind == StructSoa_Dynamic) {
				field_count -= 3;
			}
			for (isize i = 0; i < field_count; i++) {
				Entity *field = t->Struct.fields[i];
				Type *base_type = field->type;
				GB_ASSERT(base_type->kind == Type_Pointer);
				Type *elem = base_type->Pointer.elem;

				lbValue dst = lb_emit_struct_ep(p, res.addr, cast(i32)i);
				lbValue src_ptr = lb_emit_struct_ep(p, addr.addr, cast(i32)i);
				lbValue src = lb_emit_load(p, src_ptr);
				src = lb_emit_ptr_offset(p, src, addr.soa.index);
				src = lb_emit_load(p, src);
				lb_emit_store(p, dst, src);
			}
		}

		return lb_addr_load(p, res);
	} else if (addr.kind == lbAddr_Swizzle) {
		Type *array_type = base_type(addr.swizzle.type);
		GB_ASSERT(array_type->kind == Type_Array);

		unsigned res_align = cast(unsigned)type_align_of(addr.swizzle.type);

		static u8 const ordered_indices[4] = {0, 1, 2, 3};
		if (gb_memcompare(ordered_indices, addr.swizzle.indices, addr.swizzle.count) == 0) {
			if (lb_try_update_alignment(addr.addr, res_align)) {
				Type *pt = alloc_type_pointer(addr.swizzle.type);
				lbValue res = {};
				res.value = LLVMBuildPointerCast(p->builder, addr.addr.value, lb_type(p->module, pt), "");
				res.type = pt;
				return lb_emit_load(p, res);
			}
		}

		Type *elem_type = base_type(array_type->Array.elem);
		lbAddr res = lb_add_local_generated(p, addr.swizzle.type, false);
		lbValue ptr = lb_addr_get_ptr(p, res);
		GB_ASSERT(is_type_pointer(ptr.type));

		LLVMTypeRef vector_type = nullptr;
		if (lb_try_vector_cast(p->module, addr.addr, &vector_type)) {
			LLVMSetAlignment(res.addr.value, cast(unsigned)lb_alignof(vector_type));

			LLVMValueRef vp = LLVMBuildPointerCast(p->builder, addr.addr.value, LLVMPointerType(vector_type, 0), "");
			LLVMValueRef v = LLVMBuildLoad2(p->builder, vector_type, vp, "");
			LLVMValueRef scalars[4] = {};
			for (u8 i = 0; i < addr.swizzle.count; i++) {
				scalars[i] = LLVMConstInt(lb_type(p->module, t_u32), addr.swizzle.indices[i], false);
			}
			LLVMValueRef mask = LLVMConstVector(scalars, addr.swizzle.count);
			LLVMValueRef sv = LLVMBuildShuffleVector(p->builder, v, LLVMGetUndef(vector_type), mask, "");

			LLVMValueRef dst = LLVMBuildPointerCast(p->builder, ptr.value, LLVMPointerType(LLVMTypeOf(sv), 0), "");
			LLVMBuildStore(p->builder, sv, dst);
		} else {
			for (u8 i = 0; i < addr.swizzle.count; i++) {
				u8 index = addr.swizzle.indices[i];
				lbValue dst = lb_emit_array_epi(p, ptr, i);
				lbValue src = lb_emit_array_epi(p, addr.addr, index);
				lb_emit_store(p, dst, lb_emit_load(p, src));
			}
		}
		return lb_addr_load(p, res);
	}

	if (is_type_proc(addr.addr.type)) {
		return addr.addr;
	}
	return lb_emit_load(p, addr.addr);
}

lbValue lb_const_union_tag(lbModule *m, Type *u, Type *v) {
	return lb_const_value(m, union_tag_type(u), exact_value_i64(union_variant_index(u, v)));
}

lbValue lb_emit_union_tag_ptr(lbProcedure *p, lbValue u) {
	Type *t = u.type;
	GB_ASSERT_MSG(is_type_pointer(t) &&
	              is_type_union(type_deref(t)), "%s", type_to_string(t));
	Type *ut = type_deref(t);

	GB_ASSERT(!is_type_union_maybe_pointer_original_alignment(ut));
	GB_ASSERT(!is_type_union_maybe_pointer(ut));
	GB_ASSERT(type_size_of(ut) > 0);

	Type *tag_type = union_tag_type(ut);

	LLVMTypeRef uvt = LLVMGetElementType(LLVMTypeOf(u.value));
	unsigned element_count = LLVMCountStructElementTypes(uvt);
	GB_ASSERT_MSG(element_count == 3, "(%s) != (%s)", type_to_string(ut), LLVMPrintTypeToString(uvt));

	lbValue tag_ptr = {};
	tag_ptr.value = LLVMBuildStructGEP(p->builder, u.value, 2, "");
	tag_ptr.type = alloc_type_pointer(tag_type);
	return tag_ptr;
}

lbValue lb_emit_union_tag_value(lbProcedure *p, lbValue u) {
	lbValue ptr = lb_address_from_load_or_generate_local(p, u);
	lbValue tag_ptr = lb_emit_union_tag_ptr(p, ptr);
	return lb_emit_load(p, tag_ptr);
}


void lb_emit_store_union_variant_tag(lbProcedure *p, lbValue parent, Type *variant_type) {
	Type *t = type_deref(parent.type);

	if (is_type_union_maybe_pointer(t) || type_size_of(t) == 0) {
		// No tag needed!
	} else {
		lbValue tag_ptr = lb_emit_union_tag_ptr(p, parent);
		lb_emit_store(p, tag_ptr, lb_const_union_tag(p->module, t, variant_type));
	}
}

void lb_emit_store_union_variant(lbProcedure *p, lbValue parent, lbValue variant, Type *variant_type) {
	lbValue underlying = lb_emit_conv(p, parent, alloc_type_pointer(variant_type));

	lb_emit_store(p, underlying, variant);
	lb_emit_store_union_variant_tag(p, parent, variant_type);
}


void lb_clone_struct_type(LLVMTypeRef dst, LLVMTypeRef src) {
	unsigned field_count = LLVMCountStructElementTypes(src);
	LLVMTypeRef *fields = gb_alloc_array(temporary_allocator(), LLVMTypeRef, field_count);
	LLVMGetStructElementTypes(src, fields);
	LLVMStructSetBody(dst, fields, field_count, LLVMIsPackedStruct(src));
}

LLVMTypeRef lb_alignment_prefix_type_hack(lbModule *m, i64 alignment) {
	switch (alignment) {
	case 1:
		return LLVMArrayType(lb_type(m, t_u8), 0);
	case 2:
		return LLVMArrayType(lb_type(m, t_u16), 0);
	case 4:
		return LLVMArrayType(lb_type(m, t_u32), 0);
	case 8:
		return LLVMArrayType(lb_type(m, t_u64), 0);
	case 16:
		return LLVMArrayType(LLVMVectorType(lb_type(m, t_u32), 4), 0);
	default:
		GB_PANIC("Invalid alignment %d", cast(i32)alignment);
		break;
	}
	return nullptr;
}

bool lb_is_elem_const(Ast *elem, Type *elem_type) {
	if (!elem_type_can_be_constant(elem_type)) {
		return false;
	}
	if (elem->kind == Ast_FieldValue) {
		elem = elem->FieldValue.value;
	}
	TypeAndValue tav = type_and_value_of_expr(elem);
	GB_ASSERT_MSG(tav.mode != Addressing_Invalid, "%s %s", expr_to_string(elem), type_to_string(tav.type));
	return tav.value.kind != ExactValue_Invalid;
}

String lb_mangle_name(lbModule *m, Entity *e) {
	String name = e->token.string;

	AstPackage *pkg = e->pkg;
	GB_ASSERT_MSG(pkg != nullptr, "Missing package for '%.*s'", LIT(name));
	String pkgn = pkg->name;
	GB_ASSERT(!rune_is_digit(pkgn[0]));
	if (pkgn == "llvm") {
		pkgn = str_lit("llvm$");
	}

	isize max_len = pkgn.len + 1 + name.len + 1;
	bool require_suffix_id = is_type_polymorphic(e->type, true);

	if ((e->scope->flags & (ScopeFlag_File | ScopeFlag_Pkg)) == 0) {
		require_suffix_id = true;
	} else if (is_blank_ident(e->token)) {
		require_suffix_id = true;
	}if (e->flags & EntityFlag_NotExported) {
		require_suffix_id = true;
	}

	if (require_suffix_id) {
		max_len += 21;
	}

	char *new_name = gb_alloc_array(permanent_allocator(), char, max_len);
	isize new_name_len = gb_snprintf(
		new_name, max_len,
		"%.*s.%.*s", LIT(pkgn), LIT(name)
	);
	if (require_suffix_id) {
		char *str = new_name + new_name_len-1;
		isize len = max_len-new_name_len;
		isize extra = gb_snprintf(str, len, "-%llu", cast(unsigned long long)e->id);
		new_name_len += extra-1;
	}

	String mangled_name = make_string((u8 const *)new_name, new_name_len-1);
	return mangled_name;
}

String lb_set_nested_type_name_ir_mangled_name(Entity *e, lbProcedure *p) {
	// NOTE(bill, 2020-03-08): A polymorphic procedure may take a nested type declaration
	// and as a result, the declaration does not have time to determine what it should be

	GB_ASSERT(e != nullptr && e->kind == Entity_TypeName);
	if (e->TypeName.ir_mangled_name.len != 0)  {
		return e->TypeName.ir_mangled_name;
	}
	GB_ASSERT((e->scope->flags & ScopeFlag_File) == 0);

	if (p == nullptr) {
		Entity *proc = nullptr;
		if (e->parent_proc_decl != nullptr) {
			proc = e->parent_proc_decl->entity;
		} else {
			Scope *scope = e->scope;
			while (scope != nullptr && (scope->flags & ScopeFlag_Proc) == 0) {
				scope = scope->parent;
			}
			GB_ASSERT(scope != nullptr);
			GB_ASSERT(scope->flags & ScopeFlag_Proc);
			proc = scope->procedure_entity;
		}
		GB_ASSERT(proc->kind == Entity_Procedure);
		if (proc->code_gen_procedure != nullptr) {
			p = proc->code_gen_procedure;
		}
	}

	// NOTE(bill): Generate a new name
	// parent_proc.name-guid
	String ts_name = e->token.string;

	if (p != nullptr) {
		isize name_len = p->name.len + 1 + ts_name.len + 1 + 10 + 1;
		char *name_text = gb_alloc_array(permanent_allocator(), char, name_len);
		u32 guid = ++p->module->nested_type_name_guid;
		name_len = gb_snprintf(name_text, name_len, "%.*s.%.*s-%u", LIT(p->name), LIT(ts_name), guid);

		String name = make_string(cast(u8 *)name_text, name_len-1);
		e->TypeName.ir_mangled_name = name;
		return name;
	} else {
		// NOTE(bill): a nested type be required before its parameter procedure exists. Just give it a temp name for now
		isize name_len = 9 + 1 + ts_name.len + 1 + 10 + 1;
		char *name_text = gb_alloc_array(permanent_allocator(), char, name_len);
		static u32 guid = 0;
		guid += 1;
		name_len = gb_snprintf(name_text, name_len, "_internal.%.*s-%u", LIT(ts_name), guid);

		String name = make_string(cast(u8 *)name_text, name_len-1);
		e->TypeName.ir_mangled_name = name;
		return name;
	}
}


String lb_get_entity_name(lbModule *m, Entity *e, String default_name) {
	if (e != nullptr && e->kind == Entity_TypeName && e->TypeName.ir_mangled_name.len != 0) {
		return e->TypeName.ir_mangled_name;
	}
	GB_ASSERT(e != nullptr);

	if (e->pkg == nullptr) {
		return e->token.string;
	}

	if (e->kind == Entity_TypeName && (e->scope->flags & ScopeFlag_File) == 0) {
		return lb_set_nested_type_name_ir_mangled_name(e, nullptr);
	}

	String name = {};

	bool no_name_mangle = false;

	if (e->kind == Entity_Variable) {
		bool is_foreign = e->Variable.is_foreign;
		bool is_export  = e->Variable.is_export;
		no_name_mangle = e->Variable.link_name.len > 0 || is_foreign || is_export;
		if (e->Variable.link_name.len > 0) {
			return e->Variable.link_name;
		}
	} else if (e->kind == Entity_Procedure && e->Procedure.link_name.len > 0) {
		return e->Procedure.link_name;
	} else if (e->kind == Entity_Procedure && e->Procedure.is_export) {
		no_name_mangle = true;
	}

	if (!no_name_mangle) {
		name = lb_mangle_name(m, e);
	}
	if (name.len == 0) {
		name = e->token.string;
	}

	if (e->kind == Entity_TypeName) {
		e->TypeName.ir_mangled_name = name;
	} else if (e->kind == Entity_Procedure) {
		e->Procedure.link_name = name;
	}

	return name;
}

LLVMTypeRef lb_type_internal(lbModule *m, Type *type) {
	Type *original_type = type;

	LLVMContextRef ctx = m->ctx;
	i64 size = type_size_of(type); // Check size

	GB_ASSERT(type != t_invalid);

	switch (type->kind) {
	case Type_Basic:
		switch (type->Basic.kind) {
		case Basic_llvm_bool: return LLVMInt1TypeInContext(ctx);
		case Basic_bool:      return LLVMInt8TypeInContext(ctx);
		case Basic_b8:        return LLVMInt8TypeInContext(ctx);
		case Basic_b16:       return LLVMInt16TypeInContext(ctx);
		case Basic_b32:       return LLVMInt32TypeInContext(ctx);
		case Basic_b64:       return LLVMInt64TypeInContext(ctx);

		case Basic_i8:   return LLVMInt8TypeInContext(ctx);
		case Basic_u8:   return LLVMInt8TypeInContext(ctx);
		case Basic_i16:  return LLVMInt16TypeInContext(ctx);
		case Basic_u16:  return LLVMInt16TypeInContext(ctx);
		case Basic_i32:  return LLVMInt32TypeInContext(ctx);
		case Basic_u32:  return LLVMInt32TypeInContext(ctx);
		case Basic_i64:  return LLVMInt64TypeInContext(ctx);
		case Basic_u64:  return LLVMInt64TypeInContext(ctx);
		case Basic_i128: return LLVMInt128TypeInContext(ctx);
		case Basic_u128: return LLVMInt128TypeInContext(ctx);

		case Basic_rune: return LLVMInt32TypeInContext(ctx);


		case Basic_f16: return LLVMHalfTypeInContext(ctx);
		case Basic_f32: return LLVMFloatTypeInContext(ctx);
		case Basic_f64: return LLVMDoubleTypeInContext(ctx);

		case Basic_f16le: return LLVMHalfTypeInContext(ctx);
		case Basic_f32le: return LLVMFloatTypeInContext(ctx);
		case Basic_f64le: return LLVMDoubleTypeInContext(ctx);

		case Basic_f16be: return LLVMHalfTypeInContext(ctx);
		case Basic_f32be: return LLVMFloatTypeInContext(ctx);
		case Basic_f64be: return LLVMDoubleTypeInContext(ctx);

		case Basic_complex32:
			{
				char const *name = "..complex32";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[2] = {
					lb_type(m, t_f16),
					lb_type(m, t_f16),
				};
				LLVMStructSetBody(type, fields, 2, false);
				return type;
			}
		case Basic_complex64:
			{
				char const *name = "..complex64";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[2] = {
					lb_type(m, t_f32),
					lb_type(m, t_f32),
				};
				LLVMStructSetBody(type, fields, 2, false);
				return type;
			}
		case Basic_complex128:
			{
				char const *name = "..complex128";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[2] = {
					lb_type(m, t_f64),
					lb_type(m, t_f64),
				};
				LLVMStructSetBody(type, fields, 2, false);
				return type;
			}

		case Basic_quaternion64:
			{
				char const *name = "..quaternion64";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[4] = {
					lb_type(m, t_f16),
					lb_type(m, t_f16),
					lb_type(m, t_f16),
					lb_type(m, t_f16),
				};
				LLVMStructSetBody(type, fields, 4, false);
				return type;
			}
		case Basic_quaternion128:
			{
				char const *name = "..quaternion128";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[4] = {
					lb_type(m, t_f32),
					lb_type(m, t_f32),
					lb_type(m, t_f32),
					lb_type(m, t_f32),
				};
				LLVMStructSetBody(type, fields, 4, false);
				return type;
			}
		case Basic_quaternion256:
			{
				char const *name = "..quaternion256";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[4] = {
					lb_type(m, t_f64),
					lb_type(m, t_f64),
					lb_type(m, t_f64),
					lb_type(m, t_f64),
				};
				LLVMStructSetBody(type, fields, 4, false);
				return type;
			}

		case Basic_int:  return LLVMIntTypeInContext(ctx, 8*cast(unsigned)build_context.word_size);
		case Basic_uint: return LLVMIntTypeInContext(ctx, 8*cast(unsigned)build_context.word_size);

		case Basic_uintptr: return LLVMIntTypeInContext(ctx, 8*cast(unsigned)build_context.word_size);

		case Basic_rawptr: return LLVMPointerType(LLVMInt8TypeInContext(ctx), 0);
		case Basic_string:
			{
				char const *name = "..string";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[2] = {
					LLVMPointerType(lb_type(m, t_u8), 0),
					lb_type(m, t_int),
				};
				LLVMStructSetBody(type, fields, 2, false);
				return type;
			}
		case Basic_cstring: return LLVMPointerType(LLVMInt8TypeInContext(ctx), 0);
		case Basic_any:
			{
				char const *name = "..any";
				LLVMTypeRef type = LLVMGetTypeByName(m->mod, name);
				if (type != nullptr) {
					return type;
				}
				type = LLVMStructCreateNamed(ctx, name);
				LLVMTypeRef fields[2] = {
					lb_type(m, t_rawptr),
					lb_type(m, t_typeid),
				};
				LLVMStructSetBody(type, fields, 2, false);
				return type;
			}

		case Basic_typeid: return LLVMIntTypeInContext(m->ctx, 8*cast(unsigned)build_context.word_size);

		// Endian Specific Types
		case Basic_i16le:  return LLVMInt16TypeInContext(ctx);
		case Basic_u16le:  return LLVMInt16TypeInContext(ctx);
		case Basic_i32le:  return LLVMInt32TypeInContext(ctx);
		case Basic_u32le:  return LLVMInt32TypeInContext(ctx);
		case Basic_i64le:  return LLVMInt64TypeInContext(ctx);
		case Basic_u64le:  return LLVMInt64TypeInContext(ctx);
		case Basic_i128le: return LLVMInt128TypeInContext(ctx);
		case Basic_u128le: return LLVMInt128TypeInContext(ctx);

		case Basic_i16be:  return LLVMInt16TypeInContext(ctx);
		case Basic_u16be:  return LLVMInt16TypeInContext(ctx);
		case Basic_i32be:  return LLVMInt32TypeInContext(ctx);
		case Basic_u32be:  return LLVMInt32TypeInContext(ctx);
		case Basic_i64be:  return LLVMInt64TypeInContext(ctx);
		case Basic_u64be:  return LLVMInt64TypeInContext(ctx);
		case Basic_i128be: return LLVMInt128TypeInContext(ctx);
		case Basic_u128be: return LLVMInt128TypeInContext(ctx);

		// Untyped types
		case Basic_UntypedBool:       GB_PANIC("Basic_UntypedBool"); break;
		case Basic_UntypedInteger:    GB_PANIC("Basic_UntypedInteger"); break;
		case Basic_UntypedFloat:      GB_PANIC("Basic_UntypedFloat"); break;
		case Basic_UntypedComplex:    GB_PANIC("Basic_UntypedComplex"); break;
		case Basic_UntypedQuaternion: GB_PANIC("Basic_UntypedQuaternion"); break;
		case Basic_UntypedString:     GB_PANIC("Basic_UntypedString"); break;
		case Basic_UntypedRune:       GB_PANIC("Basic_UntypedRune"); break;
		case Basic_UntypedNil:        GB_PANIC("Basic_UntypedNil"); break;
		case Basic_UntypedUndef:      GB_PANIC("Basic_UntypedUndef"); break;
		}
		break;
	case Type_Named:
		{
			Type *base = base_type(type->Named.base);

			switch (base->kind) {
			case Type_Basic:
				return lb_type_internal(m, base);

			case Type_Named:
			case Type_Generic:
				GB_PANIC("INVALID TYPE");
				break;

			case Type_Pointer:
			case Type_Array:
			case Type_EnumeratedArray:
			case Type_Slice:
			case Type_DynamicArray:
			case Type_Map:
			case Type_Enum:
			case Type_BitSet:
			case Type_SimdVector:
				return lb_type_internal(m, base);

			// TODO(bill): Deal with this correctly. Can this be named?
			case Type_Proc:
				return lb_type_internal(m, base);

			case Type_Tuple:
				return lb_type_internal(m, base);
			}

			LLVMTypeRef *found = map_get(&m->types, hash_type(base));
			if (found) {
				LLVMTypeKind kind = LLVMGetTypeKind(*found);
				if (kind == LLVMStructTypeKind) {
					char const *name = alloc_cstring(permanent_allocator(), lb_get_entity_name(m, type->Named.type_name));
					LLVMTypeRef llvm_type = LLVMGetTypeByName(m->mod, name);
					if (llvm_type != nullptr) {
						return llvm_type;
					}
					llvm_type = LLVMStructCreateNamed(ctx, name);
					map_set(&m->types, hash_type(type), llvm_type);
					lb_clone_struct_type(llvm_type, *found);
					return llvm_type;
				}
			}

			switch (base->kind) {
			case Type_Struct:
			case Type_Union:
				{
					char const *name = alloc_cstring(permanent_allocator(), lb_get_entity_name(m, type->Named.type_name));
					LLVMTypeRef llvm_type = LLVMGetTypeByName(m->mod, name);
					if (llvm_type != nullptr) {
						return llvm_type;
					}
					llvm_type = LLVMStructCreateNamed(ctx, name);
					map_set(&m->types, hash_type(type), llvm_type);
					lb_clone_struct_type(llvm_type, lb_type(m, base));
					return llvm_type;
				}
			}


			return lb_type_internal(m, base);
		}

	case Type_Pointer:
		return LLVMPointerType(lb_type(m, type_deref(type)), 0);

	case Type_Array: {
		m->internal_type_level -= 1;
		LLVMTypeRef t = LLVMArrayType(lb_type(m, type->Array.elem), cast(unsigned)type->Array.count);
		m->internal_type_level += 1;
		return t;
	}

	case Type_EnumeratedArray: {
		m->internal_type_level -= 1;
		LLVMTypeRef t = LLVMArrayType(lb_type(m, type->EnumeratedArray.elem), cast(unsigned)type->EnumeratedArray.count);
		m->internal_type_level += 1;
		return t;
	}

	case Type_Slice:
		{
			LLVMTypeRef fields[2] = {
				LLVMPointerType(lb_type(m, type->Slice.elem), 0), // data
				lb_type(m, t_int), // len
			};
			return LLVMStructTypeInContext(ctx, fields, 2, false);
		}
		break;

	case Type_DynamicArray:
		{
			LLVMTypeRef fields[4] = {
				LLVMPointerType(lb_type(m, type->DynamicArray.elem), 0), // data
				lb_type(m, t_int), // len
				lb_type(m, t_int), // cap
				lb_type(m, t_allocator), // allocator
			};
			return LLVMStructTypeInContext(ctx, fields, 4, false);
		}
		break;

	case Type_Map:
		return lb_type(m, type->Map.internal_type);

	case Type_Struct:
		{
			if (type->Struct.is_raw_union) {
				unsigned field_count = 2;
				LLVMTypeRef *fields = gb_alloc_array(permanent_allocator(), LLVMTypeRef, field_count);
				i64 alignment = type_align_of(type);
				unsigned size_of_union = cast(unsigned)type_size_of(type);
				fields[0] = lb_alignment_prefix_type_hack(m, alignment);
				fields[1] = LLVMArrayType(lb_type(m, t_u8), size_of_union);
				return LLVMStructTypeInContext(ctx, fields, field_count, false);
			}

			isize offset = 0;
			if (type->Struct.custom_align > 0) {
				offset = 1;
			}

			m->internal_type_level += 1;
			defer (m->internal_type_level -= 1);

			unsigned field_count = cast(unsigned)(type->Struct.fields.count + offset);
			LLVMTypeRef *fields = gb_alloc_array(temporary_allocator(), LLVMTypeRef, field_count);

			for_array(i, type->Struct.fields) {
				Entity *field = type->Struct.fields[i];
				fields[i+offset] = lb_type(m, field->type);
			}


			if (type->Struct.custom_align > 0) {
				fields[0] = lb_alignment_prefix_type_hack(m, type->Struct.custom_align);
			}

			return LLVMStructTypeInContext(ctx, fields, field_count, type->Struct.is_packed);
		}
		break;

	case Type_Union:
		if (type->Union.variants.count == 0) {
			return LLVMStructTypeInContext(ctx, nullptr, 0, false);
		} else {
			// NOTE(bill): The zero size array is used to fix the alignment used in a structure as
			// LLVM takes the first element's alignment as the entire alignment (like C)
			i64 align = type_align_of(type);
			i64 size = type_size_of(type);

			if (is_type_union_maybe_pointer_original_alignment(type)) {
				LLVMTypeRef fields[1] = {lb_type(m, type->Union.variants[0])};
				return LLVMStructTypeInContext(ctx, fields, 1, false);
			}

			unsigned block_size = cast(unsigned)type->Union.variant_block_size;

			LLVMTypeRef fields[3] = {};
			unsigned field_count = 1;
			fields[0] = lb_alignment_prefix_type_hack(m, align);
			if (is_type_union_maybe_pointer(type)) {
				field_count += 1;
				fields[1] = lb_type(m, type->Union.variants[0]);
			} else {
				field_count += 2;
				if (block_size == align) {
					fields[1] = LLVMIntTypeInContext(m->ctx, 8*block_size);
				} else {
					fields[1] = LLVMArrayType(lb_type(m, t_u8), block_size);
				}
				fields[2] = lb_type(m, union_tag_type(type));
			}

			return LLVMStructTypeInContext(ctx, fields, field_count, false);
		}
		break;

	case Type_Enum:
		return lb_type(m, base_enum_type(type));

	case Type_Tuple:
		if (type->Tuple.variables.count == 1) {
			return lb_type(m, type->Tuple.variables[0]->type);
		} else {
			unsigned field_count = cast(unsigned)(type->Tuple.variables.count);
			LLVMTypeRef *fields = gb_alloc_array(temporary_allocator(), LLVMTypeRef, field_count);

			for_array(i, type->Tuple.variables) {
				Entity *field = type->Tuple.variables[i];

				LLVMTypeRef param_type = nullptr;
				param_type = lb_type(m, field->type);

				fields[i] = param_type;
			}

			return LLVMStructTypeInContext(ctx, fields, field_count, type->Tuple.is_packed);
		}

	case Type_Proc:
		// if (m->internal_type_level > 256) { // TODO HACK(bill): is this really enough?
		if (m->internal_type_level > 1) { // TODO HACK(bill): is this really enough?
			return LLVMPointerType(LLVMIntTypeInContext(m->ctx, 8), 0);
		} else {
			unsigned param_count = 0;
			if (type->Proc.calling_convention == ProcCC_Odin) {
				param_count += 1;
			}

			if (type->Proc.param_count != 0) {
				GB_ASSERT(type->Proc.params->kind == Type_Tuple);
				for_array(i, type->Proc.params->Tuple.variables) {
					Entity *e = type->Proc.params->Tuple.variables[i];
					if (e->kind != Entity_Variable) {
						continue;
					}
					if (e->flags & EntityFlag_CVarArg) {
						continue;
					}
					param_count += 1;
				}
			}
			m->internal_type_level += 1;
			defer (m->internal_type_level -= 1);

			LLVMTypeRef ret = nullptr;
			LLVMTypeRef *params = gb_alloc_array(heap_allocator(), LLVMTypeRef, param_count);
			if (type->Proc.result_count != 0) {
				Type *single_ret = reduce_tuple_to_single_type(type->Proc.results);
				ret = lb_type(m, single_ret);
				if (ret != nullptr) {
					if (is_type_boolean(single_ret) &&
					    is_calling_convention_none(type->Proc.calling_convention) &&
					    type_size_of(single_ret) <= 1) {
						ret = LLVMInt1TypeInContext(m->ctx);
					}
				}
			}

			isize param_index = 0;
			if (type->Proc.param_count != 0) {
				GB_ASSERT(type->Proc.params->kind == Type_Tuple);
				for_array(i, type->Proc.params->Tuple.variables) {
					Entity *e = type->Proc.params->Tuple.variables[i];
					if (e->kind != Entity_Variable) {
						continue;
					}
					if (e->flags & EntityFlag_CVarArg) {
						continue;
					}

					Type *e_type = reduce_tuple_to_single_type(e->type);

					LLVMTypeRef param_type = nullptr;
					if (is_type_boolean(e_type) &&
					    type_size_of(e_type) <= 1) {
						param_type = LLVMInt1TypeInContext(m->ctx);
					} else {
						if (is_type_proc(e_type)) {
							param_type = lb_type(m, t_rawptr);
						} else {
							param_type = lb_type(m, e_type);
						}
					}

					params[param_index++] = param_type;
				}
			}
			if (param_index < param_count) {
				params[param_index++] = lb_type(m, t_rawptr);
			}
			GB_ASSERT(param_index == param_count);

			lbFunctionType *ft = lb_get_abi_info(m->ctx, params, param_count, ret, ret != nullptr, type->Proc.calling_convention);
			{
				for_array(j, ft->args) {
					auto arg = ft->args[j];
					GB_ASSERT_MSG(LLVMGetTypeContext(arg.type) == ft->ctx,
					              "\n\t%s %td/%td"
					              "\n\tArgTypeCtx: %p\n\tCurrentCtx: %p\n\tGlobalCtx:  %p",
					              LLVMPrintTypeToString(arg.type),
					              j, ft->args.count,
					              LLVMGetTypeContext(arg.type), ft->ctx, LLVMGetGlobalContext());
				}
				GB_ASSERT_MSG(LLVMGetTypeContext(ft->ret.type) == ft->ctx,
				              "\n\t%s"
				              "\n\tRetTypeCtx: %p\n\tCurrentCtx: %p\n\tGlobalCtx:  %p",
				              LLVMPrintTypeToString(ft->ret.type),
				              LLVMGetTypeContext(ft->ret.type), ft->ctx, LLVMGetGlobalContext());
			}

			map_set(&m->function_type_map, hash_type(type), ft);
			LLVMTypeRef new_abi_fn_ptr_type = lb_function_type_to_llvm_ptr(ft, type->Proc.c_vararg);
			LLVMTypeRef new_abi_fn_type = LLVMGetElementType(new_abi_fn_ptr_type);

			GB_ASSERT_MSG(LLVMGetTypeContext(new_abi_fn_type) == m->ctx,
			              "\n\tFuncTypeCtx: %p\n\tCurrentCtx:  %p\n\tGlobalCtx:   %p",
			              LLVMGetTypeContext(new_abi_fn_type), m->ctx, LLVMGetGlobalContext());

			return new_abi_fn_ptr_type;
		}

		break;
	case Type_BitSet:
		{
			Type *ut = bit_set_to_int(type);
			return lb_type(m, ut);
		}

	case Type_SimdVector:
		return LLVMVectorType(lb_type(m, type->SimdVector.elem), cast(unsigned)type->SimdVector.count);

	case Type_RelativePointer:
		return lb_type_internal(m, type->RelativePointer.base_integer);

	case Type_RelativeSlice:
		{
			LLVMTypeRef base_integer = lb_type_internal(m, type->RelativeSlice.base_integer);

			unsigned field_count = 2;
			LLVMTypeRef *fields = gb_alloc_array(heap_allocator(), LLVMTypeRef, field_count);
			fields[0] = base_integer;
			fields[1] = base_integer;
			return LLVMStructTypeInContext(ctx, fields, field_count, false);
		}
	}

	GB_PANIC("Invalid type %s", type_to_string(type));
	return LLVMInt32TypeInContext(ctx);
}

LLVMTypeRef lb_type(lbModule *m, Type *type) {
	type = default_type(type);

	LLVMTypeRef *found = map_get(&m->types, hash_type(type));
	if (found) {
		return *found;
	}

	LLVMTypeRef llvm_type = nullptr;

	m->internal_type_level += 1;
	llvm_type = lb_type_internal(m, type);
	m->internal_type_level -= 1;
	if (m->internal_type_level == 0) {
		map_set(&m->types, hash_type(type), llvm_type);
		if (is_type_named(type)) {
			map_set(&m->llvm_types, hash_pointer(llvm_type), type);
		}
	}
	return llvm_type;
}

lbFunctionType *lb_get_function_type(lbModule *m, lbProcedure *p, Type *pt) {
	lbFunctionType **ft_found = nullptr;
	ft_found = map_get(&m->function_type_map, hash_type(pt));
	if (!ft_found) {
		LLVMTypeRef llvm_proc_type = lb_type(p->module, pt);
		ft_found = map_get(&m->function_type_map, hash_type(pt));
	}
	GB_ASSERT(ft_found != nullptr);

	return *ft_found;
}


LLVMMetadataRef lb_get_llvm_metadata(lbModule *m, void *key) {
	if (key == nullptr) {
		return nullptr;
	}
	auto found = map_get(&m->debug_values, hash_pointer(key));
	if (found) {
		return *found;
	}
	return nullptr;
}
void lb_set_llvm_metadata(lbModule *m, void *key, LLVMMetadataRef value) {
	if (key != nullptr) {
		map_set(&m->debug_values, hash_pointer(key), value);
	}
}

LLVMMetadataRef lb_get_llvm_file_metadata_from_node(lbModule *m, Ast *node) {
	if (node == nullptr) {
		return nullptr;
	}
	return lb_get_llvm_metadata(m, node->file);
}

LLVMMetadataRef lb_get_current_debug_scope(lbProcedure *p) {
	GB_ASSERT_MSG(p->debug_info != nullptr, "missing debug information for %.*s", LIT(p->name));

	for (isize i = p->scope_stack.count-1; i >= 0; i--) {
		Scope *s = p->scope_stack[i];
		LLVMMetadataRef md = lb_get_llvm_metadata(p->module, s);
		if (md) {
			return md;
		}
	}
	return p->debug_info;
}

LLVMMetadataRef lb_debug_location_from_token_pos(lbProcedure *p, TokenPos pos) {
	LLVMMetadataRef scope = lb_get_current_debug_scope(p);
	GB_ASSERT_MSG(scope != nullptr, "%.*s", LIT(p->name));
	return LLVMDIBuilderCreateDebugLocation(p->module->ctx, cast(unsigned)pos.line, cast(unsigned)pos.column, scope, nullptr);
}
LLVMMetadataRef lb_debug_location_from_ast(lbProcedure *p, Ast *node) {
	GB_ASSERT(node != nullptr);
	return lb_debug_location_from_token_pos(p, ast_token(node).pos);
}

LLVMMetadataRef lb_debug_type_internal_proc(lbModule *m, Type *type) {
	Type *original_type = type;

	LLVMContextRef ctx = m->ctx;
	i64 size = type_size_of(type); // Check size

	GB_ASSERT(type != t_invalid);

	unsigned const word_size = cast(unsigned)build_context.word_size;
	unsigned const word_bits = cast(unsigned)(8*build_context.word_size);

	GB_ASSERT(type->kind == Type_Proc);
	LLVMTypeRef return_type = LLVMVoidTypeInContext(ctx);
	unsigned parameter_count = 1;
	for (i32 i = 0; i < type->Proc.param_count; i++) {
		Entity *e = type->Proc.params->Tuple.variables[i];
		if (e->kind == Entity_Variable) {
			parameter_count += 1;
		}
	}
	LLVMMetadataRef *parameters = gb_alloc_array(permanent_allocator(), LLVMMetadataRef, parameter_count);

	unsigned param_index = 0;
	if (type->Proc.result_count == 0) {
		parameters[param_index++] = nullptr;
	} else {
		parameters[param_index++] = lb_debug_type(m, type->Proc.results);
	}

	LLVMMetadataRef parent_scope = nullptr;
	LLVMMetadataRef scope = nullptr;
	LLVMMetadataRef file = nullptr;

	for (i32 i = 0; i < type->Proc.param_count; i++) {
		Entity *e = type->Proc.params->Tuple.variables[i];
		if (e->kind != Entity_Variable) {
			continue;
		}
		parameters[param_index] = lb_debug_type(m, e->type);
		param_index += 1;
	}

	LLVMDIFlags flags = LLVMDIFlagZero;
	if (type->Proc.diverging) {
		flags = LLVMDIFlagNoReturn;
	}

	return LLVMDIBuilderCreateSubroutineType(m->debug_builder, file, parameters, parameter_count, flags);
}

LLVMMetadataRef lb_debug_struct_field(lbModule *m, String const &name, Type *type, u64 offset_in_bits) {
	unsigned field_line = 1;
	LLVMDIFlags field_flags = LLVMDIFlagZero;

	AstPackage *pkg = m->info->runtime_package;
	GB_ASSERT(pkg->files.count != 0);
	LLVMMetadataRef file = lb_get_llvm_metadata(m, pkg->files[0]);
	LLVMMetadataRef scope = file;

	return LLVMDIBuilderCreateMemberType(m->debug_builder, scope, cast(char const *)name.text, name.len, file, field_line,
		8*cast(u64)type_size_of(type), 8*cast(u32)type_align_of(type), offset_in_bits,
		field_flags, lb_debug_type(m, type)
	);
}
LLVMMetadataRef lb_debug_basic_struct(lbModule *m, String const &name, u64 size_in_bits, u32 align_in_bits, LLVMMetadataRef *elements, unsigned element_count) {
	AstPackage *pkg = m->info->runtime_package;
	GB_ASSERT(pkg->files.count != 0);
	LLVMMetadataRef file = lb_get_llvm_metadata(m, pkg->files[0]);
	LLVMMetadataRef scope = file;

	return LLVMDIBuilderCreateStructType(m->debug_builder, scope, cast(char const *)name.text, name.len, file, 1, size_in_bits, align_in_bits, LLVMDIFlagZero, nullptr, elements, element_count, 0, nullptr, "", 0);
}


LLVMMetadataRef lb_debug_type_basic_type(lbModule *m, String const &name, u64 size_in_bits, LLVMDWARFTypeEncoding encoding, LLVMDIFlags flags = LLVMDIFlagZero) {
	LLVMMetadataRef basic_type = LLVMDIBuilderCreateBasicType(m->debug_builder, cast(char const *)name.text, name.len, size_in_bits, encoding, flags);
#if 1
	LLVMMetadataRef final_decl = LLVMDIBuilderCreateTypedef(m->debug_builder, basic_type, cast(char const *)name.text, name.len, nullptr, 0, nullptr, cast(u32)size_in_bits);
	return final_decl;
#else
	return basic_type;
#endif
}

LLVMMetadataRef lb_debug_type_internal(lbModule *m, Type *type) {
	Type *original_type = type;

	LLVMContextRef ctx = m->ctx;
	i64 size = type_size_of(type); // Check size

	GB_ASSERT(type != t_invalid);

	unsigned const word_size = cast(unsigned)build_context.word_size;
	unsigned const word_bits = cast(unsigned)(8*build_context.word_size);

	switch (type->kind) {
	case Type_Basic:
		switch (type->Basic.kind) {
		case Basic_llvm_bool: return lb_debug_type_basic_type(m, str_lit("llvm bool"),  1, LLVMDWARFTypeEncoding_Boolean);
		case Basic_bool:      return lb_debug_type_basic_type(m, str_lit("bool"),       8, LLVMDWARFTypeEncoding_Boolean);
		case Basic_b8:        return lb_debug_type_basic_type(m, str_lit("b8"),         8, LLVMDWARFTypeEncoding_Boolean);
		case Basic_b16:       return lb_debug_type_basic_type(m, str_lit("b16"),       16, LLVMDWARFTypeEncoding_Boolean);
		case Basic_b32:       return lb_debug_type_basic_type(m, str_lit("b32"),       32, LLVMDWARFTypeEncoding_Boolean);
		case Basic_b64:       return lb_debug_type_basic_type(m, str_lit("b64"),       64, LLVMDWARFTypeEncoding_Boolean);

		case Basic_i8:   return lb_debug_type_basic_type(m, str_lit("i8"),     8, LLVMDWARFTypeEncoding_Signed);
		case Basic_u8:   return lb_debug_type_basic_type(m, str_lit("u8"),     8, LLVMDWARFTypeEncoding_Unsigned);
		case Basic_i16:  return lb_debug_type_basic_type(m, str_lit("i16"),   16, LLVMDWARFTypeEncoding_Signed);
		case Basic_u16:  return lb_debug_type_basic_type(m, str_lit("u16"),   16, LLVMDWARFTypeEncoding_Unsigned);
		case Basic_i32:  return lb_debug_type_basic_type(m, str_lit("i32"),   32, LLVMDWARFTypeEncoding_Signed);
		case Basic_u32:  return lb_debug_type_basic_type(m, str_lit("u32"),   32, LLVMDWARFTypeEncoding_Unsigned);
		case Basic_i64:  return lb_debug_type_basic_type(m, str_lit("i64"),   64, LLVMDWARFTypeEncoding_Signed);
		case Basic_u64:  return lb_debug_type_basic_type(m, str_lit("u64"),   64, LLVMDWARFTypeEncoding_Unsigned);
		case Basic_i128: return lb_debug_type_basic_type(m, str_lit("i128"), 128, LLVMDWARFTypeEncoding_Signed);
		case Basic_u128: return lb_debug_type_basic_type(m, str_lit("u128"), 128, LLVMDWARFTypeEncoding_Unsigned);

		case Basic_rune: return lb_debug_type_basic_type(m, str_lit("rune"), 32, LLVMDWARFTypeEncoding_Utf);


		case Basic_f16: return lb_debug_type_basic_type(m, str_lit("f16"), 16, LLVMDWARFTypeEncoding_Float);
		case Basic_f32: return lb_debug_type_basic_type(m, str_lit("f32"), 32, LLVMDWARFTypeEncoding_Float);
		case Basic_f64: return lb_debug_type_basic_type(m, str_lit("f64"), 64, LLVMDWARFTypeEncoding_Float);

		case Basic_int:  return lb_debug_type_basic_type(m,    str_lit("int"),     word_bits, LLVMDWARFTypeEncoding_Signed);
		case Basic_uint: return lb_debug_type_basic_type(m,    str_lit("uint"),    word_bits, LLVMDWARFTypeEncoding_Unsigned);
		case Basic_uintptr: return lb_debug_type_basic_type(m, str_lit("uintptr"), word_bits, LLVMDWARFTypeEncoding_Unsigned);

		case Basic_typeid:
			return lb_debug_type_basic_type(m, str_lit("typeid"), word_bits, LLVMDWARFTypeEncoding_Unsigned);

		// Endian Specific Types
		case Basic_i16le:  return lb_debug_type_basic_type(m, str_lit("i16le"),  16,  LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagLittleEndian);
		case Basic_u16le:  return lb_debug_type_basic_type(m, str_lit("u16le"),  16,  LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagLittleEndian);
		case Basic_i32le:  return lb_debug_type_basic_type(m, str_lit("i32le"),  32,  LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagLittleEndian);
		case Basic_u32le:  return lb_debug_type_basic_type(m, str_lit("u32le"),  32,  LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagLittleEndian);
		case Basic_i64le:  return lb_debug_type_basic_type(m, str_lit("i64le"),  64,  LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagLittleEndian);
		case Basic_u64le:  return lb_debug_type_basic_type(m, str_lit("u64le"),  64,  LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagLittleEndian);
		case Basic_i128le: return lb_debug_type_basic_type(m, str_lit("i128le"), 128, LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagLittleEndian);
		case Basic_u128le: return lb_debug_type_basic_type(m, str_lit("u128le"), 128, LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagLittleEndian);

		case Basic_f16le: return lb_debug_type_basic_type(m,  str_lit("f16le"),   16, LLVMDWARFTypeEncoding_Float,    LLVMDIFlagLittleEndian);
		case Basic_f32le: return lb_debug_type_basic_type(m,  str_lit("f32le"),   32, LLVMDWARFTypeEncoding_Float,    LLVMDIFlagLittleEndian);
		case Basic_f64le: return lb_debug_type_basic_type(m,  str_lit("f64le"),   64, LLVMDWARFTypeEncoding_Float,    LLVMDIFlagLittleEndian);

		case Basic_i16be:  return lb_debug_type_basic_type(m, str_lit("i16be"),  16,  LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagBigEndian);
		case Basic_u16be:  return lb_debug_type_basic_type(m, str_lit("u16be"),  16,  LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagBigEndian);
		case Basic_i32be:  return lb_debug_type_basic_type(m, str_lit("i32be"),  32,  LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagBigEndian);
		case Basic_u32be:  return lb_debug_type_basic_type(m, str_lit("u32be"),  32,  LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagBigEndian);
		case Basic_i64be:  return lb_debug_type_basic_type(m, str_lit("i64be"),  64,  LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagBigEndian);
		case Basic_u64be:  return lb_debug_type_basic_type(m, str_lit("u64be"),  64,  LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagBigEndian);
		case Basic_i128be: return lb_debug_type_basic_type(m, str_lit("i128be"), 128, LLVMDWARFTypeEncoding_Signed,   LLVMDIFlagBigEndian);
		case Basic_u128be: return lb_debug_type_basic_type(m, str_lit("u128be"), 128, LLVMDWARFTypeEncoding_Unsigned, LLVMDIFlagBigEndian);

		case Basic_f16be: return lb_debug_type_basic_type(m,  str_lit("f16be"),   16, LLVMDWARFTypeEncoding_Float,    LLVMDIFlagLittleEndian);
		case Basic_f32be: return lb_debug_type_basic_type(m,  str_lit("f32be"),   32, LLVMDWARFTypeEncoding_Float,    LLVMDIFlagLittleEndian);
		case Basic_f64be: return lb_debug_type_basic_type(m,  str_lit("f64be"),   64, LLVMDWARFTypeEncoding_Float,    LLVMDIFlagLittleEndian);

		case Basic_complex32:
			{
				LLVMMetadataRef elements[2] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("real"), t_f16, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("imag"), t_f16, 4);
				return lb_debug_basic_struct(m, str_lit("complex32"), 64, 32, elements, gb_count_of(elements));
			}
		case Basic_complex64:
			{
				LLVMMetadataRef elements[2] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("real"), t_f32, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("imag"), t_f32, 4);
				return lb_debug_basic_struct(m, str_lit("complex64"), 64, 32, elements, gb_count_of(elements));
			}
		case Basic_complex128:
			{
				LLVMMetadataRef elements[2] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("real"), t_f64, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("imag"), t_f64, 8);
				return lb_debug_basic_struct(m, str_lit("complex128"), 128, 64, elements, gb_count_of(elements));
			}

		case Basic_quaternion64:
			{
				LLVMMetadataRef elements[4] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("imag"), t_f16, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("jmag"), t_f16, 4);
				elements[2] = lb_debug_struct_field(m, str_lit("kmag"), t_f16, 8);
				elements[3] = lb_debug_struct_field(m, str_lit("real"), t_f16, 12);
				return lb_debug_basic_struct(m, str_lit("quaternion64"), 128, 32, elements, gb_count_of(elements));
			}
		case Basic_quaternion128:
			{
				LLVMMetadataRef elements[4] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("imag"), t_f32, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("jmag"), t_f32, 4);
				elements[2] = lb_debug_struct_field(m, str_lit("kmag"), t_f32, 8);
				elements[3] = lb_debug_struct_field(m, str_lit("real"), t_f32, 12);
				return lb_debug_basic_struct(m, str_lit("quaternion128"), 128, 32, elements, gb_count_of(elements));
			}
		case Basic_quaternion256:
			{
				LLVMMetadataRef elements[4] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("imag"), t_f64, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("jmag"), t_f64, 8);
				elements[2] = lb_debug_struct_field(m, str_lit("kmag"), t_f64, 16);
				elements[3] = lb_debug_struct_field(m, str_lit("real"), t_f64, 24);
				return lb_debug_basic_struct(m, str_lit("quaternion256"), 256, 32, elements, gb_count_of(elements));
			}



		case Basic_rawptr:
			{
				LLVMMetadataRef void_type = lb_debug_type_basic_type(m, str_lit("void"), 8, LLVMDWARFTypeEncoding_Unsigned);
				return LLVMDIBuilderCreatePointerType(m->debug_builder, void_type, word_bits, word_bits, LLVMDWARFTypeEncoding_Address, "rawptr", 6);
			}
		case Basic_string:
			{
				LLVMMetadataRef elements[2] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("data"), t_u8_ptr, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("len"),  t_int, word_bits);
				return lb_debug_basic_struct(m, str_lit("string"), 2*word_bits, word_bits, elements, gb_count_of(elements));
			}
		case Basic_cstring:
			{
				LLVMMetadataRef char_type = lb_debug_type_basic_type(m, str_lit("char"), 8, LLVMDWARFTypeEncoding_Unsigned);
				return LLVMDIBuilderCreatePointerType(m->debug_builder, char_type, word_bits, word_bits, 0, "cstring", 7);
			}
		case Basic_any:
			{
				LLVMMetadataRef elements[2] = {};
				elements[0] = lb_debug_struct_field(m, str_lit("data"), t_rawptr, 0);
				elements[1] = lb_debug_struct_field(m, str_lit("id"),   t_typeid, word_bits);
				return lb_debug_basic_struct(m, str_lit("any"), 2*word_bits, word_bits, elements, gb_count_of(elements));
			}

		// Untyped types
		case Basic_UntypedBool:       GB_PANIC("Basic_UntypedBool");       break;
		case Basic_UntypedInteger:    GB_PANIC("Basic_UntypedInteger");    break;
		case Basic_UntypedFloat:      GB_PANIC("Basic_UntypedFloat");      break;
		case Basic_UntypedComplex:    GB_PANIC("Basic_UntypedComplex");    break;
		case Basic_UntypedQuaternion: GB_PANIC("Basic_UntypedQuaternion"); break;
		case Basic_UntypedString:     GB_PANIC("Basic_UntypedString");     break;
		case Basic_UntypedRune:       GB_PANIC("Basic_UntypedRune");       break;
		case Basic_UntypedNil:        GB_PANIC("Basic_UntypedNil");        break;
		case Basic_UntypedUndef:      GB_PANIC("Basic_UntypedUndef");      break;

		default: GB_PANIC("Basic Unhandled"); break;
		}
		break;

	case Type_Named:
		GB_PANIC("Type_Named should be handled in lb_debug_type separately");

	case Type_Pointer:
		return LLVMDIBuilderCreatePointerType(m->debug_builder, lb_debug_type(m, type->Pointer.elem), word_bits, word_bits, 0, nullptr, 0);

	case Type_Array: {
		LLVMMetadataRef subscripts[1] = {};
		subscripts[0] = LLVMDIBuilderGetOrCreateSubrange(m->debug_builder,
			0ll,
			type->Array.count
		);

		return LLVMDIBuilderCreateArrayType(m->debug_builder,
			8*cast(uint64_t)type_size_of(type),
			8*cast(unsigned)type_align_of(type),
			lb_debug_type(m, type->Array.elem),
			subscripts, gb_count_of(subscripts));
	}

	case Type_EnumeratedArray: {
		LLVMMetadataRef subscripts[1] = {};
		subscripts[0] = LLVMDIBuilderGetOrCreateSubrange(m->debug_builder,
			0ll,
			type->EnumeratedArray.count
		);

		LLVMMetadataRef array_type = LLVMDIBuilderCreateArrayType(m->debug_builder,
			8*cast(uint64_t)type_size_of(type),
			8*cast(unsigned)type_align_of(type),
			lb_debug_type(m, type->EnumeratedArray.elem),
			subscripts, gb_count_of(subscripts));
		gbString name = type_to_string(type, temporary_allocator());
		return LLVMDIBuilderCreateTypedef(m->debug_builder, array_type, name, gb_string_length(name), nullptr, 0, nullptr, cast(u32)(8*type_align_of(type)));
	}


	case Type_Struct:
	case Type_Union:
	case Type_Slice:
	case Type_DynamicArray:
	case Type_Map:
	case Type_BitSet:
		{
			unsigned tag = DW_TAG_structure_type;
			if (is_type_raw_union(type) || is_type_union(type)) {
				tag = DW_TAG_union_type;
			}
			u64 size_in_bits  = cast(u64)(8*type_size_of(type));
			u32 align_in_bits = cast(u32)(8*type_size_of(type));
			LLVMDIFlags flags = LLVMDIFlagZero;

			LLVMMetadataRef temp_forward_decl = LLVMDIBuilderCreateReplaceableCompositeType(
				m->debug_builder, tag, "", 0, nullptr, nullptr, 0, 0, size_in_bits, align_in_bits, flags, "", 0
			);
			lbIncompleteDebugType idt = {};
			idt.type = type;
			idt.metadata = temp_forward_decl;

			array_add(&m->debug_incomplete_types, idt);
			lb_set_llvm_metadata(m, type, temp_forward_decl);
			return temp_forward_decl;
		}

	case Type_Enum:
		{
			LLVMMetadataRef scope = nullptr;
			LLVMMetadataRef file = nullptr;
			unsigned line = 0;
			unsigned element_count = cast(unsigned)type->Enum.fields.count;
			LLVMMetadataRef *elements = gb_alloc_array(permanent_allocator(), LLVMMetadataRef, element_count);
			Type *bt = base_enum_type(type);
			LLVMBool is_unsigned = is_type_unsigned(bt);
			for (unsigned i = 0; i < element_count; i++) {
				Entity *f = type->Enum.fields[i];
				GB_ASSERT(f->kind == Entity_Constant);
				String name = f->token.string;
				i64 value = exact_value_to_i64(f->Constant.value);
				elements[i] = LLVMDIBuilderCreateEnumerator(m->debug_builder, cast(char const *)name.text, cast(size_t)name.len, value, is_unsigned);
			}
			LLVMMetadataRef class_type = lb_debug_type(m, bt);
			return LLVMDIBuilderCreateEnumerationType(m->debug_builder, scope, "", 0, file, line, 8*type_size_of(type), 8*cast(unsigned)type_align_of(type), elements, element_count, class_type);
		}

	case Type_Tuple:
		if (type->Tuple.variables.count == 1) {
			return lb_debug_type(m, type->Tuple.variables[0]->type);
		} else {
			type_set_offsets(type);
			LLVMMetadataRef parent_scope = nullptr;
			LLVMMetadataRef scope = nullptr;
			LLVMMetadataRef file = nullptr;
			unsigned line = 0;
			u64 size_in_bits = 8*cast(u64)type_size_of(type);
			u32 align_in_bits = 8*cast(u32)type_align_of(type);
			LLVMDIFlags flags = LLVMDIFlagZero;

			unsigned element_count = cast(unsigned)type->Tuple.variables.count;
			LLVMMetadataRef *elements = gb_alloc_array(permanent_allocator(), LLVMMetadataRef, element_count);

			for (unsigned i = 0; i < element_count; i++) {
				Entity *f = type->Tuple.variables[i];
				GB_ASSERT(f->kind == Entity_Variable);
				String name = f->token.string;
				unsigned field_line = 0;
				LLVMDIFlags field_flags = LLVMDIFlagZero;
				u64 offset_in_bits = 8*cast(u64)type->Tuple.offsets[i];
				elements[i] = LLVMDIBuilderCreateMemberType(m->debug_builder, scope, cast(char const *)name.text, name.len, file, field_line,
					8*cast(u64)type_size_of(f->type), 8*cast(u32)type_align_of(f->type), offset_in_bits,
					field_flags, lb_debug_type(m, f->type)
				);
			}


			return LLVMDIBuilderCreateStructType(m->debug_builder, parent_scope, "", 0, file, line,
				size_in_bits, align_in_bits, flags,
				nullptr, elements, element_count, 0, nullptr,
				"", 0
			);
		}

	case Type_Proc:
		{
			LLVMMetadataRef proc_underlying_type = lb_debug_type_internal_proc(m, type);
			LLVMMetadataRef pointer_type = LLVMDIBuilderCreatePointerType(m->debug_builder, proc_underlying_type, word_bits, word_bits, 0, nullptr, 0);
			gbString name = type_to_string(type, temporary_allocator());
			return LLVMDIBuilderCreateTypedef(m->debug_builder, pointer_type, name, gb_string_length(name), nullptr, 0, nullptr, cast(u32)(8*type_align_of(type)));
		}
		break;

	case Type_SimdVector:
		return LLVMDIBuilderCreateVectorType(m->debug_builder, cast(unsigned)type->SimdVector.count, 8*cast(unsigned)type_align_of(type), lb_debug_type(m, type->SimdVector.elem), nullptr, 0);

	case Type_RelativePointer: {
		LLVMMetadataRef base_integer = lb_debug_type(m, type->RelativePointer.base_integer);
		gbString name = type_to_string(type, temporary_allocator());
		return LLVMDIBuilderCreateTypedef(m->debug_builder, base_integer, name, gb_string_length(name), nullptr, 0, nullptr, cast(u32)(8*type_align_of(type)));
	}

	case Type_RelativeSlice:
		{
			unsigned element_count = 0;
			LLVMMetadataRef elements[2] = {};
			Type *base_integer = type->RelativeSlice.base_integer;
			elements[0] = lb_debug_struct_field(m, str_lit("data_offset"), base_integer, 0);
			elements[1] = lb_debug_struct_field(m, str_lit("len"), base_integer, 8*type_size_of(base_integer));
			gbString name = type_to_string(type, temporary_allocator());
			return LLVMDIBuilderCreateStructType(m->debug_builder, nullptr, name, gb_string_length(name), nullptr, 0, 2*word_bits, word_bits, LLVMDIFlagZero, nullptr, elements, element_count, 0, nullptr, "", 0);
		}
	}

	GB_PANIC("Invalid type %s", type_to_string(type));
	return nullptr;
}

LLVMMetadataRef lb_get_base_scope_metadata(lbModule *m, Scope *scope) {
	LLVMMetadataRef found = nullptr;
	for (;;) {
		if (scope == nullptr) {
			return nullptr;
		}
		if (scope->flags & ScopeFlag_Proc) {
			found = lb_get_llvm_metadata(m, scope->procedure_entity);
			if (found) {
				return found;
			}
		}
		if (scope->flags & ScopeFlag_File) {
			found = lb_get_llvm_metadata(m, scope->file);
			if (found) {
				return found;
			}
		}
		scope = scope->parent;
	}
}

LLVMMetadataRef lb_debug_type(lbModule *m, Type *type) {
	GB_ASSERT(type != nullptr);
	LLVMMetadataRef found = lb_get_llvm_metadata(m, type);
	if (found != nullptr) {
		return found;
	}

	if (type->kind == Type_Named) {
		LLVMMetadataRef file = nullptr;
		unsigned line = 0;
		LLVMMetadataRef scope = nullptr;


		if (type->Named.type_name != nullptr) {
			Entity *e = type->Named.type_name;
			scope = lb_get_base_scope_metadata(m, e->scope);
			if (scope != nullptr) {
				file = LLVMDIScopeGetFile(scope);
			}
			line = cast(unsigned)e->token.pos.line;
		}
		// TODO(bill): location data for Type_Named

		u64 size_in_bits = 8*type_size_of(type);
		u32 align_in_bits = 8*cast(u32)type_align_of(type);
		String name = type->Named.name;
		char const *name_text = cast(char const *)name.text;
		size_t name_len = cast(size_t)name.len;
		unsigned tag = DW_TAG_structure_type;
		if (is_type_raw_union(type) || is_type_union(type)) {
			tag = DW_TAG_union_type;
		}
		LLVMDIFlags flags = LLVMDIFlagZero;

		Type *bt = base_type(type->Named.base);

		lbIncompleteDebugType idt = {};
		idt.type = type;

		switch (bt->kind) {
		case Type_Enum:
			{
				unsigned line = 0;
				unsigned element_count = cast(unsigned)bt->Enum.fields.count;
				LLVMMetadataRef *elements = gb_alloc_array(permanent_allocator(), LLVMMetadataRef, element_count);
				Type *ct = base_enum_type(type);
				LLVMBool is_unsigned = is_type_unsigned(ct);
				for (unsigned i = 0; i < element_count; i++) {
					Entity *f = bt->Enum.fields[i];
					GB_ASSERT(f->kind == Entity_Constant);
					String name = f->token.string;
					i64 value = exact_value_to_i64(f->Constant.value);
					elements[i] = LLVMDIBuilderCreateEnumerator(m->debug_builder, cast(char const *)name.text, cast(size_t)name.len, value, is_unsigned);
				}
				LLVMMetadataRef class_type = lb_debug_type(m, ct);
				return LLVMDIBuilderCreateEnumerationType(m->debug_builder, scope, name_text, name_len, file, line, 8*type_size_of(type), 8*cast(unsigned)type_align_of(type), elements, element_count, class_type);
			}


		case Type_Basic:
		case Type_Pointer:
		case Type_Array:
		case Type_EnumeratedArray:
		case Type_Tuple:
		case Type_Proc:
		case Type_SimdVector:
		case Type_RelativePointer:
		case Type_RelativeSlice:
			{
				LLVMMetadataRef debug_bt = lb_debug_type(m, bt);
				LLVMMetadataRef final_decl = LLVMDIBuilderCreateTypedef(m->debug_builder, debug_bt, name_text, name_len, file, line, scope, align_in_bits);
				lb_set_llvm_metadata(m, type, final_decl);
				return final_decl;
			}

		case Type_Slice:
		case Type_DynamicArray:
		case Type_Map:
		case Type_Struct:
		case Type_Union:
		case Type_BitSet:
			LLVMMetadataRef temp_forward_decl = LLVMDIBuilderCreateReplaceableCompositeType(
				m->debug_builder, tag, name_text, name_len, nullptr, nullptr, 0, 0, size_in_bits, align_in_bits, flags, "", 0
			);
			idt.metadata = temp_forward_decl;

			array_add(&m->debug_incomplete_types, idt);
			lb_set_llvm_metadata(m, type, temp_forward_decl);
			return temp_forward_decl;
		}
	}


	LLVMMetadataRef dt = lb_debug_type_internal(m, type);
	lb_set_llvm_metadata(m, type, dt);
	return dt;
}

void lb_debug_complete_types(lbModule *m) {
	unsigned const word_size = cast(unsigned)build_context.word_size;
	unsigned const word_bits = cast(unsigned)(8*build_context.word_size);

	for_array(debug_incomplete_type_index, m->debug_incomplete_types) {
		auto const &idt = m->debug_incomplete_types[debug_incomplete_type_index];
		GB_ASSERT(idt.type != nullptr);
		GB_ASSERT(idt.metadata != nullptr);

		Type *t = idt.type;
		Type *bt = base_type(t);

		LLVMMetadataRef parent_scope = nullptr;
		LLVMMetadataRef file = nullptr;
		unsigned line_number = 0;
		u64 size_in_bits  = 8*type_size_of(t);
		u32 align_in_bits = cast(u32)(8*type_align_of(t));
		LLVMDIFlags flags = LLVMDIFlagZero;

		LLVMMetadataRef derived_from = nullptr;

		LLVMMetadataRef *elements = nullptr;
		unsigned element_count = 0;


		unsigned runtime_lang = 0; // Objective-C runtime version
		char const *unique_id = "";
		LLVMMetadataRef vtable_holder = nullptr;
		size_t unique_id_len = 0;


		LLVMMetadataRef record_scope = nullptr;

		switch (bt->kind) {
		case Type_Slice:
		case Type_DynamicArray:
		case Type_Map:
		case Type_Struct:
		case Type_Union:
		case Type_BitSet: {
			bool is_union = is_type_raw_union(bt) || is_type_union(bt);

			String name = str_lit("<anonymous-struct>");
			if (t->kind == Type_Named) {
				name = t->Named.name;
				if (t->Named.type_name && t->Named.type_name->pkg && t->Named.type_name->pkg->name.len != 0) {
					name = concatenate3_strings(temporary_allocator(), t->Named.type_name->pkg->name, str_lit("."), t->Named.name);
				}

				LLVMMetadataRef file = nullptr;
				unsigned line = 0;
				LLVMMetadataRef file_scope = nullptr;

				if (t->Named.type_name != nullptr) {
					Entity *e = t->Named.type_name;
					file_scope = lb_get_llvm_metadata(m, e->scope);
					if (file_scope != nullptr) {
						file = LLVMDIScopeGetFile(file_scope);
					}
					line = cast(unsigned)e->token.pos.line;
				}
				// TODO(bill): location data for Type_Named

			} else {
				name = make_string_c(type_to_string(t, temporary_allocator()));
			}



			switch (bt->kind) {
			case Type_Slice:
				element_count = 2;
				elements = gb_alloc_array(temporary_allocator(), LLVMMetadataRef, element_count);
				elements[0] = lb_debug_struct_field(m, str_lit("data"), alloc_type_pointer(bt->Slice.elem), 0*word_bits);
				elements[1] = lb_debug_struct_field(m, str_lit("len"),  t_int,                              1*word_bits);
				break;
			case Type_DynamicArray:
				element_count = 4;
				elements = gb_alloc_array(temporary_allocator(), LLVMMetadataRef, element_count);
				elements[0] = lb_debug_struct_field(m, str_lit("data"),      alloc_type_pointer(bt->DynamicArray.elem), 0*word_bits);
				elements[1] = lb_debug_struct_field(m, str_lit("len"),       t_int,                                     1*word_bits);
				elements[2] = lb_debug_struct_field(m, str_lit("cap"),       t_int,                                     2*word_bits);
				elements[3] = lb_debug_struct_field(m, str_lit("allocator"), t_allocator,                               3*word_bits);
				break;

			case Type_Map:
				bt = bt->Map.internal_type;
				/*fallthrough*/
			case Type_Struct:
				if (file == nullptr) {
					if (bt->Struct.node) {
						file = lb_get_llvm_metadata(m, bt->Struct.node->file);
						line_number = cast(unsigned)ast_token(bt->Struct.node).pos.line;
					}
				}

				type_set_offsets(bt);
				{
					isize element_offset = 0;
					record_scope = lb_get_llvm_metadata(m, bt->Struct.scope);
					switch (bt->Struct.soa_kind) {
					case StructSoa_Slice:   element_offset = 1; break;
					case StructSoa_Dynamic: element_offset = 3; break;
					}
					element_count = cast(unsigned)(bt->Struct.fields.count + element_offset);
					elements = gb_alloc_array(temporary_allocator(), LLVMMetadataRef, element_count);
					switch (bt->Struct.soa_kind) {
					case StructSoa_Slice:
						elements[0] = LLVMDIBuilderCreateMemberType(
							m->debug_builder, record_scope,
							".len", 4,
							file, 0,
							8*cast(u64)type_size_of(t_int), 8*cast(u32)type_align_of(t_int),
							8*type_size_of(bt)-word_bits,
							LLVMDIFlagZero, lb_debug_type(m, t_int)
						);
						break;
					case StructSoa_Dynamic:
						elements[0] = LLVMDIBuilderCreateMemberType(
							m->debug_builder, record_scope,
							".len", 4,
							file, 0,
							8*cast(u64)type_size_of(t_int), 8*cast(u32)type_align_of(t_int),
							8*type_size_of(bt)-word_bits + 0*word_bits,
							LLVMDIFlagZero, lb_debug_type(m, t_int)
						);
						elements[1] = LLVMDIBuilderCreateMemberType(
							m->debug_builder, record_scope,
							".cap", 4,
							file, 0,
							8*cast(u64)type_size_of(t_int), 8*cast(u32)type_align_of(t_int),
							8*type_size_of(bt)-word_bits + 1*word_bits,
							LLVMDIFlagZero, lb_debug_type(m, t_int)
						);
						elements[2] = LLVMDIBuilderCreateMemberType(
							m->debug_builder, record_scope,
							".allocator", 12,
							file, 0,
							8*cast(u64)type_size_of(t_int), 8*cast(u32)type_align_of(t_int),
							8*type_size_of(bt)-word_bits + 2*word_bits,
							LLVMDIFlagZero, lb_debug_type(m, t_allocator)
						);
						break;
					}

					for_array(j, bt->Struct.fields) {
						Entity *f = bt->Struct.fields[j];
						String fname = f->token.string;

						unsigned field_line = 0;
						LLVMDIFlags field_flags = LLVMDIFlagZero;
						u64 offset_in_bits = 8*cast(u64)bt->Struct.offsets[j];

						elements[element_offset+j] = LLVMDIBuilderCreateMemberType(
							m->debug_builder, record_scope,
							cast(char const *)fname.text, cast(size_t)fname.len,
							file, field_line,
							8*cast(u64)type_size_of(f->type), 8*cast(u32)type_align_of(f->type),
							offset_in_bits,
							field_flags, lb_debug_type(m, f->type)
						);
					}
				}
				break;
			case Type_Union:
				{
					if (file == nullptr) {
						GB_ASSERT(bt->Union.node != nullptr);
						file = lb_get_llvm_metadata(m, bt->Union.node->file);
						line_number = cast(unsigned)ast_token(bt->Union.node).pos.line;
					}

					isize index_offset = 1;
					if (is_type_union_maybe_pointer(bt)) {
						index_offset = 0;
					}
					record_scope = lb_get_llvm_metadata(m, bt->Union.scope);
					element_count = cast(unsigned)bt->Union.variants.count;
					if (index_offset > 0) {
						element_count += 1;
					}

					elements = gb_alloc_array(temporary_allocator(), LLVMMetadataRef, element_count);
					if (index_offset > 0) {
						Type *tag_type = union_tag_type(bt);
						unsigned field_line = 0;
						u64 offset_in_bits = 8*cast(u64)bt->Union.variant_block_size;
						LLVMDIFlags field_flags = LLVMDIFlagZero;

						elements[0] = LLVMDIBuilderCreateMemberType(
							m->debug_builder, record_scope,
							"tag", 3,
							file, field_line,
							8*cast(u64)type_size_of(tag_type), 8*cast(u32)type_align_of(tag_type),
							offset_in_bits,
							field_flags, lb_debug_type(m, tag_type)
						);
					}

					for_array(j, bt->Union.variants) {
						Type *variant = bt->Union.variants[j];

						unsigned field_index = cast(unsigned)(index_offset+j);

						char name[16] = {};
						gb_snprintf(name, gb_size_of(name), "v%u", field_index);
						isize name_len = gb_strlen(name);

						unsigned field_line = 0;
						LLVMDIFlags field_flags = LLVMDIFlagZero;
						u64 offset_in_bits = 0;

						elements[field_index] = LLVMDIBuilderCreateMemberType(
							m->debug_builder, record_scope,
							name, name_len,
							file, field_line,
							8*cast(u64)type_size_of(variant), 8*cast(u32)type_align_of(variant),
							offset_in_bits,
							field_flags, lb_debug_type(m, variant)
						);
					}
				}
				break;

			case Type_BitSet:
				{
					if (file == nullptr) {
						GB_ASSERT(bt->BitSet.node != nullptr);
						file = lb_get_llvm_metadata(m, bt->BitSet.node->file);
						line_number = cast(unsigned)ast_token(bt->BitSet.node).pos.line;
					}

					LLVMMetadataRef bit_set_field_type = lb_debug_type(m, t_bool);
					LLVMMetadataRef scope = file;

					Type *elem = base_type(bt->BitSet.elem);
					if (elem->kind == Type_Enum) {
						element_count = cast(unsigned)elem->Enum.fields.count;
						elements = gb_alloc_array(temporary_allocator(), LLVMMetadataRef, element_count);
						for_array(i, elem->Enum.fields) {
							Entity *f = elem->Enum.fields[i];
							GB_ASSERT(f->kind == Entity_Constant);
							i64 val = exact_value_to_i64(f->Constant.value);
							String name = f->token.string;
							u64 offset_in_bits = cast(u64)(val - bt->BitSet.lower);
							elements[i] = LLVMDIBuilderCreateBitFieldMemberType(
								m->debug_builder,
								scope,
								cast(char const *)name.text, name.len,
								file, line_number,
								1,
								offset_in_bits,
								0,
								LLVMDIFlagZero,
								bit_set_field_type
							);
						}
					} else {

						char name[32] = {};

						GB_ASSERT(is_type_integer(elem));
						i64 count = bt->BitSet.upper - bt->BitSet.lower + 1;
						GB_ASSERT(0 <= count);

						element_count = cast(unsigned)count;
						elements = gb_alloc_array(temporary_allocator(), LLVMMetadataRef, element_count);
						for (unsigned i = 0; i < element_count; i++) {
							u64 offset_in_bits = i;
							i64 val = bt->BitSet.lower + cast(i64)i;
							gb_snprintf(name, gb_count_of(name), "%lld", cast(long long)val);
							elements[i] = LLVMDIBuilderCreateBitFieldMemberType(
								m->debug_builder,
								scope,
								name, gb_strlen(name),
								file, line_number,
								1,
								offset_in_bits,
								0,
								LLVMDIFlagZero,
								bit_set_field_type
							);
						}
					}
				}
			}


			LLVMMetadataRef final_metadata = nullptr;
			if (is_union) {
				final_metadata = LLVMDIBuilderCreateUnionType(
					m->debug_builder,
					parent_scope,
					cast(char const *)name.text, cast(size_t)name.len,
					file, line_number,
					size_in_bits, align_in_bits,
					flags,
					elements, element_count,
					runtime_lang,
					unique_id, unique_id_len
				);
			} else {
				final_metadata = LLVMDIBuilderCreateStructType(
					m->debug_builder,
					parent_scope,
					cast(char const *)name.text, cast(size_t)name.len,
					file, line_number,
					size_in_bits, align_in_bits,
					flags,
					derived_from,
					elements, element_count,
					runtime_lang,
					vtable_holder,
					unique_id, unique_id_len
				);
			}

			LLVMMetadataReplaceAllUsesWith(idt.metadata, final_metadata);
			lb_set_llvm_metadata(m, idt.type, final_metadata);
		} break;
		default:
			GB_PANIC("invalid incomplete debug type");
			break;
		}
	}
	array_clear(&m->debug_incomplete_types);
}


void lb_add_entity(lbModule *m, Entity *e, lbValue val) {
	if (e != nullptr) {
		map_set(&m->values, hash_entity(e), val);
	}
}
void lb_add_member(lbModule *m, String const &name, lbValue val) {
	if (name.len > 0) {
		string_map_set(&m->members, name, val);
	}
}
void lb_add_member(lbModule *m, StringHashKey const &key, lbValue val) {
	string_map_set(&m->members, key, val);
}
void lb_add_procedure_value(lbModule *m, lbProcedure *p) {
	if (p->entity != nullptr) {
		map_set(&m->procedure_values, hash_pointer(p->value), p->entity);
	}
	string_map_set(&m->procedures, p->name, p);
}


LLVMValueRef llvm_const_cast(LLVMValueRef val, LLVMTypeRef dst) {
	LLVMTypeRef src = LLVMTypeOf(val);
	if (src == dst) {
		return val;
	}
	if (LLVMIsNull(val)) {
		return LLVMConstNull(dst);
	}

	GB_ASSERT(LLVMSizeOf(dst) == LLVMSizeOf(src));
	LLVMTypeKind kind = LLVMGetTypeKind(dst);
	switch (kind) {
	case LLVMPointerTypeKind:
		return LLVMConstPointerCast(val, dst);
	case LLVMStructTypeKind:
		return LLVMConstBitCast(val, dst);
	default:
		GB_PANIC("Unhandled const cast %s to %s", LLVMPrintTypeToString(src), LLVMPrintTypeToString(dst));
	}

	return val;
}
LLVMValueRef llvm_const_named_struct(LLVMTypeRef t, LLVMValueRef *values, isize value_count_) {
	unsigned value_count = cast(unsigned)value_count_;
	unsigned elem_count = LLVMCountStructElementTypes(t);
	GB_ASSERT(value_count == elem_count);
	for (unsigned i = 0; i < elem_count; i++) {
		LLVMTypeRef elem_type = LLVMStructGetTypeAtIndex(t, i);
		values[i] = llvm_const_cast(values[i], elem_type);
	}
	return LLVMConstNamedStruct(t, values, value_count);
}

LLVMValueRef llvm_const_array(LLVMTypeRef elem_type, LLVMValueRef *values, isize value_count_) {
	unsigned value_count = cast(unsigned)value_count_;
	for (unsigned i = 0; i < value_count; i++) {
		values[i] = llvm_const_cast(values[i], elem_type);
	}
	return LLVMConstArray(elem_type, values, value_count);
}


lbValue lb_emit_string(lbProcedure *p, lbValue str_elem, lbValue str_len) {
	if (false && lb_is_const(str_elem) && lb_is_const(str_len)) {
		LLVMValueRef values[2] = {
			str_elem.value,
			str_len.value,
		};
		lbValue res = {};
		res.type = t_string;
		res.value = llvm_const_named_struct(lb_type(p->module, t_string), values, gb_count_of(values));
		return res;
	} else {
		lbAddr res = lb_add_local_generated(p, t_string, false);
		lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 0), str_elem);
		lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 1), str_len);
		return lb_addr_load(p, res);
	}
}

LLVMAttributeRef lb_create_enum_attribute_with_type(LLVMContextRef ctx, char const *name, LLVMTypeRef type) {
	String s = make_string_c(name);

	// NOTE(2021-02-25, bill); All this attributes require a type associated with them
	// and the current LLVM C API does not expose this functionality yet.
	// It is better to ignore the attributes for the time being
	if (s == "byval") {
		// return nullptr;
	} else if (s == "byref") {
		// return nullptr;
	} else if (s == "preallocated") {
		// return nullptr;
	} else if (s == "sret") {
		// return nullptr;
	}

	unsigned kind = LLVMGetEnumAttributeKindForName(name, s.len);
	GB_ASSERT_MSG(kind != 0, "unknown attribute: %s", name);
	return LLVMCreateTypeAttribute(ctx, kind, type);
}

LLVMAttributeRef lb_create_enum_attribute(LLVMContextRef ctx, char const *name, u64 value) {
	String s = make_string_c(name);

	// NOTE(2021-02-25, bill); All this attributes require a type associated with them
	// and the current LLVM C API does not expose this functionality yet.
	// It is better to ignore the attributes for the time being
	if (s == "byval") {
		GB_PANIC("lb_create_enum_attribute_with_type should be used for %s", name);
	} else if (s == "byref") {
		GB_PANIC("lb_create_enum_attribute_with_type should be used for %s", name);
	} else if (s == "preallocated") {
		GB_PANIC("lb_create_enum_attribute_with_type should be used for %s", name);
	} else if (s == "sret") {
		GB_PANIC("lb_create_enum_attribute_with_type should be used for %s", name);
	}

	unsigned kind = LLVMGetEnumAttributeKindForName(name, s.len);
	GB_ASSERT_MSG(kind != 0, "unknown attribute: %s", name);
	return LLVMCreateEnumAttribute(ctx, kind, value);
}

void lb_add_proc_attribute_at_index(lbProcedure *p, isize index, char const *name, u64 value) {
	LLVMAttributeRef attr = lb_create_enum_attribute(p->module->ctx, name, value);
	GB_ASSERT(attr != nullptr);
	LLVMAddAttributeAtIndex(p->value, cast(unsigned)index, attr);
}

void lb_add_proc_attribute_at_index(lbProcedure *p, isize index, char const *name) {
	lb_add_proc_attribute_at_index(p, index, name, 0);
}

void lb_add_attribute_to_proc(lbModule *m, LLVMValueRef proc_value, char const *name, u64 value=0) {
	LLVMAddAttributeAtIndex(proc_value, LLVMAttributeIndex_FunctionIndex, lb_create_enum_attribute(m->ctx, name, value));
}


void lb_ensure_abi_function_type(lbModule *m, lbProcedure *p) {
	if (p->abi_function_type != nullptr) {
		return;
	}
	auto hash = hash_type(p->type);
	lbFunctionType **ft_found = map_get(&m->function_type_map, hash);
	if (ft_found == nullptr) {
		LLVMTypeRef llvm_proc_type = lb_type(p->module, p->type);
		ft_found = map_get(&m->function_type_map, hash);
	}
	GB_ASSERT(ft_found != nullptr);
	p->abi_function_type = *ft_found;
	GB_ASSERT(p->abi_function_type != nullptr);
}

lbProcedure *lb_create_procedure(lbModule *m, Entity *entity, bool ignore_body) {
	GB_ASSERT(entity != nullptr);
	GB_ASSERT(entity->kind == Entity_Procedure);

	String link_name = {};

	if (ignore_body) {
		lbModule *other_module = lb_pkg_module(m->gen, entity->pkg);
		link_name = lb_get_entity_name(other_module, entity);
	} else {
		link_name = lb_get_entity_name(m, entity);
	}

	{
		StringHashKey key = string_hash_string(link_name);
		lbValue *found = string_map_get(&m->members, key);
		if (found) {
			lb_add_entity(m, entity, *found);
			return string_map_must_get(&m->procedures, key);
		}
	}


	lbProcedure *p = gb_alloc_item(permanent_allocator(), lbProcedure);

	p->module = m;
	entity->code_gen_module = m;
	entity->code_gen_procedure = p;
	p->entity = entity;
	p->name = link_name;

	DeclInfo *decl = entity->decl_info;

	ast_node(pl, ProcLit, decl->proc_lit);
	Type *pt = base_type(entity->type);
	GB_ASSERT(pt->kind == Type_Proc);

	p->type           = entity->type;
	p->type_expr      = decl->type_expr;
	p->body           = pl->body;
	p->inlining       = pl->inlining;
	p->is_foreign     = entity->Procedure.is_foreign;
	p->is_export      = entity->Procedure.is_export;
	p->is_entry_point = false;

	gbAllocator a = heap_allocator();
	p->children.allocator      = a;
	p->params.allocator        = a;
	p->defer_stmts.allocator   = a;
	p->blocks.allocator        = a;
	p->branch_blocks.allocator = a;
	p->context_stack.allocator = a;
	p->scope_stack.allocator   = a;

	if (p->is_foreign) {
		lb_add_foreign_library_path(p->module, entity->Procedure.foreign_library);
	}

	char *c_link_name = alloc_cstring(permanent_allocator(), p->name);
	LLVMTypeRef func_ptr_type = lb_type(m, p->type);
	LLVMTypeRef func_type = LLVMGetElementType(func_ptr_type);

	p->value = LLVMAddFunction(m->mod, c_link_name, func_type);

	lb_ensure_abi_function_type(m, p);
	lb_add_function_type_attributes(p->value, p->abi_function_type, p->abi_function_type->calling_convention);
	if (false) {
		lbCallingConventionKind cc_kind = lbCallingConvention_C;
		// TODO(bill): Clean up this logic
		if (!is_arch_wasm()) {
			cc_kind = lb_calling_convention_map[pt->Proc.calling_convention];
		}
		LLVMSetFunctionCallConv(p->value, cc_kind);
	}


	if (pt->Proc.diverging) {
		lb_add_attribute_to_proc(m, p->value, "noreturn");
	}

	if (pt->Proc.calling_convention == ProcCC_Naked) {
		lb_add_attribute_to_proc(m, p->value, "naked");
	}

	switch (p->inlining) {
	case ProcInlining_inline:
		lb_add_attribute_to_proc(m, p->value, "alwaysinline");
		break;
	case ProcInlining_no_inline:
		lb_add_attribute_to_proc(m, p->value, "noinline");
		break;
	}

	if (entity->flags & EntityFlag_Cold) {
		lb_add_attribute_to_proc(m, p->value, "cold");
	}

	switch (entity->Procedure.optimization_mode) {
	case ProcedureOptimizationMode_None:
		lb_add_attribute_to_proc(m, p->value, "optnone");
		break;
	case ProcedureOptimizationMode_Minimal:
		lb_add_attribute_to_proc(m, p->value, "optnone");
		break;
	case ProcedureOptimizationMode_Size:
		lb_add_attribute_to_proc(m, p->value, "optsize");
		break;
	case ProcedureOptimizationMode_Speed:
		// TODO(bill): handle this correctly
		lb_add_attribute_to_proc(m, p->value, "optsize");
		break;
	}



	// lbCallingConventionKind cc_kind = lbCallingConvention_C;
	// // TODO(bill): Clean up this logic
	// if (build_context.metrics.os != TargetOs_js)  {
	// 	cc_kind = lb_calling_convention_map[pt->Proc.calling_convention];
	// }
	// LLVMSetFunctionCallConv(p->value, cc_kind);
	lbValue proc_value = {p->value, p->type};
	lb_add_entity(m, entity,  proc_value);
	lb_add_member(m, p->name, proc_value);
	lb_add_procedure_value(m, p);

	if (p->is_export) {
		LLVMSetLinkage(p->value, LLVMDLLExportLinkage);
		LLVMSetDLLStorageClass(p->value, LLVMDLLExportStorageClass);
		LLVMSetVisibility(p->value, LLVMDefaultVisibility);

		if (is_arch_wasm()) {
			char const *export_name = alloc_cstring(permanent_allocator(), p->name);
			LLVMAddTargetDependentFunctionAttr(p->value, "wasm-export-name", export_name);
		}
	} else if (!p->is_foreign) {
		if (!USE_SEPARTE_MODULES) {
			LLVMSetLinkage(p->value, LLVMInternalLinkage);

			// NOTE(bill): if a procedure is defined in package runtime and uses a custom link name,
			// then it is very likely it is required by LLVM and thus cannot have internal linkage
			if (entity->pkg != nullptr && entity->pkg->kind == Package_Runtime && p->body != nullptr) {
				GB_ASSERT(entity->kind == Entity_Procedure);
				if (entity->Procedure.link_name != "") {
					LLVMSetLinkage(p->value, LLVMExternalLinkage);
				}
			}
		}
	}

	if (p->is_foreign) {
		if (is_arch_wasm()) {
			char const *import_name = alloc_cstring(permanent_allocator(), p->name);
			char const *module_name = "env";
			if (entity->Procedure.foreign_library != nullptr) {
				Entity *foreign_library = entity->Procedure.foreign_library;
				GB_ASSERT(foreign_library->kind == Entity_LibraryName);
				if (foreign_library->LibraryName.paths.count > 0)  {
					module_name = alloc_cstring(permanent_allocator(), foreign_library->LibraryName.paths[0]);
				}
			}
			LLVMAddTargetDependentFunctionAttr(p->value, "wasm-import-name",   import_name);
			LLVMAddTargetDependentFunctionAttr(p->value, "wasm-import-module", module_name);
		}
	}

	// NOTE(bill): offset==0 is the return value
	isize offset = 1;
	if (pt->Proc.return_by_pointer) {
		offset = 2;
	}

	isize parameter_index = 0;
	if (pt->Proc.param_count) {
		TypeTuple *params = &pt->Proc.params->Tuple;
		for (isize i = 0; i < pt->Proc.param_count; i++) {
			Entity *e = params->variables[i];
			Type *original_type = e->type;
			if (e->kind != Entity_Variable) continue;

			if (i+1 == params->variables.count && pt->Proc.c_vararg) {
				continue;
			}

			if (e->flags&EntityFlag_NoAlias) {
				lb_add_proc_attribute_at_index(p, offset+parameter_index, "noalias");
			}
			parameter_index += 1;
		}
	}

	if (ignore_body) {
		p->body = nullptr;
		LLVMSetLinkage(p->value, LLVMExternalLinkage);
	}


	if (m->debug_builder) { // Debug Information
		Type *bt = base_type(p->type);

		unsigned line = cast(unsigned)entity->token.pos.line;

		LLVMMetadataRef scope = nullptr;
		LLVMMetadataRef file = nullptr;
		LLVMMetadataRef type = nullptr;
		scope = p->module->debug_compile_unit;
		type = lb_debug_type_internal_proc(m, bt);

		if (entity->file != nullptr) {
			file = lb_get_llvm_metadata(m, entity->file);
			scope = file;
		} else if (entity->identifier != nullptr && entity->identifier->file != nullptr) {
			file = lb_get_llvm_metadata(m, entity->identifier->file);
			scope = file;
		} else if (entity->scope != nullptr) {
			file = lb_get_llvm_metadata(m, entity->scope->file);
			scope = file;
		}
		GB_ASSERT_MSG(file != nullptr, "%.*s", LIT(entity->token.string));

		// LLVMBool is_local_to_unit = !entity->Procedure.is_export;
		LLVMBool is_local_to_unit = false;
		LLVMBool is_definition = p->body != nullptr;
		unsigned scope_line = line;
		u32 flags = LLVMDIFlagStaticMember;
		LLVMBool is_optimized = false;
		if (bt->Proc.diverging) {
			flags |= LLVMDIFlagNoReturn;
		}
		if (p->body == nullptr) {
			flags |= LLVMDIFlagPrototyped;
			is_optimized = false;
		}

		if (p->body != nullptr) {
			p->debug_info = LLVMDIBuilderCreateFunction(m->debug_builder, scope,
				cast(char const *)entity->token.string.text, entity->token.string.len,
				cast(char const *)p->name.text, p->name.len,
				file, line, type,
				is_local_to_unit, is_definition,
				scope_line, cast(LLVMDIFlags)flags, is_optimized
			);
			GB_ASSERT(p->debug_info != nullptr);
			LLVMSetSubprogram(p->value, p->debug_info);
			lb_set_llvm_metadata(m, p, p->debug_info);
		}
	}

	return p;
}

lbProcedure *lb_create_dummy_procedure(lbModule *m, String link_name, Type *type) {
	{
		lbValue *found = string_map_get(&m->members, link_name);
		GB_ASSERT(found == nullptr);
	}

	lbProcedure *p = gb_alloc_item(permanent_allocator(), lbProcedure);

	p->module = m;
	p->name = link_name;

	p->type           = type;
	p->type_expr      = nullptr;
	p->body           = nullptr;
	p->tags           = 0;
	p->inlining       = ProcInlining_none;
	p->is_foreign     = false;
	p->is_export      = false;
	p->is_entry_point = false;

	gbAllocator a = permanent_allocator();
	p->children.allocator      = a;
	p->params.allocator        = a;
	p->defer_stmts.allocator   = a;
	p->blocks.allocator        = a;
	p->branch_blocks.allocator = a;
	p->context_stack.allocator = a;


	char *c_link_name = alloc_cstring(permanent_allocator(), p->name);
	LLVMTypeRef func_ptr_type = lb_type(m, p->type);
	LLVMTypeRef func_type = LLVMGetElementType(func_ptr_type);

	p->value = LLVMAddFunction(m->mod, c_link_name, func_type);

	Type *pt = p->type;
	lbCallingConventionKind cc_kind = lbCallingConvention_C;
	// TODO(bill): Clean up this logic
	if (!is_arch_wasm()) {
		cc_kind = lb_calling_convention_map[pt->Proc.calling_convention];
	}
	LLVMSetFunctionCallConv(p->value, cc_kind);
	lbValue proc_value = {p->value, p->type};
	lb_add_member(m, p->name, proc_value);
	lb_add_procedure_value(m, p);


	// NOTE(bill): offset==0 is the return value
	isize offset = 1;
	if (pt->Proc.return_by_pointer) {
		lb_add_proc_attribute_at_index(p, 1, "sret");
		lb_add_proc_attribute_at_index(p, 1, "noalias");
		offset = 2;
	}

	isize parameter_index = 0;
	if (pt->Proc.calling_convention == ProcCC_Odin) {
		lb_add_proc_attribute_at_index(p, offset+parameter_index, "noalias");
		lb_add_proc_attribute_at_index(p, offset+parameter_index, "nonnull");
		lb_add_proc_attribute_at_index(p, offset+parameter_index, "nocapture");
	}

	return p;
}


lbValue lb_value_param(lbProcedure *p, Entity *e, Type *abi_type, i32 index, lbParamPasskind *kind_) {
	lbParamPasskind kind = lbParamPass_Value;

	if (e != nullptr && !are_types_identical(abi_type, e->type)) {
		if (is_type_pointer(abi_type)) {
			GB_ASSERT(e->kind == Entity_Variable);
			Type *av = core_type(type_deref(abi_type));
			if (are_types_identical(av, core_type(e->type))) {
				kind = lbParamPass_Pointer;
				if (e->flags&EntityFlag_Value) {
					kind = lbParamPass_ConstRef;
				}
			} else {
				kind = lbParamPass_BitCast;
			}
		} else if (is_type_integer(abi_type)) {
			kind = lbParamPass_Integer;
		} else if (abi_type == t_llvm_bool) {
			kind = lbParamPass_Value;
		} else if (is_type_boolean(abi_type)) {
			kind = lbParamPass_Integer;
		} else if (is_type_simd_vector(abi_type)) {
			kind = lbParamPass_BitCast;
		} else if (is_type_float(abi_type)) {
			kind = lbParamPass_BitCast;
		} else if (is_type_tuple(abi_type)) {
			kind = lbParamPass_Tuple;
		} else if (is_type_proc(abi_type)) {
			kind = lbParamPass_Value;
		} else {
			GB_PANIC("Invalid abi type pass kind %s", type_to_string(abi_type));
		}
	}

	if (kind_) *kind_ = kind;
	lbValue res = {};
	res.value = LLVMGetParam(p->value, cast(unsigned)index);
	res.type = abi_type;
	return res;
}

Type *struct_type_from_systemv_distribute_struct_fields(Type *abi_type) {
	GB_ASSERT(is_type_tuple(abi_type));
	Type *final_type = alloc_type_struct();
	final_type->Struct.fields = abi_type->Tuple.variables;
	return final_type;
}


void lb_start_block(lbProcedure *p, lbBlock *b) {
	GB_ASSERT(b != nullptr);
	if (!b->appended) {
		b->appended = true;
		LLVMAppendExistingBasicBlock(p->value, b->block);
	}
	LLVMPositionBuilderAtEnd(p->builder, b->block);
	p->curr_block = b;
}

LLVMValueRef OdinLLVMBuildTransmute(lbProcedure *p, LLVMValueRef val, LLVMTypeRef dst_type) {
	LLVMContextRef ctx = p->module->ctx;
	LLVMTypeRef src_type = LLVMTypeOf(val);

	if (src_type == dst_type) {
		return val;
	}

	i64 src_size = lb_sizeof(src_type);
	i64 dst_size = lb_sizeof(dst_type);
	i64 src_align = lb_alignof(src_type);
	i64 dst_align = lb_alignof(dst_type);
	if (LLVMIsALoadInst(val)) {
		src_align = gb_min(src_align, LLVMGetAlignment(val));
	}

	LLVMTypeKind src_kind = LLVMGetTypeKind(src_type);
	LLVMTypeKind dst_kind = LLVMGetTypeKind(dst_type);

	if (dst_type == LLVMInt1TypeInContext(ctx)) {
		GB_ASSERT(lb_is_type_kind(src_type, LLVMIntegerTypeKind));
		return LLVMBuildICmp(p->builder, LLVMIntNE, val, LLVMConstNull(src_type), "");
	} else if (src_type == LLVMInt1TypeInContext(ctx)) {
		GB_ASSERT(lb_is_type_kind(src_type, LLVMIntegerTypeKind));
		return LLVMBuildZExtOrBitCast(p->builder, val, dst_type, "");
	}

	if (src_size != dst_size) {
		if ((lb_is_type_kind(src_type, LLVMVectorTypeKind) ^ lb_is_type_kind(dst_type, LLVMVectorTypeKind))) {
			// Okay
		} else {
			goto general_end;
		}
	}


	if (src_kind == dst_kind) {
		if (src_kind == LLVMPointerTypeKind) {
			return LLVMBuildPointerCast(p->builder, val, dst_type, "");
		} else if (src_kind == LLVMArrayTypeKind) {
			// ignore
		} else if (src_kind != LLVMStructTypeKind) {
			return LLVMBuildBitCast(p->builder, val, dst_type, "");
		}
	} else {
		if (src_kind == LLVMPointerTypeKind && dst_kind == LLVMIntegerTypeKind) {
			return LLVMBuildPtrToInt(p->builder, val, dst_type, "");
		} else if (src_kind == LLVMIntegerTypeKind && dst_kind == LLVMPointerTypeKind) {
			return LLVMBuildIntToPtr(p->builder, val, dst_type, "");
		}
	}

general_end:;
	// make the alignment big if necessary
	if (LLVMIsALoadInst(val) && src_align < dst_align) {
		LLVMValueRef val_ptr = LLVMGetOperand(val, 0);
		if (LLVMGetInstructionOpcode(val_ptr) == LLVMAlloca) {
			src_align = gb_max(LLVMGetAlignment(val_ptr), dst_align);
			LLVMSetAlignment(val_ptr, cast(unsigned)src_align);
		}
	}

	src_size = align_formula(src_size, src_align);
	dst_size = align_formula(dst_size, dst_align);

	if (LLVMIsALoadInst(val) && (src_size >= dst_size && src_align >= dst_align)) {
		LLVMValueRef val_ptr = LLVMGetOperand(val, 0);
		val_ptr = LLVMBuildPointerCast(p->builder, val_ptr, LLVMPointerType(dst_type, 0), "");
		LLVMValueRef loaded_val = LLVMBuildLoad(p->builder, val_ptr, "");

		// LLVMSetAlignment(loaded_val, gb_min(src_align, dst_align));

		return loaded_val;
	} else {
		GB_ASSERT(p->decl_block != p->curr_block);
		LLVMPositionBuilderAtEnd(p->builder, p->decl_block->block);

		LLVMValueRef ptr = LLVMBuildAlloca(p->builder, dst_type, "");
		LLVMPositionBuilderAtEnd(p->builder, p->curr_block->block);
		i64 max_align = gb_max(lb_alignof(src_type), lb_alignof(dst_type));
		max_align = gb_max(max_align, 4);
		LLVMSetAlignment(ptr, cast(unsigned)max_align);

		LLVMValueRef nptr = LLVMBuildPointerCast(p->builder, ptr, LLVMPointerType(src_type, 0), "");
		LLVMBuildStore(p->builder, val, nptr);

		return LLVMBuildLoad(p->builder, ptr, "");
	}
}


void lb_add_debug_local_variable(lbProcedure *p, LLVMValueRef ptr, Type *type, Token const &token) {
	if (p->debug_info == nullptr) {
		return;
	}
	if (type == nullptr) {
		return;
	}
	if (type == t_invalid) {
		return;
	}
	if (p->body == nullptr) {
		return;
	}

	lbModule *m = p->module;
	String const &name = token.string;
	if (name == "" || name == "_") {
		return;
	}

	if (lb_get_llvm_metadata(m, ptr) != nullptr) {
		// Already been set
		return;
	}


	AstFile *file = p->body->file;

	LLVMMetadataRef llvm_scope = lb_get_current_debug_scope(p);
	LLVMMetadataRef llvm_file = lb_get_llvm_metadata(m, file);
	GB_ASSERT(llvm_scope != nullptr);
	if (llvm_file == nullptr) {
		llvm_file = LLVMDIScopeGetFile(llvm_scope);
	}

	if (llvm_file == nullptr) {
		return;
	}

	unsigned alignment_in_bits = cast(unsigned)(8*type_align_of(type));

	LLVMDIFlags flags = LLVMDIFlagZero;
	LLVMBool always_preserve = build_context.optimization_level == 0;

	LLVMMetadataRef debug_type = lb_debug_type(m, type);

	LLVMMetadataRef var_info = LLVMDIBuilderCreateAutoVariable(
		m->debug_builder, llvm_scope,
		cast(char const *)name.text, cast(size_t)name.len,
		llvm_file, token.pos.line,
		debug_type,
		always_preserve, flags, alignment_in_bits
	);

	LLVMValueRef storage = ptr;
	LLVMValueRef instr = ptr;
	LLVMMetadataRef llvm_debug_loc = lb_debug_location_from_token_pos(p, token.pos);
	LLVMMetadataRef llvm_expr = LLVMDIBuilderCreateExpression(m->debug_builder, nullptr, 0);
	lb_set_llvm_metadata(m, ptr, llvm_expr);
	LLVMDIBuilderInsertDeclareBefore(m->debug_builder, storage, var_info, llvm_expr, llvm_debug_loc, instr);
}

void lb_add_debug_context_variable(lbProcedure *p, lbAddr const &ctx) {
	if (!p->debug_info || !p->body) {
		return;
	}
	LLVMMetadataRef loc = LLVMGetCurrentDebugLocation2(p->builder);
	if (!loc) {
		return;
	}
	TokenPos pos = {};

	pos.file_id = p->body->file ? p->body->file->id : 0;
	pos.line = LLVMDILocationGetLine(loc);
	pos.column = LLVMDILocationGetColumn(loc);

	Token token = {};
	token.kind = Token_context;
	token.string = str_lit("context");
	token.pos = pos;

	lb_add_debug_local_variable(p, ctx.addr.value, t_context, token);
}


void lb_begin_procedure_body(lbProcedure *p) {
	DeclInfo *decl = decl_info_of_entity(p->entity);
	if (decl != nullptr) {
		for_array(i, decl->labels) {
			BlockLabel bl = decl->labels[i];
			lbBranchBlocks bb = {bl.label, nullptr, nullptr};
			array_add(&p->branch_blocks, bb);
		}
	}

	p->builder = LLVMCreateBuilderInContext(p->module->ctx);

	p->decl_block  = lb_create_block(p, "decls", true);
	p->entry_block = lb_create_block(p, "entry", true);
	lb_start_block(p, p->entry_block);

	GB_ASSERT(p->type != nullptr);

	lb_ensure_abi_function_type(p->module, p);
	{
		lbFunctionType *ft = p->abi_function_type;

		unsigned param_offset = 0;

		lbValue return_ptr_value = {};
		if (ft->ret.kind == lbArg_Indirect) {
			// NOTE(bill): this must be parameter 0

			String name = str_lit("agg.result");

			Type *ptr_type = alloc_type_pointer(reduce_tuple_to_single_type(p->type->Proc.results));
			Entity *e = alloc_entity_param(nullptr, make_token_ident(name), ptr_type, false, false);
			e->flags |= EntityFlag_Sret | EntityFlag_NoAlias;

			return_ptr_value.value = LLVMGetParam(p->value, 0);
			LLVMSetValueName2(return_ptr_value.value, cast(char const *)name.text, name.len);
			return_ptr_value.type = ptr_type;
			p->return_ptr = lb_addr(return_ptr_value);

			lb_add_entity(p->module, e, return_ptr_value);

			param_offset += 1;
		}

		if (p->type->Proc.params != nullptr) {
			TypeTuple *params = &p->type->Proc.params->Tuple;

			unsigned param_index = 0;
			for_array(i, params->variables) {
				Entity *e = params->variables[i];
				if (e->kind != Entity_Variable) {
					continue;
				}

				lbArgType *arg_type = &ft->args[param_index];
				if (arg_type->kind == lbArg_Ignore) {
					continue;
				} else if (arg_type->kind == lbArg_Direct) {
					lbParamPasskind kind = lbParamPass_Value;
					LLVMTypeRef param_type = lb_type(p->module, e->type);
					if (param_type != arg_type->type) {
						kind = lbParamPass_BitCast;
					}
					LLVMValueRef value = LLVMGetParam(p->value, param_offset+param_index);

					value = OdinLLVMBuildTransmute(p, value, param_type);

					lbValue param = {};
					param.value = value;
					param.type = e->type;
					array_add(&p->params, param);

					if (e->token.string.len != 0) {
						lbAddr l = lb_add_local(p, e->type, e, false, param_index);
						lb_addr_store(p, l, param);
					}

					param_index += 1;
				} else if (arg_type->kind == lbArg_Indirect) {
					LLVMValueRef value_ptr = LLVMGetParam(p->value, param_offset+param_index);
					LLVMValueRef value = LLVMBuildLoad(p->builder, value_ptr, "");

					lbValue param = {};
					param.value = value;
					param.type = e->type;
					array_add(&p->params, param);

					lbValue ptr = {};
					ptr.value = value_ptr;
					ptr.type = alloc_type_pointer(e->type);

					lb_add_entity(p->module, e, ptr);
					param_index += 1;
				}
			}
		}

		if (p->type->Proc.has_named_results) {
			GB_ASSERT(p->type->Proc.result_count > 0);
			TypeTuple *results = &p->type->Proc.results->Tuple;

			for_array(i, results->variables) {
				Entity *e = results->variables[i];
				GB_ASSERT(e->kind == Entity_Variable);

				if (e->token.string != "") {
					GB_ASSERT(!is_blank_ident(e->token));

					lbAddr res = {};
					if (return_ptr_value.value) {
						lbValue ptr = return_ptr_value;
						if (results->variables.count != 1) {
							ptr = lb_emit_struct_ep(p, ptr, cast(i32)i);
						}

						res = lb_addr(ptr);
						lb_add_entity(p->module, e, ptr);
					} else {
						res = lb_add_local(p, e->type, e);
					}

					if (e->Variable.param_value.kind != ParameterValue_Invalid) {
						lbValue c = lb_handle_param_value(p, e->type, e->Variable.param_value, e->token.pos);
						lb_addr_store(p, res, c);
					}
				}
			}
		}
	}
	if (p->type->Proc.calling_convention == ProcCC_Odin) {
		lb_push_context_onto_stack_from_implicit_parameter(p);
	}

	lb_start_block(p, p->entry_block);

	if (p->debug_info != nullptr) {
		TokenPos pos = {};
		if (p->body != nullptr) {
			pos = ast_token(p->body).pos;
		} else if (p->type_expr != nullptr) {
			pos = ast_token(p->type_expr).pos;
		} else if (p->entity != nullptr) {
			pos = p->entity->token.pos;
		}
		if (pos.file_id != 0) {
			LLVMSetCurrentDebugLocation2(p->builder, lb_debug_location_from_token_pos(p, pos));
		}

		if (p->context_stack.count != 0) {
			lb_add_debug_context_variable(p, lb_find_or_generate_context_ptr(p));
		}

	}

}

void lb_end_procedure_body(lbProcedure *p) {
	LLVMPositionBuilderAtEnd(p->builder, p->decl_block->block);
	LLVMBuildBr(p->builder, p->entry_block->block);
	LLVMPositionBuilderAtEnd(p->builder, p->curr_block->block);

	LLVMValueRef instr = nullptr;

	// Make sure there is a "ret void" at the end of a procedure with no return type
	if (p->type->Proc.result_count == 0) {
		instr = LLVMGetLastInstruction(p->curr_block->block);
		if (!lb_is_instr_terminating(instr)) {
			lb_emit_defer_stmts(p, lbDeferExit_Return, nullptr);
			LLVMBuildRetVoid(p->builder);
		}
	}

	LLVMBasicBlockRef first_block = LLVMGetFirstBasicBlock(p->value);
	LLVMBasicBlockRef block = nullptr;

	// Make sure every block terminates, and if not, make it unreachable
	for (block = first_block; block != nullptr; block = LLVMGetNextBasicBlock(block)) {
		instr = LLVMGetLastInstruction(block);
		if (instr == nullptr || !lb_is_instr_terminating(instr)) {
			LLVMPositionBuilderAtEnd(p->builder, block);
			LLVMBuildUnreachable(p->builder);
		}
	}

	p->curr_block = nullptr;
	p->state_flags = 0;
}
void lb_end_procedure(lbProcedure *p) {
	LLVMDisposeBuilder(p->builder);
}

void lb_add_edge(lbBlock *from, lbBlock *to) {
	LLVMValueRef instr = LLVMGetLastInstruction(from->block);
	if (instr == nullptr || !LLVMIsATerminatorInst(instr)) {
		array_add(&from->succs, to);
		array_add(&to->preds, from);
	}
}


lbBlock *lb_create_block(lbProcedure *p, char const *name, bool append) {
	lbBlock *b = gb_alloc_item(permanent_allocator(), lbBlock);
	b->block = LLVMCreateBasicBlockInContext(p->module->ctx, name);
	b->appended = false;
	if (append) {
		b->appended = true;
		LLVMAppendExistingBasicBlock(p->value, b->block);
	}

	b->scope = p->curr_scope;
	b->scope_index = p->scope_index;

	b->preds.allocator = heap_allocator();
	b->succs.allocator = heap_allocator();

	array_add(&p->blocks, b);

	return b;
}

void lb_emit_jump(lbProcedure *p, lbBlock *target_block) {
	if (p->curr_block == nullptr) {
		return;
	}
	LLVMValueRef last_instr = LLVMGetLastInstruction(p->curr_block->block);
	if (last_instr != nullptr && LLVMIsATerminatorInst(last_instr)) {
		return;
	}

	lb_add_edge(p->curr_block, target_block);
	LLVMBuildBr(p->builder, target_block->block);
	p->curr_block = nullptr;
}

void lb_emit_if(lbProcedure *p, lbValue cond, lbBlock *true_block, lbBlock *false_block) {
	lbBlock *b = p->curr_block;
	if (b == nullptr) {
		return;
	}
	LLVMValueRef last_instr = LLVMGetLastInstruction(p->curr_block->block);
	if (last_instr != nullptr && LLVMIsATerminatorInst(last_instr)) {
		return;
	}

	lb_add_edge(b, true_block);
	lb_add_edge(b, false_block);

	LLVMValueRef cv = cond.value;
	cv = LLVMBuildTruncOrBitCast(p->builder, cv, lb_type(p->module, t_llvm_bool), "");
	LLVMBuildCondBr(p->builder, cv, true_block->block, false_block->block);
}

bool lb_is_expr_untyped_const(Ast *expr) {
	auto const &tv = type_and_value_of_expr(expr);
	if (is_type_untyped(tv.type)) {
		return tv.value.kind != ExactValue_Invalid;
	}
	return false;
}

lbValue lb_expr_untyped_const_to_typed(lbModule *m, Ast *expr, Type *t) {
	GB_ASSERT(is_type_typed(t));
	auto const &tv = type_and_value_of_expr(expr);
	return lb_const_value(m, t, tv.value);
}

lbValue lb_build_cond(lbProcedure *p, Ast *cond, lbBlock *true_block, lbBlock *false_block) {
	GB_ASSERT(cond != nullptr);
	GB_ASSERT(true_block  != nullptr);
	GB_ASSERT(false_block != nullptr);

	switch (cond->kind) {
	case_ast_node(pe, ParenExpr, cond);
		return lb_build_cond(p, pe->expr, true_block, false_block);
	case_end;

	case_ast_node(ue, UnaryExpr, cond);
		if (ue->op.kind == Token_Not) {
			return lb_build_cond(p, ue->expr, false_block, true_block);
		}
	case_end;

	case_ast_node(be, BinaryExpr, cond);
		if (be->op.kind == Token_CmpAnd) {
			lbBlock *block = lb_create_block(p, "cmp.and");
			lb_build_cond(p, be->left, block, false_block);
			lb_start_block(p, block);
			return lb_build_cond(p, be->right, true_block, false_block);
		} else if (be->op.kind == Token_CmpOr) {
			lbBlock *block = lb_create_block(p, "cmp.or");
			lb_build_cond(p, be->left, true_block, block);
			lb_start_block(p, block);
			return lb_build_cond(p, be->right, true_block, false_block);
		}
	case_end;
	}

	lbValue v = {};
	if (lb_is_expr_untyped_const(cond)) {
		v = lb_expr_untyped_const_to_typed(p->module, cond, t_llvm_bool);
	} else {
		v = lb_build_expr(p, cond);
	}

	v = lb_emit_conv(p, v, t_llvm_bool);

	lb_emit_if(p, v, true_block, false_block);

	return v;
}

void lb_mem_zero_ptr_internal(lbProcedure *p, LLVMValueRef ptr, LLVMValueRef len, unsigned alignment) {
	bool is_inlinable = false;

	i64 const_len = 0;
	if (LLVMIsConstant(len)) {
		const_len = cast(i64)LLVMConstIntGetSExtValue(len);
		// TODO(bill): Determine when it is better to do the `*.inline` versions
		if (const_len <= 4*build_context.word_size) {
			is_inlinable = true;
		}
	}

	char const *name = "llvm.memset";
	if (is_inlinable) {
		name = "llvm.memset.inline";
	}

	LLVMTypeRef types[2] = {
		lb_type(p->module, t_rawptr),
		lb_type(p->module, t_int)
	};
	unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
	GB_ASSERT_MSG(id != 0, "Unable to find %s.%s.%s", name, LLVMPrintTypeToString(types[0]), LLVMPrintTypeToString(types[1]));
	LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

	LLVMValueRef args[4] = {};
	args[0] = LLVMBuildPointerCast(p->builder, ptr, types[0], "");
	args[1] = LLVMConstInt(LLVMInt8TypeInContext(p->module->ctx), 0, false);
	args[2] = LLVMBuildIntCast2(p->builder, len, types[1], /*signed*/false, "");
	args[3] = LLVMConstInt(LLVMInt1TypeInContext(p->module->ctx), 0, false); // is_volatile parameter

	LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
}

void lb_mem_zero_ptr(lbProcedure *p, LLVMValueRef ptr, Type *type, unsigned alignment) {
	LLVMTypeRef llvm_type = lb_type(p->module, type);

	LLVMTypeKind kind = LLVMGetTypeKind(llvm_type);

	switch (kind) {
	case LLVMStructTypeKind:
	case LLVMArrayTypeKind:
		{
			// NOTE(bill): Enforce zeroing through memset to make sure padding is zeroed too
			i32 sz = cast(i32)type_size_of(type);
			lb_mem_zero_ptr_internal(p, ptr, lb_const_int(p->module, t_int, sz).value, alignment);
		}
		break;
	default:
		LLVMBuildStore(p->builder, LLVMConstNull(lb_type(p->module, type)), ptr);
		break;
	}
}

lbAddr lb_add_local(lbProcedure *p, Type *type, Entity *e, bool zero_init, i32 param_index, bool force_no_init) {
	GB_ASSERT(p->decl_block != p->curr_block);
	LLVMPositionBuilderAtEnd(p->builder, p->decl_block->block);

	char const *name = "";
	if (e != nullptr) {
		// name = alloc_cstring(permanent_allocator(), e->token.string);
	}

	LLVMTypeRef llvm_type = lb_type(p->module, type);
	LLVMValueRef ptr = LLVMBuildAlloca(p->builder, llvm_type, name);

	// unsigned alignment = 16; // TODO(bill): Make this configurable
	unsigned alignment = cast(unsigned)lb_alignof(llvm_type);
	LLVMSetAlignment(ptr, alignment);

	LLVMPositionBuilderAtEnd(p->builder, p->curr_block->block);


	if (!zero_init && !force_no_init) {
		// If there is any padding of any kind, just zero init regardless of zero_init parameter
		LLVMTypeKind kind = LLVMGetTypeKind(llvm_type);
		if (kind == LLVMStructTypeKind) {
			i64 sz = type_size_of(type);
			if (type_size_of_struct_pretend_is_packed(type) != sz) {
				zero_init = true;
			}
		} else if (kind == LLVMArrayTypeKind) {
			zero_init = true;
		}
	}

	if (zero_init) {
		lb_mem_zero_ptr(p, ptr, type, alignment);
	}

	lbValue val = {};
	val.value = ptr;
	val.type = alloc_type_pointer(type);

	if (e != nullptr) {
		lb_add_entity(p->module, e, val);
		lb_add_debug_local_variable(p, ptr, type, e->token);
	}

	return lb_addr(val);
}

lbAddr lb_add_local_generated(lbProcedure *p, Type *type, bool zero_init) {
	return lb_add_local(p, type, nullptr, zero_init);
}

lbAddr lb_add_local_generated_temp(lbProcedure *p, Type *type, i64 min_alignment) {
	lbAddr res = lb_add_local(p, type, nullptr, false, 0, true);
	lb_try_update_alignment(res.addr, cast(unsigned)min_alignment);
	return res;
}


void lb_build_nested_proc(lbProcedure *p, AstProcLit *pd, Entity *e) {
	GB_ASSERT(pd->body != nullptr);
	lbModule *m = p->module;
	auto *min_dep_set = &m->info->minimum_dependency_set;

	if (ptr_set_exists(min_dep_set, e) == false) {
		// NOTE(bill): Nothing depends upon it so doesn't need to be built
		return;
	}

	// NOTE(bill): Generate a new name
	// parent.name-guid
	String original_name = e->token.string;
	String pd_name = original_name;
	if (e->Procedure.link_name.len > 0) {
		pd_name = e->Procedure.link_name;
	}


	isize name_len = p->name.len + 1 + pd_name.len + 1 + 10 + 1;
	char *name_text = gb_alloc_array(permanent_allocator(), char, name_len);

	i32 guid = cast(i32)p->children.count;
	name_len = gb_snprintf(name_text, name_len, "%.*s.%.*s-%d", LIT(p->name), LIT(pd_name), guid);
	String name = make_string(cast(u8 *)name_text, name_len-1);

	e->Procedure.link_name = name;

	lbProcedure *nested_proc = lb_create_procedure(p->module, e);
	e->code_gen_procedure = nested_proc;

	lbValue value = {};
	value.value = nested_proc->value;
	value.type = nested_proc->type;

	lb_add_entity(m, e, value);
	array_add(&p->children, nested_proc);
	array_add(&m->procedures_to_generate, nested_proc);
}


void lb_add_foreign_library_path(lbModule *m, Entity *e) {
	if (e == nullptr) {
		return;
	}
	GB_ASSERT(e->kind == Entity_LibraryName);
	GB_ASSERT(e->flags & EntityFlag_Used);

	for_array(i, e->LibraryName.paths) {
		String library_path = e->LibraryName.paths[i];
		if (library_path.len == 0) {
			continue;
		}

		bool ok = true;
		for_array(path_index, m->foreign_library_paths) {
			String path = m->foreign_library_paths[path_index];
	#if defined(GB_SYSTEM_WINDOWS)
			if (str_eq_ignore_case(path, library_path)) {
	#else
			if (str_eq(path, library_path)) {
	#endif
				ok = false;
				break;
			}
		}

		if (ok) {
			array_add(&m->foreign_library_paths, library_path);
		}
	}
}



void lb_build_constant_value_decl(lbProcedure *p, AstValueDecl *vd) {
	if (vd == nullptr || vd->is_mutable) {
		return;
	}

	auto *min_dep_set = &p->module->info->minimum_dependency_set;

	static i32 global_guid = 0;

	for_array(i, vd->names) {
		Ast *ident = vd->names[i];
		GB_ASSERT(ident->kind == Ast_Ident);
		Entity *e = entity_of_node(ident);
		GB_ASSERT(e != nullptr);
		if (e->kind != Entity_TypeName) {
			continue;
		}

		bool polymorphic_struct = false;
		if (e->type != nullptr && e->kind == Entity_TypeName) {
			Type *bt = base_type(e->type);
			if (bt->kind == Type_Struct) {
				polymorphic_struct = bt->Struct.is_polymorphic;
			}
		}

		if (!polymorphic_struct && !ptr_set_exists(min_dep_set, e)) {
			continue;
		}

		if (e->TypeName.ir_mangled_name.len != 0) {
			// NOTE(bill): Already set
			continue;
		}

		lb_set_nested_type_name_ir_mangled_name(e, p);
	}

	for_array(i, vd->names) {
		Ast *ident = vd->names[i];
		GB_ASSERT(ident->kind == Ast_Ident);
		Entity *e = entity_of_node(ident);
		GB_ASSERT(e != nullptr);
		if (e->kind != Entity_Procedure) {
			continue;
		}
		GB_ASSERT (vd->values[i] != nullptr);

		Ast *value = unparen_expr(vd->values[i]);
		if (value->kind != Ast_ProcLit) {
			continue; // It's an alias
		}

		CheckerInfo *info = p->module->info;
		DeclInfo *decl = decl_info_of_entity(e);
		ast_node(pl, ProcLit, decl->proc_lit);
		if (pl->body != nullptr) {
			auto *found = map_get(&info->gen_procs, hash_pointer(ident));
			if (found) {
				auto procs = *found;
				for_array(i, procs) {
					Entity *e = procs[i];
					if (!ptr_set_exists(min_dep_set, e)) {
						continue;
					}
					DeclInfo *d = decl_info_of_entity(e);
					lb_build_nested_proc(p, &d->proc_lit->ProcLit, e);
				}
			} else {
				lb_build_nested_proc(p, pl, e);
			}
		} else {

			// FFI - Foreign function interace
			String original_name = e->token.string;
			String name = original_name;

			if (e->Procedure.is_foreign) {
				lb_add_foreign_library_path(p->module, e->Procedure.foreign_library);
			}

			if (e->Procedure.link_name.len > 0) {
				name = e->Procedure.link_name;
			}

			lbValue *prev_value = string_map_get(&p->module->members, name);
			if (prev_value != nullptr) {
				// NOTE(bill): Don't do mutliple declarations in the IR
				return;
			}

			e->Procedure.link_name = name;

			lbProcedure *nested_proc = lb_create_procedure(p->module, e);

			lbValue value = {};
			value.value = nested_proc->value;
			value.type = nested_proc->type;

			array_add(&p->module->procedures_to_generate, nested_proc);
			array_add(&p->children, nested_proc);
			string_map_set(&p->module->members, name, value);
		}
	}
}


void lb_build_stmt_list(lbProcedure *p, Slice<Ast *> const &stmts) {
	for_array(i, stmts) {
		Ast *stmt = stmts[i];
		switch (stmt->kind) {
		case_ast_node(vd, ValueDecl, stmt);
			lb_build_constant_value_decl(p, vd);
		case_end;
		case_ast_node(fb, ForeignBlockDecl, stmt);
			ast_node(block, BlockStmt, fb->body);
			lb_build_stmt_list(p, block->stmts);
		case_end;
		}
	}
	for_array(i, stmts) {
		lb_build_stmt(p, stmts[i]);
	}
}

lbBranchBlocks lb_lookup_branch_blocks(lbProcedure *p, Ast *ident) {
	GB_ASSERT(ident->kind == Ast_Ident);
	Entity *e = entity_of_node(ident);
	GB_ASSERT(e->kind == Entity_Label);
	for_array(i, p->branch_blocks) {
		lbBranchBlocks *b = &p->branch_blocks[i];
		if (b->label == e->Label.node) {
			return *b;
		}
	}

	GB_PANIC("Unreachable");
	lbBranchBlocks empty = {};
	return empty;
}


lbTargetList *lb_push_target_list(lbProcedure *p, Ast *label, lbBlock *break_, lbBlock *continue_, lbBlock *fallthrough_) {
	lbTargetList *tl = gb_alloc_item(permanent_allocator(), lbTargetList);
	tl->prev = p->target_list;
	tl->break_ = break_;
	tl->continue_ = continue_;
	tl->fallthrough_ = fallthrough_;
	p->target_list = tl;

	if (label != nullptr) { // Set label blocks
		GB_ASSERT(label->kind == Ast_Label);

		for_array(i, p->branch_blocks) {
			lbBranchBlocks *b = &p->branch_blocks[i];
			GB_ASSERT(b->label != nullptr && label != nullptr);
			GB_ASSERT(b->label->kind == Ast_Label);
			if (b->label == label) {
				b->break_    = break_;
				b->continue_ = continue_;
				return tl;
			}
		}

		GB_PANIC("Unreachable");
	}

	return tl;
}

void lb_pop_target_list(lbProcedure *p) {
	p->target_list = p->target_list->prev;
}




void lb_open_scope(lbProcedure *p, Scope *s) {
	lbModule *m = p->module;
	if (m->debug_builder) {
		LLVMMetadataRef curr_metadata = lb_get_llvm_metadata(m, s);
		if (s != nullptr && s->node != nullptr && curr_metadata == nullptr) {
			Token token = ast_token(s->node);
			unsigned line = cast(unsigned)token.pos.line;
			unsigned column = cast(unsigned)token.pos.column;

			LLVMMetadataRef file = nullptr;
			if (s->node->file != nullptr) {
				file = lb_get_llvm_metadata(m, s->node->file);
			}
			LLVMMetadataRef scope = nullptr;
			if (p->scope_stack.count > 0) {
				scope = lb_get_llvm_metadata(m, p->scope_stack[p->scope_stack.count-1]);
			}
			if (scope == nullptr) {
				scope = lb_get_llvm_metadata(m, p);
			}
			GB_ASSERT_MSG(scope != nullptr, "%.*s", LIT(p->name));

			if (m->debug_builder) {
				LLVMMetadataRef res = LLVMDIBuilderCreateLexicalBlock(m->debug_builder, scope,
					file, line, column
				);
				lb_set_llvm_metadata(m, s, res);
			}
		}
	}

	p->scope_index += 1;
	array_add(&p->scope_stack, s);

}

void lb_close_scope(lbProcedure *p, lbDeferExitKind kind, lbBlock *block, bool pop_stack=true) {
	lb_emit_defer_stmts(p, kind, block);
	GB_ASSERT(p->scope_index > 0);

	// NOTE(bill): Remove `context`s made in that scope
	while (p->context_stack.count > 0) {
		lbContextData *ctx = &p->context_stack[p->context_stack.count-1];
		if (ctx->scope_index >= p->scope_index) {
			array_pop(&p->context_stack);
		} else {
			break;
		}

	}

	p->scope_index -= 1;
	array_pop(&p->scope_stack);
}

void lb_build_when_stmt(lbProcedure *p, AstWhenStmt *ws) {
	TypeAndValue tv = type_and_value_of_expr(ws->cond);
	GB_ASSERT(is_type_boolean(tv.type));
	GB_ASSERT(tv.value.kind == ExactValue_Bool);
	if (tv.value.value_bool) {
		lb_build_stmt_list(p, ws->body->BlockStmt.stmts);
	} else if (ws->else_stmt) {
		switch (ws->else_stmt->kind) {
		case Ast_BlockStmt:
			lb_build_stmt_list(p, ws->else_stmt->BlockStmt.stmts);
			break;
		case Ast_WhenStmt:
			lb_build_when_stmt(p, &ws->else_stmt->WhenStmt);
			break;
		default:
			GB_PANIC("Invalid 'else' statement in 'when' statement");
			break;
		}
	}
}



void lb_build_range_indexed(lbProcedure *p, lbValue expr, Type *val_type, lbValue count_ptr,
                            lbValue *val_, lbValue *idx_, lbBlock **loop_, lbBlock **done_) {
	lbModule *m = p->module;

	lbValue count = {};
	Type *expr_type = base_type(type_deref(expr.type));
	switch (expr_type->kind) {
	case Type_Array:
		count = lb_const_int(m, t_int, expr_type->Array.count);
		break;
	}

	lbValue val = {};
	lbValue idx = {};
	lbBlock *loop = nullptr;
	lbBlock *done = nullptr;
	lbBlock *body = nullptr;


	lbAddr index = lb_add_local_generated(p, t_int, false);
	lb_addr_store(p, index, lb_const_int(m, t_int, cast(u64)-1));

	loop = lb_create_block(p, "for.index.loop");
	lb_emit_jump(p, loop);
	lb_start_block(p, loop);

	lbValue incr = lb_emit_arith(p, Token_Add, lb_addr_load(p, index), lb_const_int(m, t_int, 1), t_int);
	lb_addr_store(p, index, incr);

	body = lb_create_block(p, "for.index.body");
	done = lb_create_block(p, "for.index.done");
	if (count.value == nullptr) {
		GB_ASSERT(count_ptr.value != nullptr);
		count = lb_emit_load(p, count_ptr);
	}
	lbValue cond = lb_emit_comp(p, Token_Lt, incr, count);
	lb_emit_if(p, cond, body, done);
	lb_start_block(p, body);

	idx = lb_addr_load(p, index);
	switch (expr_type->kind) {
	case Type_Array: {
		if (val_type != nullptr) {
			val = lb_emit_load(p, lb_emit_array_ep(p, expr, idx));
		}
		break;
	}
	case Type_EnumeratedArray: {
		if (val_type != nullptr) {
			val = lb_emit_load(p, lb_emit_array_ep(p, expr, idx));
			// NOTE(bill): Override the idx value for the enumeration
			Type *index_type = expr_type->EnumeratedArray.index;
			if (compare_exact_values(Token_NotEq, expr_type->EnumeratedArray.min_value, exact_value_u64(0))) {
				idx = lb_emit_arith(p, Token_Add, idx, lb_const_value(m, index_type, expr_type->EnumeratedArray.min_value), index_type);
			}
		}
		break;
	}
	case Type_Slice: {
		if (val_type != nullptr) {
			lbValue elem = lb_slice_elem(p, expr);
			val = lb_emit_load(p, lb_emit_ptr_offset(p, elem, idx));
		}
		break;
	}
	case Type_DynamicArray: {
		if (val_type != nullptr) {
			lbValue elem = lb_emit_struct_ep(p, expr, 0);
			elem = lb_emit_load(p, elem);
			val = lb_emit_load(p, lb_emit_ptr_offset(p, elem, idx));
		}
		break;
	}
	case Type_Map: {
		lbValue entries = lb_map_entries_ptr(p, expr);
		lbValue elem = lb_emit_struct_ep(p, entries, 0);
		elem = lb_emit_load(p, elem);

		lbValue entry = lb_emit_ptr_offset(p, elem, idx);
		idx = lb_emit_load(p, lb_emit_struct_ep(p, entry, 2));
		val = lb_emit_load(p, lb_emit_struct_ep(p, entry, 3));

		break;
	}
	case Type_Struct: {
		GB_ASSERT(is_type_soa_struct(expr_type));
		break;
	}

	default:
		GB_PANIC("Cannot do range_indexed of %s", type_to_string(expr_type));
		break;
	}

	if (val_)  *val_  = val;
	if (idx_)  *idx_  = idx;
	if (loop_) *loop_ = loop;
	if (done_) *done_ = done;
}


void lb_build_range_string(lbProcedure *p, lbValue expr, Type *val_type,
                            lbValue *val_, lbValue *idx_, lbBlock **loop_, lbBlock **done_) {
	lbModule *m = p->module;
	lbValue count = lb_const_int(m, t_int, 0);
	Type *expr_type = base_type(expr.type);
	switch (expr_type->kind) {
	case Type_Basic:
		count = lb_string_len(p, expr);
		break;
	default:
		GB_PANIC("Cannot do range_string of %s", type_to_string(expr_type));
		break;
	}

	lbValue val = {};
	lbValue idx = {};
	lbBlock *loop = nullptr;
	lbBlock *done = nullptr;
	lbBlock *body = nullptr;


	lbAddr offset_ = lb_add_local_generated(p, t_int, false);
	lb_addr_store(p, offset_, lb_const_int(m, t_int, 0));

	loop = lb_create_block(p, "for.string.loop");
	lb_emit_jump(p, loop);
	lb_start_block(p, loop);



	body = lb_create_block(p, "for.string.body");
	done = lb_create_block(p, "for.string.done");

	lbValue offset = lb_addr_load(p, offset_);
	lbValue cond = lb_emit_comp(p, Token_Lt, offset, count);
	lb_emit_if(p, cond, body, done);
	lb_start_block(p, body);


	lbValue str_elem = lb_emit_ptr_offset(p, lb_string_elem(p, expr), offset);
	lbValue str_len  = lb_emit_arith(p, Token_Sub, count, offset, t_int);
	auto args = array_make<lbValue>(permanent_allocator(), 1);
	args[0] = lb_emit_string(p, str_elem, str_len);
	lbValue rune_and_len = lb_emit_runtime_call(p, "string_decode_rune", args);
	lbValue len  = lb_emit_struct_ev(p, rune_and_len, 1);
	lb_addr_store(p, offset_, lb_emit_arith(p, Token_Add, offset, len, t_int));


	idx = offset;
	if (val_type != nullptr) {
		val = lb_emit_struct_ev(p, rune_and_len, 0);
	}

	if (val_)  *val_  = val;
	if (idx_)  *idx_  = idx;
	if (loop_) *loop_ = loop;
	if (done_) *done_ = done;
}


void lb_build_range_interval(lbProcedure *p, AstBinaryExpr *node,
                             AstRangeStmt *rs, Scope *scope) {
	bool ADD_EXTRA_WRAPPING_CHECK = true;

	lbModule *m = p->module;

	lb_open_scope(p, scope);

	Type *val0_type = nullptr;
	Type *val1_type = nullptr;
	if (rs->vals.count > 0 && rs->vals[0] != nullptr && !is_blank_ident(rs->vals[0])) {
		val0_type = type_of_expr(rs->vals[0]);
	}
	if (rs->vals.count > 1 && rs->vals[1] != nullptr && !is_blank_ident(rs->vals[1])) {
		val1_type = type_of_expr(rs->vals[1]);
	}

	if (val0_type != nullptr) {
		Entity *e = entity_of_node(rs->vals[0]);
		lb_add_local(p, e->type, e, true);
	}
	if (val1_type != nullptr) {
		Entity *e = entity_of_node(rs->vals[1]);
		lb_add_local(p, e->type, e, true);
	}

	TokenKind op = Token_Lt;
	switch (node->op.kind) {
	case Token_Ellipsis:  op = Token_LtEq; break;
	case Token_RangeFull: op = Token_LtEq; break;
	case Token_RangeHalf: op = Token_Lt;  break;
	default: GB_PANIC("Invalid interval operator"); break;
	}

	lbValue lower = lb_build_expr(p, node->left);
	lbValue upper = {}; // initialized each time in the loop

	lbAddr value = lb_add_local_generated(p, val0_type ? val0_type : lower.type, false);
	lb_addr_store(p, value, lower);

	lbAddr index = lb_add_local_generated(p, t_int, false);
	lb_addr_store(p, index, lb_const_int(m, t_int, 0));

	lbBlock *loop = lb_create_block(p, "for.interval.loop");
	lbBlock *body = lb_create_block(p, "for.interval.body");
	lbBlock *done = lb_create_block(p, "for.interval.done");

	lb_emit_jump(p, loop);
	lb_start_block(p, loop);

	upper = lb_build_expr(p, node->right);
	lbValue curr_value = lb_addr_load(p, value);
	lbValue cond = lb_emit_comp(p, op, curr_value, upper);
	lb_emit_if(p, cond, body, done);
	lb_start_block(p, body);

	lbValue val = lb_addr_load(p, value);
	lbValue idx = lb_addr_load(p, index);
	if (val0_type) lb_store_range_stmt_val(p, rs->vals[0], val);
	if (val1_type) lb_store_range_stmt_val(p, rs->vals[1], idx);

	{
		// NOTE: this check block will most likely be optimized out, and is here
		// to make this code easier to read
		lbBlock *check = nullptr;
		lbBlock *post = lb_create_block(p, "for.interval.post");

		lbBlock *continue_block = post;

		if (ADD_EXTRA_WRAPPING_CHECK &&
		    op == Token_LtEq) {
			check = lb_create_block(p, "for.interval.check");
			continue_block = check;
		}

		lb_push_target_list(p, rs->label, done, continue_block, nullptr);

		lb_build_stmt(p, rs->body);

		lb_close_scope(p, lbDeferExit_Default, nullptr);
		lb_pop_target_list(p);

		if (check != nullptr) {
			lb_emit_jump(p, check);
			lb_start_block(p, check);

			lbValue check_cond = lb_emit_comp(p, Token_NotEq, curr_value, upper);
			lb_emit_if(p, check_cond, post, done);
		} else {
			lb_emit_jump(p, post);
		}

		lb_start_block(p, post);
		lb_emit_increment(p, value.addr);
		lb_emit_increment(p, index.addr);
		lb_emit_jump(p, loop);
	}

	lb_start_block(p, done);
}

void lb_build_range_enum(lbProcedure *p, Type *enum_type, Type *val_type, lbValue *val_, lbValue *idx_, lbBlock **loop_, lbBlock **done_) {
	lbModule *m = p->module;

	Type *t = enum_type;
	GB_ASSERT(is_type_enum(t));
	Type *enum_ptr = alloc_type_pointer(t);
	t = base_type(t);
	Type *core_elem = core_type(t);
	GB_ASSERT(t->kind == Type_Enum);
	i64 enum_count = t->Enum.fields.count;
	lbValue max_count = lb_const_int(m, t_int, enum_count);

	lbValue ti          = lb_type_info(m, t);
	lbValue variant     = lb_emit_struct_ep(p, ti, 4);
	lbValue eti_ptr     = lb_emit_conv(p, variant, t_type_info_enum_ptr);
	lbValue values      = lb_emit_load(p, lb_emit_struct_ep(p, eti_ptr, 2));
	lbValue values_data = lb_slice_elem(p, values);

	lbAddr offset_ = lb_add_local_generated(p, t_int, false);
	lb_addr_store(p, offset_, lb_const_int(m, t_int, 0));

	lbBlock *loop = lb_create_block(p, "for.enum.loop");
	lb_emit_jump(p, loop);
	lb_start_block(p, loop);

	lbBlock *body = lb_create_block(p, "for.enum.body");
	lbBlock *done = lb_create_block(p, "for.enum.done");

	lbValue offset = lb_addr_load(p, offset_);
	lbValue cond = lb_emit_comp(p, Token_Lt, offset, max_count);
	lb_emit_if(p, cond, body, done);
	lb_start_block(p, body);

	lbValue val_ptr = lb_emit_ptr_offset(p, values_data, offset);
	lb_emit_increment(p, offset_.addr);

	lbValue val = {};
	if (val_type != nullptr) {
		GB_ASSERT(are_types_identical(enum_type, val_type));

		if (is_type_integer(core_elem)) {
			lbValue i = lb_emit_load(p, lb_emit_conv(p, val_ptr, t_i64_ptr));
			val = lb_emit_conv(p, i, t);
		} else {
			GB_PANIC("TODO(bill): enum core type %s", type_to_string(core_elem));
		}
	}

	if (val_)  *val_  = val;
	if (idx_)  *idx_  = offset;
	if (loop_) *loop_ = loop;
	if (done_) *done_ = done;
}

void lb_build_range_tuple(lbProcedure *p, Ast *expr, Type *val0_type, Type *val1_type,
                          lbValue *val0_, lbValue *val1_, lbBlock **loop_, lbBlock **done_) {
	lbBlock *loop = lb_create_block(p, "for.tuple.loop");
	lb_emit_jump(p, loop);
	lb_start_block(p, loop);

	lbBlock *body = lb_create_block(p, "for.tuple.body");
	lbBlock *done = lb_create_block(p, "for.tuple.done");

	lbValue tuple_value = lb_build_expr(p, expr);
	Type *tuple = tuple_value.type;
	GB_ASSERT(tuple->kind == Type_Tuple);
	i32 tuple_count = cast(i32)tuple->Tuple.variables.count;
	i32 cond_index = tuple_count-1;

	lbValue cond = lb_emit_struct_ev(p, tuple_value, cond_index);
	lb_emit_if(p, cond, body, done);
	lb_start_block(p, body);


	if (val0_) *val0_ = lb_emit_struct_ev(p, tuple_value, 0);
	if (val1_) *val1_ = lb_emit_struct_ev(p, tuple_value, 1);
	if (loop_) *loop_ = loop;
	if (done_) *done_ = done;
}

void lb_build_range_stmt_struct_soa(lbProcedure *p, AstRangeStmt *rs, Scope *scope) {
	Ast *expr = unparen_expr(rs->expr);
	TypeAndValue tav = type_and_value_of_expr(expr);

	lbBlock *loop = nullptr;
	lbBlock *body = nullptr;
	lbBlock *done = nullptr;

	lb_open_scope(p, scope);


	Type *val_types[2] = {};
	if (rs->vals.count > 0 && rs->vals[0] != nullptr && !is_blank_ident(rs->vals[0])) {
		val_types[0] = type_of_expr(rs->vals[0]);
	}
	if (rs->vals.count > 1 && rs->vals[1] != nullptr && !is_blank_ident(rs->vals[1])) {
		val_types[1] = type_of_expr(rs->vals[1]);
	}



	lbAddr array = lb_build_addr(p, expr);
	if (is_type_pointer(type_deref(lb_addr_type(array)))) {
		array = lb_addr(lb_addr_load(p, array));
	}
	lbValue count = lb_soa_struct_len(p, lb_addr_load(p, array));


	lbAddr index = lb_add_local_generated(p, t_int, false);
	lb_addr_store(p, index, lb_const_int(p->module, t_int, cast(u64)-1));

	loop = lb_create_block(p, "for.soa.loop");
	lb_emit_jump(p, loop);
	lb_start_block(p, loop);

	lbValue incr = lb_emit_arith(p, Token_Add, lb_addr_load(p, index), lb_const_int(p->module, t_int, 1), t_int);
	lb_addr_store(p, index, incr);

	body = lb_create_block(p, "for.soa.body");
	done = lb_create_block(p, "for.soa.done");

	lbValue cond = lb_emit_comp(p, Token_Lt, incr, count);
	lb_emit_if(p, cond, body, done);
	lb_start_block(p, body);


	if (val_types[0]) {
		Entity *e = entity_of_node(rs->vals[0]);
		if (e != nullptr) {
			lbAddr soa_val = lb_addr_soa_variable(array.addr, lb_addr_load(p, index), nullptr);
			map_set(&p->module->soa_values, hash_entity(e), soa_val);
		}
	}
	if (val_types[1]) {
		lb_store_range_stmt_val(p, rs->vals[1], lb_addr_load(p, index));
	}


	lb_push_target_list(p, rs->label, done, loop, nullptr);

	lb_build_stmt(p, rs->body);

	lb_close_scope(p, lbDeferExit_Default, nullptr);
	lb_pop_target_list(p);
	lb_emit_jump(p, loop);
	lb_start_block(p, done);

}

void lb_build_range_stmt(lbProcedure *p, AstRangeStmt *rs, Scope *scope) {
	Ast *expr = unparen_expr(rs->expr);

	if (is_ast_range(expr)) {
		lb_build_range_interval(p, &expr->BinaryExpr, rs, scope);
		return;
	}

	Type *expr_type = type_of_expr(expr);
	if (expr_type != nullptr) {
		Type *et = base_type(type_deref(expr_type));
	 	if (is_type_soa_struct(et)) {
			lb_build_range_stmt_struct_soa(p, rs, scope);
			return;
		}
	}

	lb_open_scope(p, scope);

	Type *val0_type = nullptr;
	Type *val1_type = nullptr;
	if (rs->vals.count > 0 && rs->vals[0] != nullptr && !is_blank_ident(rs->vals[0])) {
		val0_type = type_of_expr(rs->vals[0]);
	}
	if (rs->vals.count > 1 && rs->vals[1] != nullptr && !is_blank_ident(rs->vals[1])) {
		val1_type = type_of_expr(rs->vals[1]);
	}

	if (val0_type != nullptr) {
		Entity *e = entity_of_node(rs->vals[0]);
		lb_add_local(p, e->type, e, true);
	}
	if (val1_type != nullptr) {
		Entity *e = entity_of_node(rs->vals[1]);
		lb_add_local(p, e->type, e, true);
	}

	lbValue val = {};
	lbValue key = {};
	lbBlock *loop = nullptr;
	lbBlock *done = nullptr;
	bool is_map = false;
	TypeAndValue tav = type_and_value_of_expr(expr);

	if (tav.mode == Addressing_Type) {
		lb_build_range_enum(p, type_deref(tav.type), val0_type, &val, &key, &loop, &done);
	} else {
		Type *expr_type = type_of_expr(expr);
		Type *et = base_type(type_deref(expr_type));
		switch (et->kind) {
		case Type_Map: {
			is_map = true;
			lbValue map = lb_build_addr_ptr(p, expr);
			if (is_type_pointer(type_deref(map.type))) {
				map = lb_emit_load(p, map);
			}
			lbValue entries_ptr = lb_map_entries_ptr(p, map);
			lbValue count_ptr = lb_emit_struct_ep(p, entries_ptr, 1);
			lb_build_range_indexed(p, map, val1_type, count_ptr, &val, &key, &loop, &done);
			break;
		}
		case Type_Array: {
			lbValue array = lb_build_addr_ptr(p, expr);
			if (is_type_pointer(type_deref(array.type))) {
				array = lb_emit_load(p, array);
			}
			lbAddr count_ptr = lb_add_local_generated(p, t_int, false);
			lb_addr_store(p, count_ptr, lb_const_int(p->module, t_int, et->Array.count));
			lb_build_range_indexed(p, array, val0_type, count_ptr.addr, &val, &key, &loop, &done);
			break;
		}
		case Type_EnumeratedArray: {
			lbValue array = lb_build_addr_ptr(p, expr);
			if (is_type_pointer(type_deref(array.type))) {
				array = lb_emit_load(p, array);
			}
			lbAddr count_ptr = lb_add_local_generated(p, t_int, false);
			lb_addr_store(p, count_ptr, lb_const_int(p->module, t_int, et->EnumeratedArray.count));
			lb_build_range_indexed(p, array, val0_type, count_ptr.addr, &val, &key, &loop, &done);
			break;
		}
		case Type_DynamicArray: {
			lbValue count_ptr = {};
			lbValue array = lb_build_addr_ptr(p, expr);
			if (is_type_pointer(type_deref(array.type))) {
				array = lb_emit_load(p, array);
			}
			count_ptr = lb_emit_struct_ep(p, array, 1);
			lb_build_range_indexed(p, array, val0_type, count_ptr, &val, &key, &loop, &done);
			break;
		}
		case Type_Slice: {
			lbValue count_ptr = {};
			lbValue slice = lb_build_expr(p, expr);
			if (is_type_pointer(slice.type)) {
				count_ptr = lb_emit_struct_ep(p, slice, 1);
				slice = lb_emit_load(p, slice);
			} else {
				count_ptr = lb_add_local_generated(p, t_int, false).addr;
				lb_emit_store(p, count_ptr, lb_slice_len(p, slice));
			}
			lb_build_range_indexed(p, slice, val0_type, count_ptr, &val, &key, &loop, &done);
			break;
		}
		case Type_Basic: {
			lbValue string = lb_build_expr(p, expr);
			if (is_type_pointer(string.type)) {
				string = lb_emit_load(p, string);
			}
			if (is_type_untyped(expr_type)) {
				lbAddr s = lb_add_local_generated(p, default_type(string.type), false);
				lb_addr_store(p, s, string);
				string = lb_addr_load(p, s);
			}
			Type *t = base_type(string.type);
			GB_ASSERT(!is_type_cstring(t));
			lb_build_range_string(p, string, val0_type, &val, &key, &loop, &done);
			break;
		}
		case Type_Tuple:
			lb_build_range_tuple(p, expr, val0_type, val1_type, &val, &key, &loop, &done);
			break;
		default:
			GB_PANIC("Cannot range over %s", type_to_string(expr_type));
			break;
		}
	}


	if (is_map) {
		if (val0_type) lb_store_range_stmt_val(p, rs->vals[0], key);
		if (val1_type) lb_store_range_stmt_val(p, rs->vals[1], val);
	} else {
		if (val0_type) lb_store_range_stmt_val(p, rs->vals[0], val);
		if (val1_type) lb_store_range_stmt_val(p, rs->vals[1], key);
	}

	lb_push_target_list(p, rs->label, done, loop, nullptr);

	lb_build_stmt(p, rs->body);

	lb_close_scope(p, lbDeferExit_Default, nullptr);
	lb_pop_target_list(p);
	lb_emit_jump(p, loop);
	lb_start_block(p, done);
}

void lb_build_unroll_range_stmt(lbProcedure *p, AstUnrollRangeStmt *rs, Scope *scope) {
	lbModule *m = p->module;

	lb_open_scope(p, scope); // Open scope here

	Type *val0_type = nullptr;
	Type *val1_type = nullptr;
	if (rs->val0 != nullptr && !is_blank_ident(rs->val0)) {
		val0_type = type_of_expr(rs->val0);
	}
	if (rs->val1 != nullptr && !is_blank_ident(rs->val1)) {
		val1_type = type_of_expr(rs->val1);
	}

	if (val0_type != nullptr) {
		Entity *e = entity_of_node(rs->val0);
		lb_add_local(p, e->type, e, true);
	}
	if (val1_type != nullptr) {
		Entity *e = entity_of_node(rs->val1);
		lb_add_local(p, e->type, e, true);
	}

	lbValue val = {};
	lbValue key = {};
	lbBlock *loop = nullptr;
	lbBlock *done = nullptr;
	Ast *expr = unparen_expr(rs->expr);

	TypeAndValue tav = type_and_value_of_expr(expr);

	if (is_ast_range(expr)) {

		lbAddr val0_addr = {};
		lbAddr val1_addr = {};
		if (val0_type) val0_addr = lb_build_addr(p, rs->val0);
		if (val1_type) val1_addr = lb_build_addr(p, rs->val1);

		TokenKind op = expr->BinaryExpr.op.kind;
		Ast *start_expr = expr->BinaryExpr.left;
		Ast *end_expr   = expr->BinaryExpr.right;
		GB_ASSERT(start_expr->tav.mode == Addressing_Constant);
		GB_ASSERT(end_expr->tav.mode == Addressing_Constant);

		ExactValue start = start_expr->tav.value;
		ExactValue end   = end_expr->tav.value;
		if (op != Token_RangeHalf) { // .. [start, end] (or ..=)
			ExactValue index = exact_value_i64(0);
			for (ExactValue val = start;
			     compare_exact_values(Token_LtEq, val, end);
			     val = exact_value_increment_one(val), index = exact_value_increment_one(index)) {

				if (val0_type) lb_addr_store(p, val0_addr, lb_const_value(m, val0_type, val));
				if (val1_type) lb_addr_store(p, val1_addr, lb_const_value(m, val1_type, index));

				lb_build_stmt(p, rs->body);
			}
		} else { // ..< [start, end)
			ExactValue index = exact_value_i64(0);
			for (ExactValue val = start;
			     compare_exact_values(Token_Lt, val, end);
			     val = exact_value_increment_one(val), index = exact_value_increment_one(index)) {

				if (val0_type) lb_addr_store(p, val0_addr, lb_const_value(m, val0_type, val));
				if (val1_type) lb_addr_store(p, val1_addr, lb_const_value(m, val1_type, index));

				lb_build_stmt(p, rs->body);
			}
		}


	} else if (tav.mode == Addressing_Type) {
		GB_ASSERT(is_type_enum(type_deref(tav.type)));
		Type *et = type_deref(tav.type);
		Type *bet = base_type(et);

		lbAddr val0_addr = {};
		lbAddr val1_addr = {};
		if (val0_type) val0_addr = lb_build_addr(p, rs->val0);
		if (val1_type) val1_addr = lb_build_addr(p, rs->val1);

		for_array(i, bet->Enum.fields) {
			Entity *field = bet->Enum.fields[i];
			GB_ASSERT(field->kind == Entity_Constant);
			if (val0_type) lb_addr_store(p, val0_addr, lb_const_value(m, val0_type, field->Constant.value));
			if (val1_type) lb_addr_store(p, val1_addr, lb_const_value(m, val1_type, exact_value_i64(i)));

			lb_build_stmt(p, rs->body);
		}
	} else {
		lbAddr val0_addr = {};
		lbAddr val1_addr = {};
		if (val0_type) val0_addr = lb_build_addr(p, rs->val0);
		if (val1_type) val1_addr = lb_build_addr(p, rs->val1);

		GB_ASSERT(expr->tav.mode == Addressing_Constant);

		Type *t = base_type(expr->tav.type);


		switch (t->kind) {
		case Type_Basic:
			GB_ASSERT(is_type_string(t));
			{
				ExactValue value = expr->tav.value;
				GB_ASSERT(value.kind == ExactValue_String);
				String str = value.value_string;
				Rune codepoint = 0;
				isize offset = 0;
				do {
					isize width = gb_utf8_decode(str.text+offset, str.len-offset, &codepoint);
					if (val0_type) lb_addr_store(p, val0_addr, lb_const_value(m, val0_type, exact_value_i64(codepoint)));
					if (val1_type) lb_addr_store(p, val1_addr, lb_const_value(m, val1_type, exact_value_i64(offset)));
					lb_build_stmt(p, rs->body);

					offset += width;
				} while (offset < str.len);
			}
			break;
		case Type_Array:
			if (t->Array.count > 0) {
				lbValue val = lb_build_expr(p, expr);
				lbValue val_addr = lb_address_from_load_or_generate_local(p, val);

				for (i64 i = 0; i < t->Array.count; i++) {
					if (val0_type) {
						// NOTE(bill): Due to weird legacy issues in LLVM, this needs to be an i32
						lbValue elem = lb_emit_array_epi(p, val_addr, cast(i32)i);
						lb_addr_store(p, val0_addr, lb_emit_load(p, elem));
					}
					if (val1_type) lb_addr_store(p, val1_addr, lb_const_value(m, val1_type, exact_value_i64(i)));

					lb_build_stmt(p, rs->body);
				}

			}
			break;
		case Type_EnumeratedArray:
			if (t->EnumeratedArray.count > 0) {
				lbValue val = lb_build_expr(p, expr);
				lbValue val_addr = lb_address_from_load_or_generate_local(p, val);

				for (i64 i = 0; i < t->EnumeratedArray.count; i++) {
					if (val0_type) {
						// NOTE(bill): Due to weird legacy issues in LLVM, this needs to be an i32
						lbValue elem = lb_emit_array_epi(p, val_addr, cast(i32)i);
						lb_addr_store(p, val0_addr, lb_emit_load(p, elem));
					}
					if (val1_type) {
						ExactValue idx = exact_value_add(exact_value_i64(i), t->EnumeratedArray.min_value);
						lb_addr_store(p, val1_addr, lb_const_value(m, val1_type, idx));
					}

					lb_build_stmt(p, rs->body);
				}

			}
			break;
		default:
			GB_PANIC("Invalid '#unroll for' type");
			break;
		}
	}


	lb_close_scope(p, lbDeferExit_Default, nullptr);
}

bool lb_switch_stmt_can_be_trivial_jump_table(AstSwitchStmt *ss, bool *default_found_) {
	if (ss->tag == nullptr) {
		return false;
	}
	bool is_typeid = false;
	TypeAndValue tv = type_and_value_of_expr(ss->tag);
	if (is_type_integer(core_type(tv.type))) {
		// okay
	} else if (is_type_typeid(tv.type)) {
		// okay
		is_typeid = true;
	} else {
		return false;
	}

	ast_node(body, BlockStmt, ss->body);
	for_array(i, body->stmts) {
		Ast *clause = body->stmts[i];
		ast_node(cc, CaseClause, clause);

		if (cc->list.count == 0) {
			if (default_found_) *default_found_ = true;
			continue;
		}

		for_array(j, cc->list) {
			Ast *expr = unparen_expr(cc->list[j]);
			if (is_ast_range(expr)) {
				return false;
			}
			if (expr->tav.mode == Addressing_Type) {
				GB_ASSERT(is_typeid);
				continue;
			}
			tv = type_and_value_of_expr(expr);
			if (tv.mode != Addressing_Constant) {
				return false;
			}
			if (!is_type_integer(core_type(tv.type))) {
				return false;
			}
		}

	}

	return true;
}


void lb_build_switch_stmt(lbProcedure *p, AstSwitchStmt *ss, Scope *scope) {
	lb_open_scope(p, scope);

	if (ss->init != nullptr) {
		lb_build_stmt(p, ss->init);
	}
	lbValue tag = lb_const_bool(p->module, t_llvm_bool, true);
	if (ss->tag != nullptr) {
		tag = lb_build_expr(p, ss->tag);
	}
	lbBlock *done = lb_create_block(p, "switch.done"); // NOTE(bill): Append later

	ast_node(body, BlockStmt, ss->body);

	isize case_count = body->stmts.count;
	Slice<Ast *> default_stmts = {};
	lbBlock *default_fall = nullptr;
	lbBlock *default_block = nullptr;
	lbBlock *fall = nullptr;

	bool default_found = false;
	bool is_trivial = lb_switch_stmt_can_be_trivial_jump_table(ss, &default_found);

	auto body_blocks = slice_make<lbBlock *>(permanent_allocator(), body->stmts.count);
	for_array(i, body->stmts) {
		Ast *clause = body->stmts[i];
		ast_node(cc, CaseClause, clause);

		body_blocks[i] = lb_create_block(p, cc->list.count == 0 ? "switch.default.body" : "switch.case.body");
		if (cc->list.count == 0) {
			default_block = body_blocks[i];
		}
	}


	LLVMValueRef switch_instr = nullptr;
	if (is_trivial) {
		isize num_cases = 0;
		for_array(i, body->stmts) {
			Ast *clause = body->stmts[i];
			ast_node(cc, CaseClause, clause);
			num_cases += cc->list.count;
		}

		LLVMBasicBlockRef end_block = done->block;
		if (default_block) {
			end_block = default_block->block;
		}

		switch_instr = LLVMBuildSwitch(p->builder, tag.value, end_block, cast(unsigned)num_cases);
	}


	for_array(i, body->stmts) {
		Ast *clause = body->stmts[i];
		ast_node(cc, CaseClause, clause);

		lbBlock *body = body_blocks[i];
		fall = done;
		if (i+1 < case_count) {
			fall = body_blocks[i+1];
		}

		if (cc->list.count == 0) {
			// default case
			default_stmts = cc->stmts;
			default_fall  = fall;
			if (switch_instr == nullptr) {
				default_block = body;
			} else {
				GB_ASSERT(default_block != nullptr);
			}
			continue;
		}

		lbBlock *next_cond = nullptr;
		for_array(j, cc->list) {
			Ast *expr = unparen_expr(cc->list[j]);

			if (switch_instr != nullptr) {
				lbValue on_val = {};
				if (expr->tav.mode == Addressing_Type) {
					GB_ASSERT(is_type_typeid(tag.type));
					lbValue e = lb_typeid(p->module, expr->tav.type);
					on_val = lb_emit_conv(p, e, tag.type);
				} else {
					GB_ASSERT(expr->tav.mode == Addressing_Constant);
					GB_ASSERT(!is_ast_range(expr));

					on_val = lb_build_expr(p, expr);
				}

				GB_ASSERT(LLVMIsConstant(on_val.value));
				LLVMAddCase(switch_instr, on_val.value, body->block);
				continue;
			}

			next_cond = lb_create_block(p, "switch.case.next");

			lbValue cond = {};
			if (is_ast_range(expr)) {
				ast_node(ie, BinaryExpr, expr);
				TokenKind op = Token_Invalid;
				switch (ie->op.kind) {
				case Token_Ellipsis:  op = Token_LtEq; break;
				case Token_RangeFull: op = Token_LtEq; break;
				case Token_RangeHalf: op = Token_Lt;   break;
				default: GB_PANIC("Invalid interval operator"); break;
				}
				lbValue lhs = lb_build_expr(p, ie->left);
				lbValue rhs = lb_build_expr(p, ie->right);

				lbValue cond_lhs = lb_emit_comp(p, Token_LtEq, lhs, tag);
				lbValue cond_rhs = lb_emit_comp(p, op, tag, rhs);
				cond = lb_emit_arith(p, Token_And, cond_lhs, cond_rhs, t_bool);
			} else {
				if (expr->tav.mode == Addressing_Type) {
					GB_ASSERT(is_type_typeid(tag.type));
					lbValue e = lb_typeid(p->module, expr->tav.type);
					e = lb_emit_conv(p, e, tag.type);
					cond = lb_emit_comp(p, Token_CmpEq, tag, e);
				} else {
					cond = lb_emit_comp(p, Token_CmpEq, tag, lb_build_expr(p, expr));
				}
			}

			lb_emit_if(p, cond, body, next_cond);
			lb_start_block(p, next_cond);
		}
		lb_start_block(p, body);

		lb_push_target_list(p, ss->label, done, nullptr, fall);
		lb_open_scope(p, body->scope);
		lb_build_stmt_list(p, cc->stmts);
		lb_close_scope(p, lbDeferExit_Default, body);
		lb_pop_target_list(p);

		lb_emit_jump(p, done);
		if (switch_instr == nullptr) {
			lb_start_block(p, next_cond);
		}
	}

	if (default_block != nullptr) {
		if (switch_instr == nullptr) {
			lb_emit_jump(p, default_block);
		}
		lb_start_block(p, default_block);

		lb_push_target_list(p, ss->label, done, nullptr, default_fall);
		lb_open_scope(p, default_block->scope);
		lb_build_stmt_list(p, default_stmts);
		lb_close_scope(p, lbDeferExit_Default, default_block);
		lb_pop_target_list(p);
	}

	lb_emit_jump(p, done);
	lb_close_scope(p, lbDeferExit_Default, done);
	lb_start_block(p, done);
}

void lb_store_type_case_implicit(lbProcedure *p, Ast *clause, lbValue value) {
	Entity *e = implicit_entity_of_node(clause);
	GB_ASSERT(e != nullptr);
	if (e->flags & EntityFlag_Value) {
		// by value
		GB_ASSERT(are_types_identical(e->type, value.type));
		lbAddr x = lb_add_local(p, e->type, e, false);
		lb_addr_store(p, x, value);
	} else {
		// by reference
		GB_ASSERT(are_types_identical(e->type, type_deref(value.type)));
		lb_add_entity(p->module, e, value);
	}
}

lbAddr lb_store_range_stmt_val(lbProcedure *p, Ast *stmt_val, lbValue value) {
	Entity *e = entity_of_node(stmt_val);
	if (e == nullptr) {
		return {};
	}

	if ((e->flags & EntityFlag_Value) == 0) {
		if (LLVMIsALoadInst(value.value)) {
			lbValue ptr = lb_address_from_load_or_generate_local(p, value);
			lb_add_entity(p->module, e, ptr);
			return lb_addr(ptr);
		}
	}

	// by value
	lbAddr addr = lb_add_local(p, e->type, e, false);
	lb_addr_store(p, addr, value);
	return addr;
}

void lb_type_case_body(lbProcedure *p, Ast *label, Ast *clause, lbBlock *body, lbBlock *done) {
	ast_node(cc, CaseClause, clause);

	lb_push_target_list(p, label, done, nullptr, nullptr);
	lb_open_scope(p, body->scope);
	lb_build_stmt_list(p, cc->stmts);
	lb_close_scope(p, lbDeferExit_Default, body);
	lb_pop_target_list(p);

	lb_emit_jump(p, done);
}



void lb_build_type_switch_stmt(lbProcedure *p, AstTypeSwitchStmt *ss) {
	lbModule *m = p->module;

	ast_node(as, AssignStmt, ss->tag);
	GB_ASSERT(as->lhs.count == 1);
	GB_ASSERT(as->rhs.count == 1);

	lbValue parent = lb_build_expr(p, as->rhs[0]);
	bool is_parent_ptr = is_type_pointer(parent.type);

	TypeSwitchKind switch_kind = check_valid_type_switch_type(parent.type);
	GB_ASSERT(switch_kind != TypeSwitch_Invalid);

	lbValue parent_value = parent;

	lbValue parent_ptr = parent;
	if (!is_parent_ptr) {
		parent_ptr = lb_address_from_load_or_generate_local(p, parent);
	}

	lbValue tag = {};
	lbValue union_data = {};
	if (switch_kind == TypeSwitch_Union) {
		union_data = lb_emit_conv(p, parent_ptr, t_rawptr);
		if (is_type_union_maybe_pointer(type_deref(parent_ptr.type))) {
			tag = lb_emit_conv(p, lb_emit_comp_against_nil(p, Token_NotEq, union_data), t_int);
		} else {
			lbValue tag_ptr = lb_emit_union_tag_ptr(p, parent_ptr);
			tag = lb_emit_load(p, tag_ptr);
		}
	} else if (switch_kind == TypeSwitch_Any) {
		tag = lb_emit_load(p, lb_emit_struct_ep(p, parent_ptr, 1));
	} else {
		GB_PANIC("Unknown switch kind");
	}

	ast_node(body, BlockStmt, ss->body);

	lbBlock *done = lb_create_block(p, "typeswitch.done");
	lbBlock *else_block = done;
	lbBlock *default_block = nullptr;
	isize num_cases = 0;

	for_array(i, body->stmts) {
		Ast *clause = body->stmts[i];
		ast_node(cc, CaseClause, clause);
		num_cases += cc->list.count;
		if (cc->list.count == 0) {
			GB_ASSERT(default_block == nullptr);
			default_block = lb_create_block(p, "typeswitch.default.body");
			else_block = default_block;
		}
	}

	GB_ASSERT(tag.value != nullptr);
	LLVMValueRef switch_instr = LLVMBuildSwitch(p->builder, tag.value, else_block->block, cast(unsigned)num_cases);

	for_array(i, body->stmts) {
		Ast *clause = body->stmts[i];
		ast_node(cc, CaseClause, clause);
		if (cc->list.count == 0) {
			lb_start_block(p, default_block);
			lb_store_type_case_implicit(p, clause, parent_value);
			lb_type_case_body(p, ss->label, clause, p->curr_block, done);
			continue;
		}

		lbBlock *body = lb_create_block(p, "typeswitch.body");
		Type *case_type = nullptr;
		for_array(type_index, cc->list) {
			case_type = type_of_expr(cc->list[type_index]);
			lbValue on_val = {};
			if (switch_kind == TypeSwitch_Union) {
				Type *ut = base_type(type_deref(parent.type));
				on_val = lb_const_union_tag(m, ut, case_type);

			} else if (switch_kind == TypeSwitch_Any) {
				on_val = lb_typeid(m, case_type);
			}
			GB_ASSERT(on_val.value != nullptr);
			LLVMAddCase(switch_instr, on_val.value, body->block);
		}

		Entity *case_entity = implicit_entity_of_node(clause);

		lbValue value = parent_value;

		lb_start_block(p, body);

		bool by_reference = (case_entity->flags & EntityFlag_Value) == 0;

		if (cc->list.count == 1) {
			lbValue data = {};
			if (switch_kind == TypeSwitch_Union) {
				data = union_data;
			} else if (switch_kind == TypeSwitch_Any) {
				data = lb_emit_load(p, lb_emit_struct_ep(p, parent_ptr, 0));
			}

			Type *ct = case_entity->type;
			Type *ct_ptr = alloc_type_pointer(ct);

			value = lb_emit_conv(p, data, ct_ptr);
			if (!by_reference) {
				value = lb_emit_load(p, value);
			}
		}

		lb_store_type_case_implicit(p, clause, value);
		lb_type_case_body(p, ss->label, clause, body, done);
	}

	lb_emit_jump(p, done);
	lb_start_block(p, done);
}


lbValue lb_emit_logical_binary_expr(lbProcedure *p, TokenKind op, Ast *left, Ast *right, Type *type) {
	lbModule *m = p->module;

	lbBlock *rhs  = lb_create_block(p, "logical.cmp.rhs");
	lbBlock *done = lb_create_block(p, "logical.cmp.done");

	type = default_type(type);

	lbValue short_circuit = {};
	if (op == Token_CmpAnd) {
		lb_build_cond(p, left, rhs, done);
		short_circuit = lb_const_bool(m, type, false);
	} else if (op == Token_CmpOr) {
		lb_build_cond(p, left, done, rhs);
		short_circuit = lb_const_bool(m, type, true);
	}

	if (rhs->preds.count == 0) {
		lb_start_block(p, done);
		return short_circuit;
	}

	if (done->preds.count == 0) {
		lb_start_block(p, rhs);
		if (lb_is_expr_untyped_const(right)) {
			return lb_expr_untyped_const_to_typed(m, right, type);
		}
		return lb_build_expr(p, right);
	}

	Array<LLVMValueRef> incoming_values = {};
	Array<LLVMBasicBlockRef> incoming_blocks = {};
	array_init(&incoming_values, heap_allocator(), done->preds.count+1);
	array_init(&incoming_blocks, heap_allocator(), done->preds.count+1);

	for_array(i, done->preds) {
		incoming_values[i] = short_circuit.value;
		incoming_blocks[i] = done->preds[i]->block;
	}

	lb_start_block(p, rhs);
	lbValue edge = {};
	if (lb_is_expr_untyped_const(right)) {
		edge = lb_expr_untyped_const_to_typed(m, right, type);
	} else {
		edge = lb_build_expr(p, right);
	}

	incoming_values[done->preds.count] = edge.value;
	incoming_blocks[done->preds.count] = p->curr_block->block;

	lb_emit_jump(p, done);
	lb_start_block(p, done);

	lbValue res = {};
	res.type = type;
	res.value = LLVMBuildPhi(p->builder, lb_type(m, type), "");
	GB_ASSERT(incoming_values.count == incoming_blocks.count);
	LLVMAddIncoming(res.value, incoming_values.data, incoming_blocks.data, cast(unsigned)incoming_values.count);

	return res;
}

lbCopyElisionHint lb_set_copy_elision_hint(lbProcedure *p, lbAddr const &addr, Ast *ast) {
	lbCopyElisionHint prev = p->copy_elision_hint;
	p->copy_elision_hint.used = false;
	p->copy_elision_hint.ptr = {};
	p->copy_elision_hint.ast = nullptr;
#if 0
	if (addr.kind == lbAddr_Default && addr.addr.value != nullptr) {
		p->copy_elision_hint.ptr = lb_addr_get_ptr(p, addr);
		p->copy_elision_hint.ast = unparen_expr(ast);
	}
#endif
	return prev;
}

void lb_reset_copy_elision_hint(lbProcedure *p, lbCopyElisionHint prev_hint) {
	p->copy_elision_hint = prev_hint;
}

lbValue lb_consume_copy_elision_hint(lbProcedure *p) {
	lbValue return_ptr = p->copy_elision_hint.ptr;
	p->copy_elision_hint.used = true;
	p->copy_elision_hint.ptr = {};
	p->copy_elision_hint.ast = nullptr;
	return return_ptr;
}

void lb_build_static_variables(lbProcedure *p, AstValueDecl *vd) {
	for_array(i, vd->names) {
		lbValue value = {};
		if (vd->values.count > 0) {
			GB_ASSERT(vd->names.count == vd->values.count);
			Ast *ast_value = vd->values[i];
			GB_ASSERT(ast_value->tav.mode == Addressing_Constant ||
			          ast_value->tav.mode == Addressing_Invalid);

			bool allow_local = false;
			value = lb_const_value(p->module, ast_value->tav.type, ast_value->tav.value, allow_local);
		}

		Ast *ident = vd->names[i];
		GB_ASSERT(!is_blank_ident(ident));
		Entity *e = entity_of_node(ident);
		GB_ASSERT(e->flags & EntityFlag_Static);
		String name = e->token.string;

		String mangled_name = {};
		{
			gbString str = gb_string_make_length(permanent_allocator(), p->name.text, p->name.len);
			str = gb_string_appendc(str, "-");
			str = gb_string_append_fmt(str, ".%.*s-%llu", LIT(name), cast(long long)e->id);
			mangled_name.text = cast(u8 *)str;
			mangled_name.len = gb_string_length(str);
		}

		char *c_name = alloc_cstring(permanent_allocator(), mangled_name);

		LLVMValueRef global = LLVMAddGlobal(p->module->mod, lb_type(p->module, e->type), c_name);
		LLVMSetInitializer(global, LLVMConstNull(lb_type(p->module, e->type)));
		if (value.value != nullptr) {
			LLVMSetInitializer(global, value.value);
		} else {
		}
		if (e->Variable.thread_local_model != "") {
			LLVMSetThreadLocal(global, true);

			String m = e->Variable.thread_local_model;
			LLVMThreadLocalMode mode = LLVMGeneralDynamicTLSModel;
			if (m == "default") {
				mode = LLVMGeneralDynamicTLSModel;
			} else if (m == "localdynamic") {
				mode = LLVMLocalDynamicTLSModel;
			} else if (m == "initialexec") {
				mode = LLVMInitialExecTLSModel;
			} else if (m == "localexec") {
				mode = LLVMLocalExecTLSModel;
			} else {
				GB_PANIC("Unhandled thread local mode %.*s", LIT(m));
			}
			LLVMSetThreadLocalMode(global, mode);
		} else {
			LLVMSetLinkage(global, LLVMInternalLinkage);
		}


		lbValue global_val = {global, alloc_type_pointer(e->type)};
		lb_add_entity(p->module, e, global_val);
		lb_add_member(p->module, mangled_name, global_val);
	}
}


void lb_build_assignment(lbProcedure *p, Array<lbAddr> &lvals, Slice<Ast *> const &values) {
	if (values.count == 0) {
		return;
	}

	auto inits = array_make<lbValue>(permanent_allocator(), 0, lvals.count);

	for_array(i, values) {
		Ast *rhs = values[i];
		if (is_type_tuple(type_of_expr(rhs))) {
			lbValue init = lb_build_expr(p, rhs);
			Type *t = init.type;
			GB_ASSERT(t->kind == Type_Tuple);
			for_array(i, t->Tuple.variables) {
				Entity *e = t->Tuple.variables[i];
				lbValue v = lb_emit_struct_ev(p, init, cast(i32)i);
				array_add(&inits, v);
			}
		} else {
			auto prev_hint = lb_set_copy_elision_hint(p, lvals[inits.count], rhs);
			lbValue init = lb_build_expr(p, rhs);
			if (p->copy_elision_hint.used) {
				lvals[inits.count] = {}; // zero lval
			}
			lb_reset_copy_elision_hint(p, prev_hint);
			array_add(&inits, init);
		}
	}

	GB_ASSERT(lvals.count == inits.count);
	for_array(i, inits) {
		lbAddr lval = lvals[i];
		lbValue init = inits[i];
		lb_addr_store(p, lval, init);
	}
}

void lb_build_return_stmt_internal(lbProcedure *p, lbValue const &res) {
	lbFunctionType *ft = lb_get_function_type(p->module, p, p->type);
	bool return_by_pointer = ft->ret.kind == lbArg_Indirect;

	if (return_by_pointer) {
		if (res.value != nullptr) {
			LLVMBuildStore(p->builder, res.value, p->return_ptr.addr.value);
		} else {
			LLVMBuildStore(p->builder, LLVMConstNull(p->abi_function_type->ret.type), p->return_ptr.addr.value);
		}

		lb_emit_defer_stmts(p, lbDeferExit_Return, nullptr);

		LLVMBuildRetVoid(p->builder);
	} else {
		LLVMValueRef ret_val = res.value;
		ret_val = OdinLLVMBuildTransmute(p, ret_val, p->abi_function_type->ret.type);
		if (p->abi_function_type->ret.cast_type != nullptr) {
			ret_val = OdinLLVMBuildTransmute(p, ret_val, p->abi_function_type->ret.cast_type);
		}

		lb_emit_defer_stmts(p, lbDeferExit_Return, nullptr);
		LLVMBuildRet(p->builder, ret_val);
	}
}
void lb_build_return_stmt(lbProcedure *p, Slice<Ast *> const &return_results) {
	lb_ensure_abi_function_type(p->module, p);

	lbValue res = {};

	TypeTuple *tuple  = &p->type->Proc.results->Tuple;
	isize return_count = p->type->Proc.result_count;
	isize res_count = return_results.count;

	lbFunctionType *ft = lb_get_function_type(p->module, p, p->type);
	bool return_by_pointer = ft->ret.kind == lbArg_Indirect;

	if (return_count == 0) {
		// No return values

		lb_emit_defer_stmts(p, lbDeferExit_Return, nullptr);

		LLVMBuildRetVoid(p->builder);
		return;
	} else if (return_count == 1) {
		Entity *e = tuple->variables[0];
		if (res_count == 0) {
			lbValue found = map_must_get(&p->module->values, hash_entity(e));
			res = lb_emit_load(p, found);
		} else {
			res = lb_build_expr(p, return_results[0]);
			res = lb_emit_conv(p, res, e->type);
		}
		if (p->type->Proc.has_named_results) {
			// NOTE(bill): store the named values before returning
			if (e->token.string != "") {
				lbValue found = map_must_get(&p->module->values, hash_entity(e));
				lb_emit_store(p, found, lb_emit_conv(p, res, e->type));
			}
		}

	} else {
		auto results = array_make<lbValue>(permanent_allocator(), 0, return_count);

		if (res_count != 0) {
			for (isize res_index = 0; res_index < res_count; res_index++) {
				lbValue res = lb_build_expr(p, return_results[res_index]);
				Type *t = res.type;
				if (t->kind == Type_Tuple) {
					for_array(i, t->Tuple.variables) {
						Entity *e = t->Tuple.variables[i];
						lbValue v = lb_emit_struct_ev(p, res, cast(i32)i);
						array_add(&results, v);
					}
				} else {
					array_add(&results, res);
				}
			}
		} else {
			for (isize res_index = 0; res_index < return_count; res_index++) {
				Entity *e = tuple->variables[res_index];
				lbValue found = map_must_get(&p->module->values, hash_entity(e));
				lbValue res = lb_emit_load(p, found);
				array_add(&results, res);
			}
		}

		GB_ASSERT(results.count == return_count);

		if (p->type->Proc.has_named_results) {
			auto named_results = slice_make<lbValue>(temporary_allocator(), results.count);
			auto values = slice_make<lbValue>(temporary_allocator(), results.count);

			// NOTE(bill): store the named values before returning
			for_array(i, p->type->Proc.results->Tuple.variables) {
				Entity *e = p->type->Proc.results->Tuple.variables[i];
				if (e->kind != Entity_Variable) {
					continue;
				}

				if (e->token.string == "") {
					continue;
				}
				named_results[i] = map_must_get(&p->module->values, hash_entity(e));
				values[i] = lb_emit_conv(p, results[i], e->type);
			}

			for_array(i, named_results) {
				lb_emit_store(p, named_results[i], values[i]);
			}
		}

		Type *ret_type = p->type->Proc.results;

		// NOTE(bill): Doesn't need to be zero because it will be initialized in the loops
		if (return_by_pointer) {
			res = p->return_ptr.addr;
		} else {
			res = lb_add_local_generated(p, ret_type, false).addr;
		}

		auto result_values = slice_make<lbValue>(temporary_allocator(), results.count);
		auto result_eps = slice_make<lbValue>(temporary_allocator(), results.count);

		for_array(i, results) {
			result_values[i] = lb_emit_conv(p, results[i], tuple->variables[i]->type);
		}
		for_array(i, results) {
			result_eps[i] = lb_emit_struct_ep(p, res, cast(i32)i);
		}
		for_array(i, result_values) {
			lb_emit_store(p, result_eps[i], result_values[i]);
		}

		if (return_by_pointer) {
			lb_emit_defer_stmts(p, lbDeferExit_Return, nullptr);
			LLVMBuildRetVoid(p->builder);
			return;
		}

		res = lb_emit_load(p, res);
	}
	lb_build_return_stmt_internal(p, res);
}

void lb_build_if_stmt(lbProcedure *p, Ast *node) {
	ast_node(is, IfStmt, node);
	lb_open_scope(p, node->scope); // Scope #1

	if (is->init != nullptr) {
		// TODO(bill): Should this have a separate block to begin with?
	#if 1
		lbBlock *init = lb_create_block(p, "if.init");
		lb_emit_jump(p, init);
		lb_start_block(p, init);
	#endif
		lb_build_stmt(p, is->init);
	}
	lbBlock *then = lb_create_block(p, "if.then");
	lbBlock *done = lb_create_block(p, "if.done");
	lbBlock *else_ = done;
	if (is->else_stmt != nullptr) {
		else_ = lb_create_block(p, "if.else");
	}

	lb_build_cond(p, is->cond, then, else_);
	lb_start_block(p, then);

	if (is->label != nullptr) {
		lbTargetList *tl = lb_push_target_list(p, is->label, done, nullptr, nullptr);
		tl->is_block = true;
	}

	lb_build_stmt(p, is->body);

	lb_emit_jump(p, done);

	if (is->else_stmt != nullptr) {
		lb_start_block(p, else_);

		lb_open_scope(p, is->else_stmt->scope);
		lb_build_stmt(p, is->else_stmt);
		lb_close_scope(p, lbDeferExit_Default, nullptr);

		lb_emit_jump(p, done);
	}

	lb_start_block(p, done);
	lb_close_scope(p, lbDeferExit_Default, nullptr);
}

void lb_build_for_stmt(lbProcedure *p, Ast *node) {
	ast_node(fs, ForStmt, node);

	lb_open_scope(p, node->scope); // Open Scope here

	if (fs->init != nullptr) {
	#if 1
		lbBlock *init = lb_create_block(p, "for.init");
		lb_emit_jump(p, init);
		lb_start_block(p, init);
	#endif
		lb_build_stmt(p, fs->init);
	}
	lbBlock *body = lb_create_block(p, "for.body");
	lbBlock *done = lb_create_block(p, "for.done"); // NOTE(bill): Append later
	lbBlock *loop = body;
	if (fs->cond != nullptr) {
		loop = lb_create_block(p, "for.loop");
	}
	lbBlock *post = loop;
	if (fs->post != nullptr) {
		post = lb_create_block(p, "for.post");
	}


	lb_emit_jump(p, loop);
	lb_start_block(p, loop);

	if (loop != body) {
		lb_build_cond(p, fs->cond, body, done);
		lb_start_block(p, body);
	}

	lb_push_target_list(p, fs->label, done, post, nullptr);

	lb_build_stmt(p, fs->body);
	lb_close_scope(p, lbDeferExit_Default, nullptr);

	lb_pop_target_list(p);

	lb_emit_jump(p, post);

	if (fs->post != nullptr) {
		lb_start_block(p, post);
		lb_build_stmt(p, fs->post);
		lb_emit_jump(p, loop);
	}

	lb_start_block(p, done);
}

void lb_build_assign_stmt_array(lbProcedure *p, TokenKind op, lbAddr const &lhs, lbValue const &value) {
	Type *lhs_type = lb_addr_type(lhs);
	Type *rhs_type = value.type;
	Type *array_type = base_type(lhs_type);
	GB_ASSERT(is_type_array_like(array_type));
	i64 count = get_array_type_count(array_type);
	Type *elem_type = base_array_type(array_type);

	lbValue rhs = lb_emit_conv(p, value, lhs_type);

	bool inline_array_arith = type_size_of(array_type) <= build_context.max_align;


	if (lhs.kind == lbAddr_Swizzle) {
		GB_ASSERT(is_type_array(lhs_type));

		struct ValueAndIndex {
			lbValue value;
			u8      index;
		};

		bool indices_handled[4] = {};
		i32 indices[4] = {};
		i32 index_count = 0;
		for (u8 i = 0; i < lhs.swizzle.count; i++) {
			u8 index = lhs.swizzle.indices[i];
			if (indices_handled[index]) {
				continue;
			}
			indices[index_count++] = index;
		}
		gb_sort_array(indices, index_count, gb_i32_cmp(0));

		lbValue lhs_ptrs[4] = {};
		lbValue x_loads[4]  = {};
		lbValue y_loads[4]  = {};
		lbValue ops[4]      = {};

		for (i32 i = 0; i < index_count; i++) {
			lhs_ptrs[i] = lb_emit_array_epi(p, lhs.addr, indices[i]);
		}
		for (i32 i = 0; i < index_count; i++) {
			x_loads[i] = lb_emit_load(p, lhs_ptrs[i]);
		}
		for (i32 i = 0; i < index_count; i++) {
			y_loads[i].value = LLVMBuildExtractValue(p->builder, rhs.value, i, "");
			y_loads[i].type = elem_type;
		}
		for (i32 i = 0; i < index_count; i++) {
			ops[i] = lb_emit_arith(p, op, x_loads[i], y_loads[i], elem_type);
		}
		for (i32 i = 0; i < index_count; i++) {
			lb_emit_store(p, lhs_ptrs[i], ops[i]);
		}
		return;
	}


	lbValue x = lb_addr_get_ptr(p, lhs);


	if (inline_array_arith) {
	#if 1
		#if 1
		unsigned n = cast(unsigned)count;

		auto lhs_ptrs = slice_make<lbValue>(temporary_allocator(), n);
		auto x_loads  = slice_make<lbValue>(temporary_allocator(), n);
		auto y_loads  = slice_make<lbValue>(temporary_allocator(), n);
		auto ops      = slice_make<lbValue>(temporary_allocator(), n);

		for (unsigned i = 0; i < n; i++) {
			lhs_ptrs[i] = lb_emit_array_epi(p, x, i);
		}
		for (unsigned i = 0; i < n; i++) {
			x_loads[i] = lb_emit_load(p, lhs_ptrs[i]);
		}
		for (unsigned i = 0; i < n; i++) {
			y_loads[i].value = LLVMBuildExtractValue(p->builder, rhs.value, i, "");
			y_loads[i].type = elem_type;
		}
		for (unsigned i = 0; i < n; i++) {
			ops[i] = lb_emit_arith(p, op, x_loads[i], y_loads[i], elem_type);
		}
		for (unsigned i = 0; i < n; i++) {
			lb_emit_store(p, lhs_ptrs[i], ops[i]);
		}

		#else
		lbValue y = lb_address_from_load_or_generate_local(p, rhs);

		unsigned n = cast(unsigned)count;

		auto lhs_ptrs = slice_make<lbValue>(temporary_allocator(), n);
		auto rhs_ptrs = slice_make<lbValue>(temporary_allocator(), n);
		auto x_loads  = slice_make<lbValue>(temporary_allocator(), n);
		auto y_loads  = slice_make<lbValue>(temporary_allocator(), n);
		auto ops      = slice_make<lbValue>(temporary_allocator(), n);

		for (unsigned i = 0; i < n; i++) {
			lhs_ptrs[i] = lb_emit_array_epi(p, x, i);
		}
		for (unsigned i = 0; i < n; i++) {
			rhs_ptrs[i] = lb_emit_array_epi(p, y, i);
		}
		for (unsigned i = 0; i < n; i++) {
			x_loads[i] = lb_emit_load(p, lhs_ptrs[i]);
		}
		for (unsigned i = 0; i < n; i++) {
			y_loads[i] = lb_emit_load(p, rhs_ptrs[i]);
		}
		for (unsigned i = 0; i < n; i++) {
			ops[i] = lb_emit_arith(p, op, x_loads[i], y_loads[i], elem_type);
		}
		for (unsigned i = 0; i < n; i++) {
			lb_emit_store(p, lhs_ptrs[i], ops[i]);
		}
		#endif
	#else
		lbValue y = lb_address_from_load_or_generate_local(p, rhs);

		for (i64 i = 0; i < count; i++) {
			lbValue a_ptr = lb_emit_array_epi(p, x, i);
			lbValue b_ptr = lb_emit_array_epi(p, y, i);

			lbValue a = lb_emit_load(p, a_ptr);
			lbValue b = lb_emit_load(p, b_ptr);
			lbValue c = lb_emit_arith(p, op, a, b, elem_type);
			lb_emit_store(p, a_ptr, c);
		}
	#endif
	} else {
		lbValue y = lb_address_from_load_or_generate_local(p, rhs);

		auto loop_data = lb_loop_start(p, count, t_i32);

		lbValue a_ptr = lb_emit_array_ep(p, x, loop_data.idx);
		lbValue b_ptr = lb_emit_array_ep(p, y, loop_data.idx);

		lbValue a = lb_emit_load(p, a_ptr);
		lbValue b = lb_emit_load(p, b_ptr);
		lbValue c = lb_emit_arith(p, op, a, b, elem_type);
		lb_emit_store(p, a_ptr, c);

		lb_loop_end(p, loop_data);
	}
}
void lb_build_assign_stmt(lbProcedure *p, AstAssignStmt *as) {
	if (as->op.kind == Token_Eq) {
		auto lvals = array_make<lbAddr>(permanent_allocator(), 0, as->lhs.count);

		for_array(i, as->lhs) {
			Ast *lhs = as->lhs[i];
			lbAddr lval = {};
			if (!is_blank_ident(lhs)) {
				lval = lb_build_addr(p, lhs);
			}
			array_add(&lvals, lval);
		}
		lb_build_assignment(p, lvals, as->rhs);
		return;
	}
	GB_ASSERT(as->lhs.count == 1);
	GB_ASSERT(as->rhs.count == 1);
	// NOTE(bill): Only 1 += 1 is allowed, no tuples
	// +=, -=, etc
	i32 op_ = cast(i32)as->op.kind;
	op_ += Token_Add - Token_AddEq; // Convert += to +
	TokenKind op = cast(TokenKind)op_;
	if (op == Token_CmpAnd || op == Token_CmpOr) {
		Type *type = as->lhs[0]->tav.type;
		lbValue new_value = lb_emit_logical_binary_expr(p, op, as->lhs[0], as->rhs[0], type);

		lbAddr lhs = lb_build_addr(p, as->lhs[0]);
		lb_addr_store(p, lhs, new_value);
	} else {
		lbAddr lhs = lb_build_addr(p, as->lhs[0]);
		lbValue value = lb_build_expr(p, as->rhs[0]);

		Type *lhs_type = lb_addr_type(lhs);
		if (is_type_array(lhs_type)) {
			lb_build_assign_stmt_array(p, op, lhs, value);
			return;
		} else {
			lbValue old_value = lb_addr_load(p, lhs);
			Type *type = old_value.type;

			lbValue change = lb_emit_conv(p, value, type);
			lbValue new_value = lb_emit_arith(p, op, old_value, change, type);
			lb_addr_store(p, lhs, new_value);
		}
	}
}


void lb_build_stmt(lbProcedure *p, Ast *node) {
	Ast *prev_stmt = p->curr_stmt;
	defer (p->curr_stmt = prev_stmt);
	p->curr_stmt = node;

	if (p->curr_block != nullptr) {
		LLVMValueRef last_instr = LLVMGetLastInstruction(p->curr_block->block);
		if (lb_is_instr_terminating(last_instr)) {
			return;
		}
	}

	LLVMMetadataRef prev_debug_location = nullptr;
	if (p->debug_info != nullptr) {
		prev_debug_location = LLVMGetCurrentDebugLocation2(p->builder);
		LLVMSetCurrentDebugLocation2(p->builder, lb_debug_location_from_ast(p, node));
	}
	defer (if (prev_debug_location != nullptr) {
		LLVMSetCurrentDebugLocation2(p->builder, prev_debug_location);
	});

	u16 prev_state_flags = p->state_flags;
	defer (p->state_flags = prev_state_flags);

	if (node->state_flags != 0) {
		u16 in = node->state_flags;
		u16 out = p->state_flags;

		if (in & StateFlag_bounds_check) {
			out |= StateFlag_bounds_check;
			out &= ~StateFlag_no_bounds_check;
		} else if (in & StateFlag_no_bounds_check) {
			out |= StateFlag_no_bounds_check;
			out &= ~StateFlag_bounds_check;
		}

		p->state_flags = out;
	}

	switch (node->kind) {
	case_ast_node(bs, EmptyStmt, node);
	case_end;

	case_ast_node(us, UsingStmt, node);
	case_end;

	case_ast_node(ws, WhenStmt, node);
		lb_build_when_stmt(p, ws);
	case_end;


	case_ast_node(bs, BlockStmt, node);
		lbBlock *done = nullptr;
		if (bs->label != nullptr) {
			done = lb_create_block(p, "block.done");
			lbTargetList *tl = lb_push_target_list(p, bs->label, done, nullptr, nullptr);
			tl->is_block = true;
		}

		lb_open_scope(p, node->scope);
		lb_build_stmt_list(p, bs->stmts);
		lb_close_scope(p, lbDeferExit_Default, nullptr);

		if (done != nullptr) {
			lb_emit_jump(p, done);
			lb_start_block(p, done);
		}
	case_end;

	case_ast_node(vd, ValueDecl, node);
		if (!vd->is_mutable) {
			return;
		}

		bool is_static = false;
		if (vd->names.count > 0) {
			Entity *e = entity_of_node(vd->names[0]);
			if (e->flags & EntityFlag_Static) {
				// NOTE(bill): If one of the entities is static, they all are
				is_static = true;
			}
		}

		if (is_static) {
			lb_build_static_variables(p, vd);
			return;
		}

		auto lvals = array_make<lbAddr>(permanent_allocator(), 0, vd->names.count);

		for_array(i, vd->names) {
			Ast *name = vd->names[i];
			lbAddr lval = {};
			if (!is_blank_ident(name)) {
				Entity *e = entity_of_node(name);
				bool zero_init = true; // Always do it
				lval = lb_add_local(p, e->type, e, zero_init);
			}
			array_add(&lvals, lval);
		}
		lb_build_assignment(p, lvals, vd->values);
	case_end;

	case_ast_node(as, AssignStmt, node);
		lb_build_assign_stmt(p, as);
	case_end;

	case_ast_node(es, ExprStmt, node);
		lb_build_expr(p, es->expr);
	case_end;

	case_ast_node(ds, DeferStmt, node);
		lb_add_defer_node(p, p->scope_index, ds->stmt);
	case_end;

	case_ast_node(rs, ReturnStmt, node);
		lb_build_return_stmt(p, rs->results);
	case_end;

	case_ast_node(is, IfStmt, node);
		lb_build_if_stmt(p, node);
	case_end;

	case_ast_node(fs, ForStmt, node);
		lb_build_for_stmt(p, node);
	case_end;

	case_ast_node(rs, RangeStmt, node);
		lb_build_range_stmt(p, rs, node->scope);
	case_end;

	case_ast_node(rs, UnrollRangeStmt, node);
		lb_build_unroll_range_stmt(p, rs, node->scope);
	case_end;

	case_ast_node(ss, SwitchStmt, node);
		lb_build_switch_stmt(p, ss, node->scope);
	case_end;

	case_ast_node(ss, TypeSwitchStmt, node);
		lb_build_type_switch_stmt(p, ss);
	case_end;

	case_ast_node(bs, BranchStmt, node);
		lbBlock *block = nullptr;

		if (bs->label != nullptr) {
			lbBranchBlocks bb = lb_lookup_branch_blocks(p, bs->label);
			switch (bs->token.kind) {
			case Token_break:    block = bb.break_;    break;
			case Token_continue: block = bb.continue_; break;
			case Token_fallthrough:
				GB_PANIC("fallthrough cannot have a label");
				break;
			}
		} else {
			for (lbTargetList *t = p->target_list; t != nullptr && block == nullptr; t = t->prev) {
				if (t->is_block) {
					continue;
				}

				switch (bs->token.kind) {
				case Token_break:       block = t->break_;       break;
				case Token_continue:    block = t->continue_;    break;
				case Token_fallthrough: block = t->fallthrough_; break;
				}
			}
		}
		if (block != nullptr) {
			lb_emit_defer_stmts(p, lbDeferExit_Branch, block);
		}
		lb_emit_jump(p, block);
	case_end;
	}
}

lbValue lb_emit_select(lbProcedure *p, lbValue cond, lbValue x, lbValue y) {
	cond = lb_emit_conv(p, cond, t_llvm_bool);
	lbValue res = {};
	res.value = LLVMBuildSelect(p->builder, cond.value, x.value, y.value, "");
	res.type = x.type;
	return res;
}

lbValue lb_const_nil(lbModule *m, Type *type) {
	LLVMValueRef v = LLVMConstNull(lb_type(m, type));
	return lbValue{v, type};
}

lbValue lb_const_undef(lbModule *m, Type *type) {
	LLVMValueRef v = LLVMGetUndef(lb_type(m, type));
	return lbValue{v, type};
}


lbValue lb_const_int(lbModule *m, Type *type, u64 value) {
	lbValue res = {};
	res.value = LLVMConstInt(lb_type(m, type), cast(unsigned long long)value, !is_type_unsigned(type));
	res.type = type;
	return res;
}

lbValue lb_const_string(lbModule *m, String const &value) {
	return lb_const_value(m, t_string, exact_value_string(value));
}


lbValue lb_const_bool(lbModule *m, Type *type, bool value) {
	lbValue res = {};
	res.value = LLVMConstInt(lb_type(m, type), value, false);
	res.type = type;
	return res;
}

LLVMValueRef lb_const_f16(lbModule *m, f32 f, Type *type=t_f16) {
	GB_ASSERT(type_size_of(type) == 2);

	u16 u = f32_to_f16(f);
	if (is_type_different_to_arch_endianness(type)) {
		u = gb_endian_swap16(u);
	}
	LLVMValueRef i = LLVMConstInt(LLVMInt16TypeInContext(m->ctx), u, false);
	return LLVMConstBitCast(i, lb_type(m, type));
}

LLVMValueRef lb_const_f32(lbModule *m, f32 f, Type *type=t_f32) {
	GB_ASSERT(type_size_of(type) == 4);
	u32 u = bit_cast<u32>(f);
	if (is_type_different_to_arch_endianness(type)) {
		u = gb_endian_swap32(u);
	}
	LLVMValueRef i = LLVMConstInt(LLVMInt32TypeInContext(m->ctx), u, false);
	return LLVMConstBitCast(i, lb_type(m, type));
}

lbValue lb_emit_min(lbProcedure *p, Type *t, lbValue x, lbValue y) {
	x = lb_emit_conv(p, x, t);
	y = lb_emit_conv(p, y, t);
	return lb_emit_select(p, lb_emit_comp(p, Token_Lt, x, y), x, y);
}
lbValue lb_emit_max(lbProcedure *p, Type *t, lbValue x, lbValue y) {
	x = lb_emit_conv(p, x, t);
	y = lb_emit_conv(p, y, t);
	return lb_emit_select(p, lb_emit_comp(p, Token_Gt, x, y), x, y);
}


lbValue lb_emit_clamp(lbProcedure *p, Type *t, lbValue x, lbValue min, lbValue max) {
	lbValue z = {};
	z = lb_emit_max(p, t, x, min);
	z = lb_emit_min(p, t, z, max);
	return z;
}



LLVMValueRef lb_find_or_add_entity_string_ptr(lbModule *m, String const &str) {
	StringHashKey key = string_hash_string(str);
	LLVMValueRef *found = string_map_get(&m->const_strings, key);
	if (found != nullptr) {
		return *found;
	} else {
		LLVMValueRef indices[2] = {llvm_zero(m), llvm_zero(m)};
		LLVMValueRef data = LLVMConstStringInContext(m->ctx,
			cast(char const *)str.text,
			cast(unsigned)str.len,
			false);


		isize max_len = 7+8+1;
		char *name = gb_alloc_array(permanent_allocator(), char, max_len);

		u32 id = cast(u32)gb_atomic32_fetch_add(&m->gen->global_array_index, 1);
		isize len = gb_snprintf(name, max_len, "csbs$%x", id);
		len -= 1;

		LLVMValueRef global_data = LLVMAddGlobal(m->mod, LLVMTypeOf(data), name);
		LLVMSetInitializer(global_data, data);
		LLVMSetLinkage(global_data, LLVMInternalLinkage);

		LLVMValueRef ptr = LLVMConstInBoundsGEP(global_data, indices, 2);
		string_map_set(&m->const_strings, key, ptr);
		return ptr;
	}
}

lbValue lb_find_or_add_entity_string(lbModule *m, String const &str) {
	LLVMValueRef ptr = nullptr;
	if (str.len != 0) {
		ptr = lb_find_or_add_entity_string_ptr(m, str);
	} else {
		ptr = LLVMConstNull(lb_type(m, t_u8_ptr));
	}
	LLVMValueRef str_len = LLVMConstInt(lb_type(m, t_int), str.len, true);
	LLVMValueRef values[2] = {ptr, str_len};

	lbValue res = {};
	res.value = llvm_const_named_struct(lb_type(m, t_string), values, 2);
	res.type = t_string;
	return res;
}

lbValue lb_find_or_add_entity_string_byte_slice(lbModule *m, String const &str) {
	LLVMValueRef indices[2] = {llvm_zero(m), llvm_zero(m)};
	LLVMValueRef data = LLVMConstStringInContext(m->ctx,
		cast(char const *)str.text,
		cast(unsigned)str.len,
		false);


	char *name = nullptr;
	{
		isize max_len = 7+8+1;
		name = gb_alloc_array(permanent_allocator(), char, max_len);
		u32 id = cast(u32)gb_atomic32_fetch_add(&m->gen->global_array_index, 1);
		isize len = gb_snprintf(name, max_len, "csbs$%x", id);
		len -= 1;
	}
	LLVMValueRef global_data = LLVMAddGlobal(m->mod, LLVMTypeOf(data), name);
	LLVMSetInitializer(global_data, data);
	LLVMSetLinkage(global_data, LLVMInternalLinkage);

	LLVMValueRef ptr = nullptr;
	if (str.len != 0) {
		ptr = LLVMConstInBoundsGEP(global_data, indices, 2);
	} else {
		ptr = LLVMConstNull(lb_type(m, t_u8_ptr));
	}
	LLVMValueRef len = LLVMConstInt(lb_type(m, t_int), str.len, true);
	LLVMValueRef values[2] = {ptr, len};

	lbValue res = {};
	res.value = llvm_const_named_struct(lb_type(m, t_u8_slice), values, 2);
	res.type = t_u8_slice;
	return res;
}

isize lb_type_info_index(CheckerInfo *info, Type *type, bool err_on_not_found=true) {
	isize index = type_info_index(info, type, false);
	if (index >= 0) {
		auto *set = &info->minimum_dependency_type_info_set;
		for_array(i, set->entries) {
			if (set->entries[i].ptr == index) {
				return i+1;
			}
		}
	}
	if (err_on_not_found) {
		GB_PANIC("NOT FOUND lb_type_info_index %s @ index %td", type_to_string(type), index);
	}
	return -1;
}

lbValue lb_typeid(lbModule *m, Type *type) {
	type = default_type(type);

	u64 id = cast(u64)lb_type_info_index(m->info, type);
	GB_ASSERT(id >= 0);

	u64 kind = Typeid_Invalid;
	u64 named = is_type_named(type) && type->kind != Type_Basic;
	u64 special = 0;
	u64 reserved = 0;

	Type *bt = base_type(type);
	TypeKind tk = bt->kind;
	switch (tk) {
	case Type_Basic: {
		u32 flags = bt->Basic.flags;
		if (flags & BasicFlag_Boolean)  kind = Typeid_Boolean;
		if (flags & BasicFlag_Integer)  kind = Typeid_Integer;
		if (flags & BasicFlag_Unsigned) kind = Typeid_Integer;
		if (flags & BasicFlag_Float)    kind = Typeid_Float;
		if (flags & BasicFlag_Complex)  kind = Typeid_Complex;
		if (flags & BasicFlag_Pointer)  kind = Typeid_Pointer;
		if (flags & BasicFlag_String)   kind = Typeid_String;
		if (flags & BasicFlag_Rune)     kind = Typeid_Rune;
	} break;
	case Type_Pointer:         kind = Typeid_Pointer;          break;
	case Type_Array:           kind = Typeid_Array;            break;
	case Type_EnumeratedArray: kind = Typeid_Enumerated_Array; break;
	case Type_Slice:           kind = Typeid_Slice;            break;
	case Type_DynamicArray:    kind = Typeid_Dynamic_Array;    break;
	case Type_Map:             kind = Typeid_Map;              break;
	case Type_Struct:          kind = Typeid_Struct;           break;
	case Type_Enum:            kind = Typeid_Enum;             break;
	case Type_Union:           kind = Typeid_Union;            break;
	case Type_Tuple:           kind = Typeid_Tuple;            break;
	case Type_Proc:            kind = Typeid_Procedure;        break;
	case Type_BitSet:          kind = Typeid_Bit_Set;          break;
	case Type_SimdVector:      kind = Typeid_Simd_Vector;      break;
	case Type_RelativePointer: kind = Typeid_Relative_Pointer; break;
	case Type_RelativeSlice:   kind = Typeid_Relative_Slice;   break;
	}

	if (is_type_cstring(type)) {
		special = 1;
	} else if (is_type_integer(type) && !is_type_unsigned(type)) {
		special = 1;
	}

	u64 data = 0;
	if (build_context.word_size == 4) {
		GB_ASSERT(id <= (1u<<24u));
		data |= (id       &~ (1u<<24)) << 0u;  // index
		data |= (kind     &~ (1u<<5))  << 24u; // kind
		data |= (named    &~ (1u<<1))  << 29u; // kind
		data |= (special  &~ (1u<<1))  << 30u; // kind
		data |= (reserved &~ (1u<<1))  << 31u; // kind
	} else {
		GB_ASSERT(build_context.word_size == 8);
		GB_ASSERT(id <= (1ull<<56u));
		data |= (id       &~ (1ull<<56)) << 0ul;  // index
		data |= (kind     &~ (1ull<<5))  << 56ull; // kind
		data |= (named    &~ (1ull<<1))  << 61ull; // kind
		data |= (special  &~ (1ull<<1))  << 62ull; // kind
		data |= (reserved &~ (1ull<<1))  << 63ull; // kind
	}

	lbValue res = {};
	res.value = LLVMConstInt(lb_type(m, t_typeid), data, false);
	res.type = t_typeid;
	return res;
}

lbValue lb_type_info(lbModule *m, Type *type) {
	type = default_type(type);

	isize index = lb_type_info_index(m->info, type);
	GB_ASSERT(index >= 0);

	LLVMTypeRef it = lb_type(m, t_int);
	LLVMValueRef indices[2] = {
		LLVMConstInt(it, 0, false),
		LLVMConstInt(it, index, true),
	};

	lbValue value = {};
	value.value = LLVMConstGEP(lb_global_type_info_data_ptr(m).value, indices, gb_count_of(indices));
	value.type = t_type_info_ptr;
	return value;
}

LLVMValueRef lb_build_constant_array_values(lbModule *m, Type *type, Type *elem_type, isize count, LLVMValueRef *values, bool allow_local) {
	bool is_local = allow_local && m->curr_procedure != nullptr;
	bool is_const = true;
	if (is_local) {
		for (isize i = 0; i < count; i++) {
			GB_ASSERT(values[i] != nullptr);
			if (!LLVMIsConstant(values[i])) {
				is_const = false;
				break;
			}
		}
	}

	if (!is_const) {
		lbProcedure *p = m->curr_procedure;
		GB_ASSERT(p != nullptr);
		lbAddr v = lb_add_local_generated(p, type, false);
		lbValue ptr = lb_addr_get_ptr(p, v);
		for (isize i = 0; i < count; i++) {
			lbValue elem = lb_emit_array_epi(p, ptr, i);
			LLVMBuildStore(p->builder, values[i], elem.value);
		}
		return lb_addr_load(p, v).value;
	}

	return llvm_const_array(lb_type(m, elem_type), values, cast(unsigned int)count);
}

lbValue lb_find_procedure_value_from_entity(lbModule *m, Entity *e) {
	GB_ASSERT(is_type_proc(e->type));
	e = strip_entity_wrapping(e);
	GB_ASSERT(e != nullptr);
	auto *found = map_get(&m->values, hash_entity(e));
	if (found) {
		return *found;
	}

	bool ignore_body = false;

	if (USE_SEPARTE_MODULES) {
		lbModule *other_module = lb_pkg_module(m->gen, e->pkg);
		ignore_body = other_module != m;
	}

	lbProcedure *missing_proc = lb_create_procedure(m, e, ignore_body);
	found = map_get(&m->values, hash_entity(e));
	if (found) {
		return *found;
	}

	GB_PANIC("Error in: %s, missing procedure %.*s\n", token_pos_to_string(e->token.pos), LIT(e->token.string));
	return {};
}

void lb_set_entity_from_other_modules_linkage_correctly(lbModule *other_module, Entity *e, String const &name) {
	if (other_module == nullptr) {
		return;
	}
	char const *cname = alloc_cstring(temporary_allocator(), name);

	LLVMValueRef other_global = nullptr;
	if (e->kind == Entity_Variable) {
		other_global = LLVMGetNamedGlobal(other_module->mod, cname);
	} else if (e->kind == Entity_Procedure) {
		other_global = LLVMGetNamedFunction(other_module->mod, cname);
	}
	if (other_global) {
		LLVMSetLinkage(other_global, LLVMExternalLinkage);
	}
}

lbValue lb_find_value_from_entity(lbModule *m, Entity *e) {
	e = strip_entity_wrapping(e);
	GB_ASSERT(e != nullptr);

	GB_ASSERT(e->token.string != "_");

	if (e->kind == Entity_Procedure) {
		return lb_find_procedure_value_from_entity(m, e);
	}

	auto *found = map_get(&m->values, hash_entity(e));
	if (found) {
		return *found;
	}

	if (USE_SEPARTE_MODULES) {
		lbModule *other_module = lb_pkg_module(m->gen, e->pkg);

		// TODO(bill): correct this logic
		bool is_external = other_module != m;
		if (!is_external) {
			if (e->code_gen_module != nullptr) {
				other_module = e->code_gen_module;
			} else {
				other_module = nullptr;
			}
			is_external = other_module != m;
		}

		if (is_external) {
			String name = lb_get_entity_name(other_module, e);

			lbValue g = {};
			g.value = LLVMAddGlobal(m->mod, lb_type(m, e->type), alloc_cstring(permanent_allocator(), name));
			g.type = alloc_type_pointer(e->type);
			lb_add_entity(m, e, g);
			lb_add_member(m, name, g);

			LLVMSetLinkage(g.value, LLVMExternalLinkage);

			lb_set_entity_from_other_modules_linkage_correctly(other_module, e, name);

			// LLVMSetLinkage(other_g.value, LLVMExternalLinkage);

			if (e->Variable.thread_local_model != "") {
				LLVMSetThreadLocal(g.value, true);

				String m = e->Variable.thread_local_model;
				LLVMThreadLocalMode mode = LLVMGeneralDynamicTLSModel;
				if (m == "default") {
					mode = LLVMGeneralDynamicTLSModel;
				} else if (m == "localdynamic") {
					mode = LLVMLocalDynamicTLSModel;
				} else if (m == "initialexec") {
					mode = LLVMInitialExecTLSModel;
				} else if (m == "localexec") {
					mode = LLVMLocalExecTLSModel;
				} else {
					GB_PANIC("Unhandled thread local mode %.*s", LIT(m));
				}
				LLVMSetThreadLocalMode(g.value, mode);
			}


			return g;
		}
	}

	GB_PANIC("\n\tError in: %s, missing value '%.*s'\n", token_pos_to_string(e->token.pos), LIT(e->token.string));
	return {};
}





lbValue lb_const_value(lbModule *m, Type *type, ExactValue value, bool allow_local) {
	LLVMContextRef ctx = m->ctx;

	type = default_type(type);
	Type *original_type = type;

	lbValue res = {};
	res.type = original_type;
	type = core_type(type);
	value = convert_exact_value_for_type(value, type);

	if (value.kind == ExactValue_Typeid) {
		return lb_typeid(m, value.value_typeid);
	}

	if (value.kind == ExactValue_Invalid) {
		return lb_const_nil(m, type);
	}

	if (value.kind == ExactValue_Procedure) {
		Ast *expr = unparen_expr(value.value_procedure);
		if (expr->kind == Ast_ProcLit) {
			return lb_generate_anonymous_proc_lit(m, str_lit("_proclit"), expr);
		}
		Entity *e = entity_from_expr(expr);
		return lb_find_procedure_value_from_entity(m, e);
	}

	bool is_local = allow_local && m->curr_procedure != nullptr;

	// GB_ASSERT_MSG(is_type_typed(type), "%s", type_to_string(type));

	if (is_type_slice(type)) {
		if (value.kind == ExactValue_String) {
			GB_ASSERT(is_type_u8_slice(type));
			res.value = lb_find_or_add_entity_string_byte_slice(m, value.value_string).value;
			return res;
		} else {
			ast_node(cl, CompoundLit, value.value_compound);

			isize count = cl->elems.count;
			if (count == 0) {
				return lb_const_nil(m, type);
			}
			count = gb_max(cl->max_count, count);
			Type *elem = base_type(type)->Slice.elem;
			Type *t = alloc_type_array(elem, count);
			lbValue backing_array = lb_const_value(m, t, value, allow_local);

			LLVMValueRef array_data = nullptr;

			if (is_local) {
				// NOTE(bill, 2020-06-08): This is a bit of a hack but a "constant" slice needs
				// its backing data on the stack
				lbProcedure *p = m->curr_procedure;
				LLVMPositionBuilderAtEnd(p->builder, p->decl_block->block);

				LLVMTypeRef llvm_type = lb_type(m, t);
				array_data = LLVMBuildAlloca(p->builder, llvm_type, "");
				LLVMSetAlignment(array_data, 16); // TODO(bill): Make this configurable
				LLVMPositionBuilderAtEnd(p->builder, p->curr_block->block);
				LLVMBuildStore(p->builder, backing_array.value, array_data);

				{
					LLVMValueRef indices[2] = {llvm_zero(m), llvm_zero(m)};
					LLVMValueRef ptr = LLVMBuildInBoundsGEP(p->builder, array_data, indices, 2, "");
					LLVMValueRef len = LLVMConstInt(lb_type(m, t_int), count, true);
					lbAddr slice = lb_add_local_generated(p, type, false);
					lb_fill_slice(p, slice, {ptr, alloc_type_pointer(elem)}, {len, t_int});
					return lb_addr_load(p, slice);
				}
			} else {
				isize max_len = 7+8+1;
				char *str = gb_alloc_array(permanent_allocator(), char, max_len);
				u32 id = cast(u32)gb_atomic32_fetch_add(&m->gen->global_array_index, 1);
				isize len = gb_snprintf(str, max_len, "csba$%x", id);

				String name = make_string(cast(u8 *)str, len-1);

				Entity *e = alloc_entity_constant(nullptr, make_token_ident(name), t, value);
				array_data = LLVMAddGlobal(m->mod, lb_type(m, t), str);
				LLVMSetInitializer(array_data, backing_array.value);

				lbValue g = {};
				g.value = array_data;
				g.type = t;

				lb_add_entity(m, e, g);
				lb_add_member(m, name, g);

				{
					LLVMValueRef indices[2] = {llvm_zero(m), llvm_zero(m)};
					LLVMValueRef ptr = LLVMConstInBoundsGEP(array_data, indices, 2);
					LLVMValueRef len = LLVMConstInt(lb_type(m, t_int), count, true);
					LLVMValueRef values[2] = {ptr, len};

					res.value = llvm_const_named_struct(lb_type(m, original_type), values, 2);
					return res;
				}
			}


		}
	} else if (is_type_array(type) && value.kind == ExactValue_String && !is_type_u8(core_array_type(type))) {
		if (is_type_rune_array(type) && value.kind == ExactValue_String) {
			i64 count  = type->Array.count;
			Type *elem = type->Array.elem;
			LLVMTypeRef et = lb_type(m, elem);

			Rune rune;
			isize offset = 0;
			isize width = 1;
			String s = value.value_string;

			LLVMValueRef *elems = gb_alloc_array(permanent_allocator(), LLVMValueRef, count);

			for (i64 i = 0; i < count && offset < s.len; i++) {
				width = gb_utf8_decode(s.text+offset, s.len-offset, &rune);
				offset += width;

				elems[i] = LLVMConstInt(et, rune, true);

			}
			GB_ASSERT(offset == s.len);

			res.value = llvm_const_array(et, elems, cast(unsigned)count);
			return res;
		}
		GB_PANIC("This should not have happened!\n");

		LLVMValueRef data = LLVMConstStringInContext(ctx,
			cast(char const *)value.value_string.text,
			cast(unsigned)value.value_string.len,
			false /*DontNullTerminate*/);
		res.value = data;
		return res;
	} else if (is_type_u8_array(type) && value.kind == ExactValue_String) {
		GB_ASSERT(type->Array.count == value.value_string.len);
		LLVMValueRef data = LLVMConstStringInContext(ctx,
			cast(char const *)value.value_string.text,
			cast(unsigned)value.value_string.len,
			true /*DontNullTerminate*/);
		res.value = data;
		return res;
	} else if (is_type_array(type) &&
	    value.kind != ExactValue_Invalid &&
	    value.kind != ExactValue_String &&
	    value.kind != ExactValue_Compound) {

		i64 count  = type->Array.count;
		Type *elem = type->Array.elem;


		lbValue single_elem = lb_const_value(m, elem, value, allow_local);

		LLVMValueRef *elems = gb_alloc_array(permanent_allocator(), LLVMValueRef, count);
		for (i64 i = 0; i < count; i++) {
			elems[i] = single_elem.value;
		}

		res.value = llvm_const_array(lb_type(m, elem), elems, cast(unsigned)count);
		return res;
	}

	switch (value.kind) {
	case ExactValue_Invalid:
		res.value = LLVMConstNull(lb_type(m, original_type));
		return res;
	case ExactValue_Bool:
		res.value = LLVMConstInt(lb_type(m, original_type), value.value_bool, false);
		return res;
	case ExactValue_String:
		{
			LLVMValueRef ptr = lb_find_or_add_entity_string_ptr(m, value.value_string);
			lbValue res = {};
			res.type = default_type(original_type);
			if (is_type_cstring(res.type)) {
				res.value = ptr;
			} else {
				if (value.value_string.len == 0) {
					ptr = LLVMConstNull(lb_type(m, t_u8_ptr));
				}
				LLVMValueRef str_len = LLVMConstInt(lb_type(m, t_int), value.value_string.len, true);
				LLVMValueRef values[2] = {ptr, str_len};

				res.value = llvm_const_named_struct(lb_type(m, original_type), values, 2);
			}

			return res;
		}

	case ExactValue_Integer:
		if (is_type_pointer(type)) {
			unsigned len = cast(unsigned)value.value_integer.len;
			LLVMTypeRef t = lb_type(m, original_type);
			if (len == 0) {
				res.value = LLVMConstNull(t);
			} else {
				LLVMValueRef i = LLVMConstIntOfArbitraryPrecision(lb_type(m, t_uintptr), len, big_int_ptr(&value.value_integer));
				res.value = LLVMConstIntToPtr(i, t);
			}
		} else {
			unsigned len = cast(unsigned)value.value_integer.len;
			if (len == 0) {
				res.value = LLVMConstNull(lb_type(m, original_type));
			} else {
				u64 *words = big_int_ptr(&value.value_integer);
				if (is_type_different_to_arch_endianness(type)) {
					// NOTE(bill): Swap byte order for different endianness
					i64 sz = type_size_of(type);
					isize byte_len = gb_size_of(u64)*len;
					u8 *old_bytes = cast(u8 *)words;
					// TODO(bill): Use a different allocator here for a temporary allocation
					u8 *new_bytes = cast(u8 *)gb_alloc_align(permanent_allocator(), byte_len, gb_align_of(u64));
					for (i64 i = 0; i < sz; i++) {
						new_bytes[i] = old_bytes[sz-1-i];
					}
					words = cast(u64 *)new_bytes;
				}
				res.value = LLVMConstIntOfArbitraryPrecision(lb_type(m, original_type), len, words);
				if (value.value_integer.neg) {
					res.value = LLVMConstNeg(res.value);
				}
			}
		}
		return res;
	case ExactValue_Float:
		if (is_type_different_to_arch_endianness(type)) {
			u64 u = bit_cast<u64>(value.value_float);
			u = gb_endian_swap64(u);
			res.value = LLVMConstReal(lb_type(m, original_type), bit_cast<f64>(u));
		} else {
			res.value = LLVMConstReal(lb_type(m, original_type), value.value_float);
		}
		return res;
	case ExactValue_Complex:
		{
			LLVMValueRef values[2] = {};
			switch (8*type_size_of(type)) {
			case 32:
				values[0] = lb_const_f16(m, cast(f32)value.value_complex->real);
				values[1] = lb_const_f16(m, cast(f32)value.value_complex->imag);
				break;
			case 64:
				values[0] = lb_const_f32(m, cast(f32)value.value_complex->real);
				values[1] = lb_const_f32(m, cast(f32)value.value_complex->imag);
				break;
			case 128:
				values[0] = LLVMConstReal(lb_type(m, t_f64), value.value_complex->real);
				values[1] = LLVMConstReal(lb_type(m, t_f64), value.value_complex->imag);
				break;
			}

			res.value = llvm_const_named_struct(lb_type(m, original_type), values, 2);
			return res;
		}
		break;
	case ExactValue_Quaternion:
		{
			LLVMValueRef values[4] = {};
			switch (8*type_size_of(type)) {
			case 64:
				// @QuaternionLayout
				values[3] = lb_const_f16(m, cast(f32)value.value_quaternion->real);
				values[0] = lb_const_f16(m, cast(f32)value.value_quaternion->imag);
				values[1] = lb_const_f16(m, cast(f32)value.value_quaternion->jmag);
				values[2] = lb_const_f16(m, cast(f32)value.value_quaternion->kmag);
				break;
			case 128:
				// @QuaternionLayout
				values[3] = lb_const_f32(m, cast(f32)value.value_quaternion->real);
				values[0] = lb_const_f32(m, cast(f32)value.value_quaternion->imag);
				values[1] = lb_const_f32(m, cast(f32)value.value_quaternion->jmag);
				values[2] = lb_const_f32(m, cast(f32)value.value_quaternion->kmag);
				break;
			case 256:
				// @QuaternionLayout
				values[3] = LLVMConstReal(lb_type(m, t_f64), value.value_quaternion->real);
				values[0] = LLVMConstReal(lb_type(m, t_f64), value.value_quaternion->imag);
				values[1] = LLVMConstReal(lb_type(m, t_f64), value.value_quaternion->jmag);
				values[2] = LLVMConstReal(lb_type(m, t_f64), value.value_quaternion->kmag);
				break;
			}

			res.value = llvm_const_named_struct(lb_type(m, original_type), values, 4);
			return res;
		}
		break;

	case ExactValue_Pointer:
		res.value = LLVMConstIntToPtr(LLVMConstInt(lb_type(m, t_uintptr), value.value_pointer, false), lb_type(m, original_type));
		return res;

	case ExactValue_Compound:
		if (is_type_slice(type)) {
			return lb_const_value(m, type, value, allow_local);
		} else if (is_type_array(type)) {
			ast_node(cl, CompoundLit, value.value_compound);
			Type *elem_type = type->Array.elem;
			isize elem_count = cl->elems.count;
			if (elem_count == 0 || !elem_type_can_be_constant(elem_type)) {
				return lb_const_nil(m, original_type);
			}
			if (cl->elems[0]->kind == Ast_FieldValue) {
				// TODO(bill): This is O(N*M) and will be quite slow; it should probably be sorted before hand
				LLVMValueRef *values = gb_alloc_array(temporary_allocator(), LLVMValueRef, type->Array.count);

				isize value_index = 0;
				for (i64 i = 0; i < type->Array.count; i++) {
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
							if (op != Token_RangeHalf) {
								hi += 1;
							}
							if (lo == i) {
								TypeAndValue tav = fv->value->tav;
								LLVMValueRef val = lb_const_value(m, elem_type, tav.value, allow_local).value;
								for (i64 k = lo; k < hi; k++) {
									values[value_index++] = val;
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
								LLVMValueRef val = lb_const_value(m, elem_type, tav.value, allow_local).value;
								values[value_index++] = val;
								found = true;
								break;
							}
						}
					}

					if (!found) {
						values[value_index++] = LLVMConstNull(lb_type(m, elem_type));
					}
				}

				res.value = lb_build_constant_array_values(m, type, elem_type, type->Array.count, values, allow_local);
				return res;
			} else {
				GB_ASSERT_MSG(elem_count == type->Array.count, "%td != %td", elem_count, type->Array.count);

				LLVMValueRef *values = gb_alloc_array(temporary_allocator(), LLVMValueRef, type->Array.count);

				for (isize i = 0; i < elem_count; i++) {
					TypeAndValue tav = cl->elems[i]->tav;
					GB_ASSERT(tav.mode != Addressing_Invalid);
					values[i] = lb_const_value(m, elem_type, tav.value, allow_local).value;
				}
				for (isize i = elem_count; i < type->Array.count; i++) {
					values[i] = LLVMConstNull(lb_type(m, elem_type));
				}

				res.value = lb_build_constant_array_values(m, type, elem_type, type->Array.count, values, allow_local);
				return res;
			}
		} else if (is_type_enumerated_array(type)) {
			ast_node(cl, CompoundLit, value.value_compound);
			Type *elem_type = type->EnumeratedArray.elem;
			isize elem_count = cl->elems.count;
			if (elem_count == 0 || !elem_type_can_be_constant(elem_type)) {
				return lb_const_nil(m, original_type);
			}
			if (cl->elems[0]->kind == Ast_FieldValue) {
				// TODO(bill): This is O(N*M) and will be quite slow; it should probably be sorted before hand
				LLVMValueRef *values = gb_alloc_array(temporary_allocator(), LLVMValueRef, type->EnumeratedArray.count);

				isize value_index = 0;

				i64 total_lo = exact_value_to_i64(type->EnumeratedArray.min_value);
				i64 total_hi = exact_value_to_i64(type->EnumeratedArray.max_value);

				for (i64 i = total_lo; i <= total_hi; i++) {
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
							if (op != Token_RangeHalf) {
								hi += 1;
							}
							if (lo == i) {
								TypeAndValue tav = fv->value->tav;
								LLVMValueRef val = lb_const_value(m, elem_type, tav.value, allow_local).value;
								for (i64 k = lo; k < hi; k++) {
									values[value_index++] = val;
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
								LLVMValueRef val = lb_const_value(m, elem_type, tav.value, allow_local).value;
								values[value_index++] = val;
								found = true;
								break;
							}
						}
					}

					if (!found) {
						values[value_index++] = LLVMConstNull(lb_type(m, elem_type));
					}
				}

				res.value = lb_build_constant_array_values(m, type, elem_type, type->EnumeratedArray.count, values, allow_local);
				return res;
			} else {
				GB_ASSERT_MSG(elem_count == type->EnumeratedArray.count, "%td != %td", elem_count, type->EnumeratedArray.count);

				LLVMValueRef *values = gb_alloc_array(temporary_allocator(), LLVMValueRef, type->EnumeratedArray.count);

				for (isize i = 0; i < elem_count; i++) {
					TypeAndValue tav = cl->elems[i]->tav;
					GB_ASSERT(tav.mode != Addressing_Invalid);
					values[i] = lb_const_value(m, elem_type, tav.value, allow_local).value;
				}
				for (isize i = elem_count; i < type->EnumeratedArray.count; i++) {
					values[i] = LLVMConstNull(lb_type(m, elem_type));
				}

				res.value = lb_build_constant_array_values(m, type, elem_type, type->EnumeratedArray.count, values, allow_local);
				return res;
			}
		} else if (is_type_simd_vector(type)) {
			ast_node(cl, CompoundLit, value.value_compound);

			Type *elem_type = type->SimdVector.elem;
			isize elem_count = cl->elems.count;
			if (elem_count == 0) {
				return lb_const_nil(m, original_type);
			}
			GB_ASSERT(elem_type_can_be_constant(elem_type));

			isize total_elem_count = type->SimdVector.count;
			LLVMValueRef *values = gb_alloc_array(temporary_allocator(), LLVMValueRef, total_elem_count);

			for (isize i = 0; i < elem_count; i++) {
				TypeAndValue tav = cl->elems[i]->tav;
				GB_ASSERT(tav.mode != Addressing_Invalid);
				values[i] = lb_const_value(m, elem_type, tav.value, allow_local).value;
			}
			LLVMTypeRef et = lb_type(m, elem_type);

			for (isize i = elem_count; i < type->SimdVector.count; i++) {
				values[i] = LLVMConstNull(et);
			}
			for (isize i = 0; i< total_elem_count; i++) {
				values[i] = llvm_const_cast(values[i], et);
			}

			res.value = LLVMConstVector(values, cast(unsigned)total_elem_count);
			return res;
		} else if (is_type_struct(type)) {
			ast_node(cl, CompoundLit, value.value_compound);

			if (cl->elems.count == 0) {
				return lb_const_nil(m, original_type);
			}

			if (is_type_raw_union(type)) {
				return lb_const_nil(m, original_type);
			}

			isize offset = 0;
			if (type->Struct.custom_align > 0) {
				offset = 1;
			}

			isize value_count = type->Struct.fields.count + offset;
			LLVMValueRef *values = gb_alloc_array(temporary_allocator(), LLVMValueRef, value_count);
			bool *visited = gb_alloc_array(temporary_allocator(), bool, value_count);

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
						if (elem_type_can_be_constant(f->type)) {
							values[offset+f->Variable.field_index] = lb_const_value(m, f->type, tav.value, allow_local).value;
							visited[offset+f->Variable.field_index] = true;
						}
					}
				} else {
					for_array(i, cl->elems) {
						Entity *f = type->Struct.fields[i];
						TypeAndValue tav = cl->elems[i]->tav;
						ExactValue val = {};
						if (tav.mode != Addressing_Invalid) {
							val = tav.value;
						}
						if (elem_type_can_be_constant(f->type)) {
							values[offset+f->Variable.field_index]  = lb_const_value(m, f->type, val, allow_local).value;
							visited[offset+f->Variable.field_index] = true;
						}
					}
				}
			}

			for (isize i = 0; i < type->Struct.fields.count; i++) {
				if (!visited[offset+i]) {
					GB_ASSERT(values[offset+i] == nullptr);
					values[offset+i] = lb_const_nil(m, get_struct_field_type(type, i)).value;
				}
			}

			if (type->Struct.custom_align > 0) {
				values[0] = LLVMConstNull(lb_alignment_prefix_type_hack(m, type->Struct.custom_align));
			}

			bool is_constant = true;

			for (isize i = 0; i < value_count; i++) {
				LLVMValueRef val = values[i];
				if (!LLVMIsConstant(val)) {
					GB_ASSERT(is_local);
					GB_ASSERT(LLVMGetInstructionOpcode(val) == LLVMLoad);
					is_constant = false;
				}
			}

			if (is_constant) {
				res.value = llvm_const_named_struct(lb_type(m, original_type), values, cast(unsigned)value_count);
				return res;
			} else {
				// TODO(bill): THIS IS HACK BUT IT WORKS FOR WHAT I NEED
				LLVMValueRef *old_values = values;
				LLVMValueRef *new_values = gb_alloc_array(temporary_allocator(), LLVMValueRef, value_count);
				for (isize i = 0; i < value_count; i++) {
					LLVMValueRef old_value = old_values[i];
					if (LLVMIsConstant(old_value)) {
						new_values[i] = old_value;
					} else {
						new_values[i] = LLVMConstNull(LLVMTypeOf(old_value));
					}
				}
				LLVMValueRef constant_value = llvm_const_named_struct(lb_type(m, original_type), new_values, cast(unsigned)value_count);


				GB_ASSERT(is_local);
				lbProcedure *p = m->curr_procedure;
				lbAddr v = lb_add_local_generated(p, res.type, true);
				LLVMBuildStore(p->builder, constant_value, v.addr.value);
				for (isize i = 0; i < value_count; i++) {
					LLVMValueRef val = old_values[i];
					if (!LLVMIsConstant(val)) {
						LLVMValueRef dst = LLVMBuildStructGEP(p->builder, v.addr.value, cast(unsigned)i, "");
						LLVMBuildStore(p->builder, val, dst);
					}
				}
				return lb_addr_load(p, v);

			}
		} else if (is_type_bit_set(type)) {
			ast_node(cl, CompoundLit, value.value_compound);
			if (cl->elems.count == 0) {
				return lb_const_nil(m, original_type);
			}

			i64 sz = type_size_of(type);
			if (sz == 0) {
				return lb_const_nil(m, original_type);
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

			res.value = LLVMConstInt(lb_type(m, original_type), bits, false);
			return res;
		} else {
			return lb_const_nil(m, original_type);
		}
		break;
	case ExactValue_Procedure:
		{
			Ast *expr = value.value_procedure;
			GB_ASSERT(expr != nullptr);
			if (expr->kind == Ast_ProcLit) {
				return lb_generate_anonymous_proc_lit(m, str_lit("_proclit"), expr);
			}
		}
		break;
	case ExactValue_Typeid:
		return lb_typeid(m, value.value_typeid);
	}

	return lb_const_nil(m, original_type);
}

lbValue lb_emit_source_code_location(lbProcedure *p, String const &procedure, TokenPos const &pos) {
	lbModule *m = p->module;

	LLVMValueRef fields[4] = {};
	fields[0]/*file*/      = lb_find_or_add_entity_string(p->module, get_file_path_string(pos.file_id)).value;
	fields[1]/*line*/      = lb_const_int(m, t_i32, pos.line).value;
	fields[2]/*column*/    = lb_const_int(m, t_i32, pos.column).value;
	fields[3]/*procedure*/ = lb_find_or_add_entity_string(p->module, procedure).value;

	lbValue res = {};
	res.value = llvm_const_named_struct(lb_type(m, t_source_code_location), fields, gb_count_of(fields));
	res.type = t_source_code_location;
	return res;
}

lbValue lb_emit_source_code_location(lbProcedure *p, Ast *node) {
	String proc_name = {};
	if (p->entity) {
		proc_name = p->entity->token.string;
	}
	TokenPos pos = {};
	if (node) {
		pos = ast_token(node).pos;
	}
	return lb_emit_source_code_location(p, proc_name, pos);
}


lbValue lb_emit_unary_arith(lbProcedure *p, TokenKind op, lbValue x, Type *type) {
	switch (op) {
	case Token_Add:
		return x;
	case Token_Not: // Boolean not
	case Token_Xor: // Bitwise not
	case Token_Sub: // Number negation
		break;
	case Token_Pointer:
		GB_PANIC("This should be handled elsewhere");
		break;
	}

	if (is_type_array_like(x.type)) {
		// IMPORTANT TODO(bill): This is very wasteful with regards to stack memory
		Type *tl = base_type(x.type);
		lbValue val = lb_address_from_load_or_generate_local(p, x);
		GB_ASSERT(is_type_array_like(type));
		Type *elem_type = base_array_type(type);

		// NOTE(bill): Doesn't need to be zero because it will be initialized in the loops
		lbAddr res_addr = lb_add_local(p, type, nullptr, false, 0, true);
		lbValue res = lb_addr_get_ptr(p, res_addr);

		bool inline_array_arith = type_size_of(type) <= build_context.max_align;

		i32 count = cast(i32)get_array_type_count(tl);

		LLVMTypeRef vector_type = nullptr;
		if (op != Token_Not && lb_try_vector_cast(p->module, val, &vector_type)) {
			LLVMValueRef vp = LLVMBuildPointerCast(p->builder, val.value, LLVMPointerType(vector_type, 0), "");
			LLVMValueRef v = LLVMBuildLoad2(p->builder, vector_type, vp, "");

			LLVMValueRef opv = nullptr;
			switch (op) {
			case Token_Xor:
				opv = LLVMBuildNot(p->builder, v, "");
				break;
			case Token_Sub:
				if (is_type_float(elem_type)) {
					opv = LLVMBuildFNeg(p->builder, v, "");
				} else {
					opv = LLVMBuildNeg(p->builder, v, "");
				}
				break;
			}

			if (opv != nullptr) {
				LLVMSetAlignment(res.value, cast(unsigned)lb_alignof(vector_type));
				LLVMValueRef res_ptr = LLVMBuildPointerCast(p->builder, res.value, LLVMPointerType(vector_type, 0), "");
				LLVMBuildStore(p->builder, opv, res_ptr);
				return lb_emit_conv(p, lb_emit_load(p, res), type);
			}
		}

		if (inline_array_arith) {
			// inline
			for (i32 i = 0; i < count; i++) {
				lbValue e = lb_emit_load(p, lb_emit_array_epi(p, val, i));
				lbValue z = lb_emit_unary_arith(p, op, e, elem_type);
				lb_emit_store(p, lb_emit_array_epi(p, res, i), z);
			}
		} else {
			auto loop_data = lb_loop_start(p, count, t_i32);

			lbValue e = lb_emit_load(p, lb_emit_array_ep(p, val, loop_data.idx));
			lbValue z = lb_emit_unary_arith(p, op, e, elem_type);
			lb_emit_store(p, lb_emit_array_ep(p, res, loop_data.idx), z);

			lb_loop_end(p, loop_data);
		}
		return lb_emit_load(p, res);

	}

	if (op == Token_Xor) {
		lbValue cmp = {};
		cmp.value = LLVMBuildNot(p->builder, x.value, "");
		cmp.type = x.type;
		return lb_emit_conv(p, cmp, type);
	}

	if (op == Token_Not) {
		lbValue cmp = {};
		LLVMValueRef zero =  LLVMConstInt(lb_type(p->module, x.type), 0, false);
		cmp.value = LLVMBuildICmp(p->builder, LLVMIntEQ, x.value, zero, "");
		cmp.type = t_llvm_bool;
		return lb_emit_conv(p, cmp, type);
	}

	if (op == Token_Sub && is_type_integer(type) && is_type_different_to_arch_endianness(type)) {
		Type *platform_type = integer_endian_type_to_platform_type(type);
		lbValue v = lb_emit_byte_swap(p, x, platform_type);

		lbValue res = {};
		res.value = LLVMBuildNeg(p->builder, v.value, "");
		res.type = platform_type;

		return lb_emit_byte_swap(p, res, type);
	}

	if (op == Token_Sub && is_type_float(type) && is_type_different_to_arch_endianness(type)) {
		Type *platform_type = integer_endian_type_to_platform_type(type);
		lbValue v = lb_emit_byte_swap(p, x, platform_type);

		lbValue res = {};
		res.value = LLVMBuildFNeg(p->builder, v.value, "");
		res.type = platform_type;

		return lb_emit_byte_swap(p, res, type);
	}

	lbValue res = {};

	switch (op) {
	case Token_Not: // Boolean not
	case Token_Xor: // Bitwise not
		res.value = LLVMBuildNot(p->builder, x.value, "");
		res.type = x.type;
		return res;
	case Token_Sub: // Number negation
		if (is_type_integer(x.type)) {
			res.value = LLVMBuildNeg(p->builder, x.value, "");
		} else if (is_type_float(x.type)) {
			res.value = LLVMBuildFNeg(p->builder, x.value, "");
		} else if (is_type_complex(x.type)) {
			LLVMValueRef v0 = LLVMBuildFNeg(p->builder, LLVMBuildExtractValue(p->builder, x.value, 0, ""), "");
			LLVMValueRef v1 = LLVMBuildFNeg(p->builder, LLVMBuildExtractValue(p->builder, x.value, 1, ""), "");

			lbAddr addr = lb_add_local_generated(p, x.type, false);
			LLVMBuildStore(p->builder, v0, LLVMBuildStructGEP(p->builder, addr.addr.value, 0, ""));
			LLVMBuildStore(p->builder, v1, LLVMBuildStructGEP(p->builder, addr.addr.value, 1, ""));
			return lb_addr_load(p, addr);

		} else if (is_type_quaternion(x.type)) {
			LLVMValueRef v0 = LLVMBuildFNeg(p->builder, LLVMBuildExtractValue(p->builder, x.value, 0, ""), "");
			LLVMValueRef v1 = LLVMBuildFNeg(p->builder, LLVMBuildExtractValue(p->builder, x.value, 1, ""), "");
			LLVMValueRef v2 = LLVMBuildFNeg(p->builder, LLVMBuildExtractValue(p->builder, x.value, 2, ""), "");
			LLVMValueRef v3 = LLVMBuildFNeg(p->builder, LLVMBuildExtractValue(p->builder, x.value, 3, ""), "");

			lbAddr addr = lb_add_local_generated(p, x.type, false);
			LLVMBuildStore(p->builder, v0, LLVMBuildStructGEP(p->builder, addr.addr.value, 0, ""));
			LLVMBuildStore(p->builder, v1, LLVMBuildStructGEP(p->builder, addr.addr.value, 1, ""));
			LLVMBuildStore(p->builder, v2, LLVMBuildStructGEP(p->builder, addr.addr.value, 2, ""));
			LLVMBuildStore(p->builder, v3, LLVMBuildStructGEP(p->builder, addr.addr.value, 3, ""));
			return lb_addr_load(p, addr);

		} else {
			GB_PANIC("Unhandled type %s", type_to_string(x.type));
		}
		res.type = x.type;
		return res;
	}

	return res;
}

bool lb_try_direct_vector_arith(lbProcedure *p, TokenKind op, lbValue lhs, lbValue rhs, Type *type, lbValue *res_) {
	GB_ASSERT(is_type_array_like(type));
	Type *elem_type = base_array_type(type);

	// NOTE(bill): Shift operations cannot be easily dealt with due to Odin's semantics
	if (op == Token_Shl || op == Token_Shr) {
		return false;
	}

	if (!LLVMIsALoadInst(lhs.value) || !LLVMIsALoadInst(rhs.value)) {
		return false;
	}

	lbValue lhs_ptr = {};
	lbValue rhs_ptr = {};
	lhs_ptr.value = LLVMGetOperand(lhs.value, 0);
	lhs_ptr.type = alloc_type_pointer(lhs.type);
	rhs_ptr.value = LLVMGetOperand(rhs.value, 0);
	rhs_ptr.type = alloc_type_pointer(rhs.type);

	LLVMTypeRef vector_type0 = nullptr;
	LLVMTypeRef vector_type1 = nullptr;
	if (lb_try_vector_cast(p->module, lhs_ptr, &vector_type0) &&
	    lb_try_vector_cast(p->module, rhs_ptr, &vector_type1)) {
		GB_ASSERT(vector_type0 == vector_type1);
		LLVMTypeRef vector_type = vector_type0;

		LLVMValueRef lhs_vp = LLVMBuildPointerCast(p->builder, lhs_ptr.value, LLVMPointerType(vector_type, 0), "");
		LLVMValueRef rhs_vp = LLVMBuildPointerCast(p->builder, rhs_ptr.value, LLVMPointerType(vector_type, 0), "");
		LLVMValueRef x = LLVMBuildLoad2(p->builder, vector_type, lhs_vp, "");
		LLVMValueRef y = LLVMBuildLoad2(p->builder, vector_type, rhs_vp, "");
		LLVMValueRef z = nullptr;

		Type *integral_type = base_type(elem_type);
		if (is_type_simd_vector(integral_type)) {
			integral_type = core_array_type(integral_type);
		}
		if (is_type_bit_set(integral_type)) {
			switch (op) {
			case Token_Add: op = Token_Or;     break;
			case Token_Sub: op = Token_AndNot; break;
			}
		}

		if (is_type_float(integral_type)) {
			switch (op) {
			case Token_Add:
				z = LLVMBuildFAdd(p->builder, x, y, "");
				break;
			case Token_Sub:
				z = LLVMBuildFSub(p->builder, x, y, "");
				break;
			case Token_Mul:
				z = LLVMBuildFMul(p->builder, x, y, "");
				break;
			case Token_Quo:
				z = LLVMBuildFDiv(p->builder, x, y, "");
				break;
			case Token_Mod:
				z = LLVMBuildFRem(p->builder, x, y, "");
				break;
			default:
				GB_PANIC("Unsupported vector operation");
				break;
			}

		} else {

			switch (op) {
			case Token_Add:
				z = LLVMBuildAdd(p->builder, x, y, "");
				break;
			case Token_Sub:
				z = LLVMBuildSub(p->builder, x, y, "");
				break;
			case Token_Mul:
				z = LLVMBuildMul(p->builder, x, y, "");
				break;
			case Token_Quo:
				if (is_type_unsigned(integral_type)) {
					z = LLVMBuildUDiv(p->builder, x, y, "");
				} else {
					z = LLVMBuildSDiv(p->builder, x, y, "");
				}
				break;
			case Token_Mod:
				if (is_type_unsigned(integral_type)) {
					z = LLVMBuildURem(p->builder, x, y, "");
				} else {
					z = LLVMBuildSRem(p->builder, x, y, "");
				}
				break;
			case Token_ModMod:
				if (is_type_unsigned(integral_type)) {
					z = LLVMBuildURem(p->builder, x, y, "");
				} else {
					LLVMValueRef a = LLVMBuildSRem(p->builder, x, y, "");
					LLVMValueRef b = LLVMBuildAdd(p->builder, a, y, "");
					z = LLVMBuildSRem(p->builder, b, y, "");
				}
				break;
			case Token_And:
				z = LLVMBuildAnd(p->builder, x, y, "");
				break;
			case Token_AndNot:
				z = LLVMBuildAnd(p->builder, x, LLVMBuildNot(p->builder, y, ""), "");
				break;
			case Token_Or:
				z = LLVMBuildOr(p->builder, x, y, "");
				break;
			case Token_Xor:
				z = LLVMBuildXor(p->builder, x, y, "");
				break;
			default:
				GB_PANIC("Unsupported vector operation");
				break;
			}
		}


		if (z != nullptr) {
			lbAddr res = lb_add_local_generated_temp(p, type, lb_alignof(vector_type));

			LLVMValueRef vp = LLVMBuildPointerCast(p->builder, res.addr.value, LLVMPointerType(vector_type, 0), "");
			LLVMBuildStore(p->builder, z, vp);
			lbValue v = lb_addr_load(p, res);
			if (res_) *res_ = v;
			return true;
		}
	}

	return false;
}


lbValue lb_emit_arith_array(lbProcedure *p, TokenKind op, lbValue lhs, lbValue rhs, Type *type) {
	GB_ASSERT(is_type_array_like(lhs.type) || is_type_array_like(rhs.type));

	lhs = lb_emit_conv(p, lhs, type);
	rhs = lb_emit_conv(p, rhs, type);

	GB_ASSERT(is_type_array_like(type));
	Type *elem_type = base_array_type(type);

	i64 count = get_array_type_count(type);
	unsigned n = cast(unsigned)count;

	// NOTE(bill, 2021-06-12): Try to do a direct operation as a vector, if possible
	lbValue direct_vector_res = {};
	if (lb_try_direct_vector_arith(p, op, lhs, rhs, type, &direct_vector_res)) {
		return direct_vector_res;
	}

	bool inline_array_arith = type_size_of(type) <= build_context.max_align;
	if (inline_array_arith) {

		auto dst_ptrs = slice_make<lbValue>(temporary_allocator(), n);

		auto a_loads = slice_make<lbValue>(temporary_allocator(), n);
		auto b_loads = slice_make<lbValue>(temporary_allocator(), n);
		auto c_ops = slice_make<lbValue>(temporary_allocator(), n);

		for (unsigned i = 0; i < n; i++) {
			a_loads[i].value = LLVMBuildExtractValue(p->builder, lhs.value, i, "");
			a_loads[i].type = elem_type;
		}
		for (unsigned i = 0; i < n; i++) {
			b_loads[i].value = LLVMBuildExtractValue(p->builder, rhs.value, i, "");
			b_loads[i].type = elem_type;
		}
		for (unsigned i = 0; i < n; i++) {
			c_ops[i] = lb_emit_arith(p, op, a_loads[i], b_loads[i], elem_type);
		}

		lbAddr res = lb_add_local_generated(p, type, false);
		for (unsigned i = 0; i < n; i++) {
			dst_ptrs[i] = lb_emit_array_epi(p, res.addr, i);
		}
		for (unsigned i = 0; i < n; i++) {
			lb_emit_store(p, dst_ptrs[i], c_ops[i]);
		}


		return lb_addr_load(p, res);
	} else {
		lbValue x = lb_address_from_load_or_generate_local(p, lhs);
		lbValue y = lb_address_from_load_or_generate_local(p, rhs);

		lbAddr res = lb_add_local_generated(p, type, false);

		auto loop_data = lb_loop_start(p, count, t_i32);

		lbValue a_ptr = lb_emit_array_ep(p, x, loop_data.idx);
		lbValue b_ptr = lb_emit_array_ep(p, y, loop_data.idx);
		lbValue dst_ptr = lb_emit_array_ep(p, res.addr, loop_data.idx);

		lbValue a = lb_emit_load(p, a_ptr);
		lbValue b = lb_emit_load(p, b_ptr);
		lbValue c = lb_emit_arith(p, op, a, b, elem_type);
		lb_emit_store(p, dst_ptr, c);

		lb_loop_end(p, loop_data);

		return lb_addr_load(p, res);
	}
}



lbValue lb_emit_arith(lbProcedure *p, TokenKind op, lbValue lhs, lbValue rhs, Type *type) {
	lbModule *m = p->module;

	if (is_type_array_like(lhs.type) || is_type_array_like(rhs.type)) {
		return lb_emit_arith_array(p, op, lhs, rhs, type);
	} else if (is_type_complex(type)) {
		lhs = lb_emit_conv(p, lhs, type);
		rhs = lb_emit_conv(p, rhs, type);

		Type *ft = base_complex_elem_type(type);

		if (op == Token_Quo) {
			auto args = array_make<lbValue>(permanent_allocator(), 2);
			args[0] = lhs;
			args[1] = rhs;

			switch (type_size_of(ft)) {
			case 4: return lb_emit_runtime_call(p, "quo_complex64", args);
			case 8: return lb_emit_runtime_call(p, "quo_complex128", args);
			default: GB_PANIC("Unknown float type"); break;
			}
		}

		lbAddr res = lb_add_local_generated(p, type, false); // NOTE: initialized in full later
		lbValue a = lb_emit_struct_ev(p, lhs, 0);
		lbValue b = lb_emit_struct_ev(p, lhs, 1);
		lbValue c = lb_emit_struct_ev(p, rhs, 0);
		lbValue d = lb_emit_struct_ev(p, rhs, 1);

		lbValue real = {};
		lbValue imag = {};

		switch (op) {
		case Token_Add:
			real = lb_emit_arith(p, Token_Add, a, c, ft);
			imag = lb_emit_arith(p, Token_Add, b, d, ft);
			break;
		case Token_Sub:
			real = lb_emit_arith(p, Token_Sub, a, c, ft);
			imag = lb_emit_arith(p, Token_Sub, b, d, ft);
			break;
		case Token_Mul: {
			lbValue x = lb_emit_arith(p, Token_Mul, a, c, ft);
			lbValue y = lb_emit_arith(p, Token_Mul, b, d, ft);
			real = lb_emit_arith(p, Token_Sub, x, y, ft);
			lbValue z = lb_emit_arith(p, Token_Mul, b, c, ft);
			lbValue w = lb_emit_arith(p, Token_Mul, a, d, ft);
			imag = lb_emit_arith(p, Token_Add, z, w, ft);
			break;
		}
		}

		lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 0), real);
		lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 1), imag);

		return lb_addr_load(p, res);
	} else if (is_type_quaternion(type)) {
		lhs = lb_emit_conv(p, lhs, type);
		rhs = lb_emit_conv(p, rhs, type);

		Type *ft = base_complex_elem_type(type);

		if (op == Token_Add || op == Token_Sub) {
			lbAddr res = lb_add_local_generated(p, type, false); // NOTE: initialized in full later
			lbValue x0 = lb_emit_struct_ev(p, lhs, 0);
			lbValue x1 = lb_emit_struct_ev(p, lhs, 1);
			lbValue x2 = lb_emit_struct_ev(p, lhs, 2);
			lbValue x3 = lb_emit_struct_ev(p, lhs, 3);

			lbValue y0 = lb_emit_struct_ev(p, rhs, 0);
			lbValue y1 = lb_emit_struct_ev(p, rhs, 1);
			lbValue y2 = lb_emit_struct_ev(p, rhs, 2);
			lbValue y3 = lb_emit_struct_ev(p, rhs, 3);

			lbValue z0 = lb_emit_arith(p, op, x0, y0, ft);
			lbValue z1 = lb_emit_arith(p, op, x1, y1, ft);
			lbValue z2 = lb_emit_arith(p, op, x2, y2, ft);
			lbValue z3 = lb_emit_arith(p, op, x3, y3, ft);

			lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 0), z0);
			lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 1), z1);
			lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 2), z2);
			lb_emit_store(p, lb_emit_struct_ep(p, res.addr, 3), z3);

			return lb_addr_load(p, res);
		} else if (op == Token_Mul) {
			auto args = array_make<lbValue>(permanent_allocator(), 2);
			args[0] = lhs;
			args[1] = rhs;

			switch (8*type_size_of(ft)) {
			case 32: return lb_emit_runtime_call(p, "mul_quaternion128", args);
			case 64: return lb_emit_runtime_call(p, "mul_quaternion256", args);
			default: GB_PANIC("Unknown float type"); break;
			}
		} else if (op == Token_Quo) {
			auto args = array_make<lbValue>(permanent_allocator(), 2);
			args[0] = lhs;
			args[1] = rhs;

			switch (8*type_size_of(ft)) {
			case 32: return lb_emit_runtime_call(p, "quo_quaternion128", args);
			case 64: return lb_emit_runtime_call(p, "quo_quaternion256", args);
			default: GB_PANIC("Unknown float type"); break;
			}
		}
	}

	if (is_type_integer(type) && is_type_different_to_arch_endianness(type)) {
		switch (op) {
		case Token_AndNot:
		case Token_And:
		case Token_Or:
		case Token_Xor:
			goto handle_op;
		}

		Type *platform_type = integer_endian_type_to_platform_type(type);
		lbValue x = lb_emit_byte_swap(p, lhs, integer_endian_type_to_platform_type(lhs.type));
		lbValue y = lb_emit_byte_swap(p, rhs, integer_endian_type_to_platform_type(rhs.type));

		lbValue res = lb_emit_arith(p, op, x, y, platform_type);

		return lb_emit_byte_swap(p, res, type);
	}

	if (is_type_float(type) && is_type_different_to_arch_endianness(type)) {
		Type *platform_type = integer_endian_type_to_platform_type(type);
		lbValue x = lb_emit_conv(p, lhs, integer_endian_type_to_platform_type(lhs.type));
		lbValue y = lb_emit_conv(p, rhs, integer_endian_type_to_platform_type(rhs.type));

		lbValue res = lb_emit_arith(p, op, x, y, platform_type);

		return lb_emit_byte_swap(p, res, type);
	}



handle_op:
	lhs = lb_emit_conv(p, lhs, type);
	rhs = lb_emit_conv(p, rhs, type);

	lbValue res = {};
	res.type = type;

	// NOTE(bill): Bit Set Aliases for + and -
	if (is_type_bit_set(type)) {
		switch (op) {
		case Token_Add: op = Token_Or;     break;
		case Token_Sub: op = Token_AndNot; break;
		}
	}

	Type *integral_type = type;
	if (is_type_simd_vector(integral_type)) {
		integral_type = core_array_type(integral_type);
	}

	switch (op) {
	case Token_Add:
		if (is_type_float(integral_type)) {
			res.value = LLVMBuildFAdd(p->builder, lhs.value, rhs.value, "");
			return res;
		}
		res.value = LLVMBuildAdd(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_Sub:
		if (is_type_float(integral_type)) {
			res.value = LLVMBuildFSub(p->builder, lhs.value, rhs.value, "");
			return res;
		}
		res.value = LLVMBuildSub(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_Mul:
		if (is_type_float(integral_type)) {
			res.value = LLVMBuildFMul(p->builder, lhs.value, rhs.value, "");
			return res;
		}
		res.value = LLVMBuildMul(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_Quo:
		if (is_type_float(integral_type)) {
			res.value = LLVMBuildFDiv(p->builder, lhs.value, rhs.value, "");
			return res;
		} else if (is_type_unsigned(integral_type)) {
			res.value = LLVMBuildUDiv(p->builder, lhs.value, rhs.value, "");
			return res;
		}
		res.value = LLVMBuildSDiv(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_Mod:
		if (is_type_float(integral_type)) {
			res.value = LLVMBuildFRem(p->builder, lhs.value, rhs.value, "");
			return res;
		} else if (is_type_unsigned(integral_type)) {
			res.value = LLVMBuildURem(p->builder, lhs.value, rhs.value, "");
			return res;
		}
		res.value = LLVMBuildSRem(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_ModMod:
		if (is_type_unsigned(integral_type)) {
			res.value = LLVMBuildURem(p->builder, lhs.value, rhs.value, "");
			return res;
		} else {
			LLVMValueRef a = LLVMBuildSRem(p->builder, lhs.value, rhs.value, "");
			LLVMValueRef b = LLVMBuildAdd(p->builder, a, rhs.value, "");
			LLVMValueRef c = LLVMBuildSRem(p->builder, b, rhs.value, "");
			res.value = c;
			return res;
		}

	case Token_And:
		res.value = LLVMBuildAnd(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_Or:
		res.value = LLVMBuildOr(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_Xor:
		res.value = LLVMBuildXor(p->builder, lhs.value, rhs.value, "");
		return res;
	case Token_Shl:
		{
			rhs = lb_emit_conv(p, rhs, lhs.type);
			LLVMValueRef lhsval = lhs.value;
			LLVMValueRef bits = rhs.value;

			LLVMValueRef bit_size = LLVMConstInt(lb_type(p->module, rhs.type), 8*type_size_of(lhs.type), false);

			LLVMValueRef width_test = LLVMBuildICmp(p->builder, LLVMIntULT, bits, bit_size, "");

			res.value = LLVMBuildShl(p->builder, lhsval, bits, "");
			LLVMValueRef zero = LLVMConstNull(lb_type(p->module, lhs.type));
			res.value = LLVMBuildSelect(p->builder, width_test, res.value, zero, "");
			return res;
		}
	case Token_Shr:
		{
			rhs = lb_emit_conv(p, rhs, lhs.type);
			LLVMValueRef lhsval = lhs.value;
			LLVMValueRef bits = rhs.value;
			bool is_unsigned = is_type_unsigned(integral_type);

			LLVMValueRef bit_size = LLVMConstInt(lb_type(p->module, rhs.type), 8*type_size_of(lhs.type), false);

			LLVMValueRef width_test = LLVMBuildICmp(p->builder, LLVMIntULT, bits, bit_size, "");

			if (is_unsigned) {
				res.value = LLVMBuildLShr(p->builder, lhsval, bits, "");
			} else {
				res.value = LLVMBuildAShr(p->builder, lhsval, bits, "");
			}

			LLVMValueRef zero = LLVMConstNull(lb_type(p->module, lhs.type));
			res.value = LLVMBuildSelect(p->builder, width_test, res.value, zero, "");
			return res;
		}
	case Token_AndNot:
		{
			LLVMValueRef new_rhs = LLVMBuildNot(p->builder, rhs.value, "");
			res.value = LLVMBuildAnd(p->builder, lhs.value, new_rhs, "");
			return res;
		}
		break;
	}

	GB_PANIC("unhandled operator of lb_emit_arith");

	return {};
}

lbValue lb_build_binary_expr(lbProcedure *p, Ast *expr) {
	ast_node(be, BinaryExpr, expr);

	TypeAndValue tv = type_and_value_of_expr(expr);

	switch (be->op.kind) {
	case Token_Add:
	case Token_Sub:
	case Token_Mul:
	case Token_Quo:
	case Token_Mod:
	case Token_ModMod:
	case Token_And:
	case Token_Or:
	case Token_Xor:
	case Token_AndNot: {
		Type *type = default_type(tv.type);
		lbValue left = lb_build_expr(p, be->left);
		lbValue right = lb_build_expr(p, be->right);
		return lb_emit_arith(p, be->op.kind, left, right, type);
	}

	case Token_Shl:
	case Token_Shr: {
		lbValue left, right;
		Type *type = default_type(tv.type);
		left = lb_build_expr(p, be->left);

		if (lb_is_expr_untyped_const(be->right)) {
			// NOTE(bill): RHS shift operands can still be untyped
			// Just bypass the standard lb_build_expr
			right = lb_expr_untyped_const_to_typed(p->module, be->right, type);
		} else {
			right = lb_build_expr(p, be->right);
		}
		return lb_emit_arith(p, be->op.kind, left, right, type);
	}

	case Token_CmpEq:
	case Token_NotEq:
	case Token_Lt:
	case Token_LtEq:
	case Token_Gt:
	case Token_GtEq:
		{
			lbValue left = {};
			lbValue right = {};

			if (be->left->tav.mode == Addressing_Type) {
				left = lb_typeid(p->module, be->left->tav.type);
			}
			if (be->right->tav.mode == Addressing_Type) {
				right = lb_typeid(p->module, be->right->tav.type);
			}
			if (left.value == nullptr)  left  = lb_build_expr(p, be->left);
			if (right.value == nullptr) right = lb_build_expr(p, be->right);
			lbValue cmp = lb_emit_comp(p, be->op.kind, left, right);
			Type *type = default_type(tv.type);
			return lb_emit_conv(p, cmp, type);
		}

	case Token_CmpAnd:
	case Token_CmpOr:
		return lb_emit_logical_binary_expr(p, be->op.kind, be->left, be->right, tv.type);

	case Token_in:
	case Token_not_in:
		{
			lbValue left = lb_build_expr(p, be->left);
			Type *type = default_type(tv.type);
			lbValue right = lb_build_expr(p, be->right);
			Type *rt = base_type(right.type);
			if (is_type_pointer(rt)) {
				right = lb_emit_load(p, right);
				rt = type_deref(rt);
			}

			switch (rt->kind) {
			case Type_Map:
				{
					lbValue addr = lb_address_from_load_or_generate_local(p, right);
					lbValue h = lb_gen_map_header(p, addr, rt);
					lbValue key = lb_gen_map_hash(p, left, rt->Map.key);

					auto args = array_make<lbValue>(permanent_allocator(), 2);
					args[0] = h;
					args[1] = key;

					lbValue ptr = lb_emit_runtime_call(p, "__dynamic_map_get", args);
					if (be->op.kind == Token_in) {
						return lb_emit_conv(p, lb_emit_comp_against_nil(p, Token_NotEq, ptr), t_bool);
					} else {
						return lb_emit_conv(p, lb_emit_comp_against_nil(p, Token_CmpEq, ptr), t_bool);
					}
				}
				break;
			case Type_BitSet:
				{
					Type *key_type = rt->BitSet.elem;
					GB_ASSERT(are_types_identical(left.type, key_type));

					Type *it = bit_set_to_int(rt);
					left = lb_emit_conv(p, left, it);

					lbValue lower = lb_const_value(p->module, it, exact_value_i64(rt->BitSet.lower));
					lbValue key = lb_emit_arith(p, Token_Sub, left, lower, it);
					lbValue bit = lb_emit_arith(p, Token_Shl, lb_const_int(p->module, it, 1), key, it);
					bit = lb_emit_conv(p, bit, it);

					lbValue old_value = lb_emit_transmute(p, right, it);
					lbValue new_value = lb_emit_arith(p, Token_And, old_value, bit, it);

					if (be->op.kind == Token_in) {
						return lb_emit_conv(p, lb_emit_comp(p, Token_NotEq, new_value, lb_const_int(p->module, new_value.type, 0)), t_bool);
					} else {
						return lb_emit_conv(p, lb_emit_comp(p, Token_CmpEq, new_value, lb_const_int(p->module, new_value.type, 0)), t_bool);
					}
				}
				break;
			default:
				GB_PANIC("Invalid 'in' type");
			}
			break;
		}
		break;
	default:
		GB_PANIC("Invalid binary expression");
		break;
	}
	return {};
}


String lookup_subtype_polymorphic_field(CheckerInfo *info, Type *dst, Type *src) {
	Type *prev_src = src;
	// Type *prev_dst = dst;
	src = base_type(type_deref(src));
	// dst = base_type(type_deref(dst));
	bool src_is_ptr = src != prev_src;
	// bool dst_is_ptr = dst != prev_dst;

	GB_ASSERT(is_type_struct(src) || is_type_union(src));
	for_array(i, src->Struct.fields) {
		Entity *f = src->Struct.fields[i];
		if (f->kind == Entity_Variable && f->flags & EntityFlag_Using) {
			if (are_types_identical(dst, f->type)) {
				return f->token.string;
			}
			if (src_is_ptr && is_type_pointer(dst)) {
				if (are_types_identical(type_deref(dst), f->type)) {
					return f->token.string;
				}
			}
			if (is_type_struct(f->type)) {
				String name = lookup_subtype_polymorphic_field(info, dst, f->type);
				if (name.len > 0) {
					return name;
				}
			}
		}
	}
	return str_lit("");
}

lbValue lb_const_ptr_cast(lbModule *m, lbValue value, Type *t) {
	GB_ASSERT(is_type_pointer(value.type));
	GB_ASSERT(is_type_pointer(t));
	GB_ASSERT(lb_is_const(value));

	lbValue res = {};
	res.value = LLVMConstPointerCast(value.value, lb_type(m, t));
	res.type = t;
	return res;
}

lbValue lb_emit_conv(lbProcedure *p, lbValue value, Type *t) {
	lbModule *m = p->module;
	t = reduce_tuple_to_single_type(t);

	Type *src_type = value.type;
	if (are_types_identical(t, src_type)) {
		return value;
	}

	Type *src = core_type(src_type);
	Type *dst = core_type(t);
	GB_ASSERT(src != nullptr);
	GB_ASSERT(dst != nullptr);

	if (is_type_untyped_nil(src)) {
		return lb_const_nil(m, t);
	}
	if (is_type_untyped_undef(src)) {
		return lb_const_undef(m, t);
	}

	if (LLVMIsConstant(value.value)) {
		if (is_type_any(dst)) {
			Type *st = default_type(src_type);
			lbAddr default_value = lb_add_local_generated(p, st, false);
			lb_addr_store(p, default_value, value);
			lbValue data = lb_emit_conv(p, default_value.addr, t_rawptr);
			lbValue id = lb_typeid(m, st);

			lbAddr res = lb_add_local_generated(p, t, false);
			lbValue a0 = lb_emit_struct_ep(p, res.addr, 0);
			lbValue a1 = lb_emit_struct_ep(p, res.addr, 1);
			lb_emit_store(p, a0, data);
			lb_emit_store(p, a1, id);
			return lb_addr_load(p, res);
		} else if (dst->kind == Type_Basic) {
			if (src->Basic.kind == Basic_string && dst->Basic.kind == Basic_cstring) {
				String str = lb_get_const_string(m, value);
				lbValue res = {};
				res.type = t;
				res.value = llvm_cstring(m, str);
				return res;
			}
			// if (is_type_float(dst)) {
			// 	return value;
			// } else if (is_type_integer(dst)) {
			// 	return value;
			// }
			// ExactValue ev = value->Constant.value;
			// if (is_type_float(dst)) {
			// 	ev = exact_value_to_float(ev);
			// } else if (is_type_complex(dst)) {
			// 	ev = exact_value_to_complex(ev);
			// } else if (is_type_quaternion(dst)) {
			// 	ev = exact_value_to_quaternion(ev);
			// } else if (is_type_string(dst)) {
			// 	// Handled elsewhere
			// 	GB_ASSERT_MSG(ev.kind == ExactValue_String, "%d", ev.kind);
			// } else if (is_type_integer(dst)) {
			// 	ev = exact_value_to_integer(ev);
			// } else if (is_type_pointer(dst)) {
			// 	// IMPORTANT NOTE(bill): LLVM doesn't support pointer constants expect 'null'
			// 	lbValue i = lb_add_module_constant(p->module, t_uintptr, ev);
			// 	return lb_emit(p, lb_instr_conv(p, irConv_inttoptr, i, t_uintptr, dst));
			// }
			// return lb_const_value(p->module, t, ev);
		}
	}

	if (are_types_identical(src, dst)) {
		if (!are_types_identical(src_type, t)) {
			return lb_emit_transmute(p, value, t);
		}
		return value;
	}



	// bool <-> llvm bool
	if (is_type_boolean(src) && dst == t_llvm_bool) {
		lbValue res = {};
		res.value = LLVMBuildTrunc(p->builder, value.value, lb_type(m, dst), "");
		res.type = dst;
		return res;
	}
	if (src == t_llvm_bool && is_type_boolean(dst)) {
		lbValue res = {};
		res.value = LLVMBuildZExt(p->builder, value.value, lb_type(m, dst), "");
		res.type = dst;
		return res;
	}


	// integer -> integer
	if (is_type_integer(src) && is_type_integer(dst)) {
		GB_ASSERT(src->kind == Type_Basic &&
		          dst->kind == Type_Basic);
		i64 sz = type_size_of(default_type(src));
		i64 dz = type_size_of(default_type(dst));


		if (sz == dz) {
			if (dz > 1 && !types_have_same_internal_endian(src, dst)) {
				return lb_emit_byte_swap(p, value, t);
			}
			lbValue res = {};
			res.value = value.value;
			res.type = t;
			return res;
		}

		if (sz > 1 && is_type_different_to_arch_endianness(src)) {
			Type *platform_src_type = integer_endian_type_to_platform_type(src);
			value = lb_emit_byte_swap(p, value, platform_src_type);
		}
		LLVMOpcode op = LLVMTrunc;

		if (dz < sz) {
			op = LLVMTrunc;
		} else if (dz == sz) {
			// NOTE(bill): In LLVM, all integers are signed and rely upon 2's compliment
			// NOTE(bill): Copy the value just for type correctness
			op = LLVMBitCast;
		} else if (dz > sz) {
			op = is_type_unsigned(src) ? LLVMZExt : LLVMSExt; // zero extent
		}

		if (dz > 1 && is_type_different_to_arch_endianness(dst)) {
			Type *platform_dst_type = integer_endian_type_to_platform_type(dst);
			lbValue res = {};
			res.value = LLVMBuildCast(p->builder, op, value.value, lb_type(m, platform_dst_type), "");
			res.type = t;
			return lb_emit_byte_swap(p, res, t);
		} else {
			lbValue res = {};
			res.value = LLVMBuildCast(p->builder, op, value.value, lb_type(m, t), "");
			res.type = t;
			return res;
		}
	}


	// boolean -> boolean/integer
	if (is_type_boolean(src) && (is_type_boolean(dst) || is_type_integer(dst))) {
		LLVMValueRef b = LLVMBuildICmp(p->builder, LLVMIntNE, value.value, LLVMConstNull(lb_type(m, value.type)), "");
		lbValue res = {};
		res.value = LLVMBuildIntCast2(p->builder, value.value, lb_type(m, t), false, "");
		res.type = t;
		return res;
	}

	if (is_type_cstring(src) && is_type_u8_ptr(dst)) {
		return lb_emit_transmute(p, value, dst);
	}
	if (is_type_u8_ptr(src) && is_type_cstring(dst)) {
		return lb_emit_transmute(p, value, dst);
	}
	if (is_type_cstring(src) && is_type_rawptr(dst)) {
		return lb_emit_transmute(p, value, dst);
	}
	if (is_type_rawptr(src) && is_type_cstring(dst)) {
		return lb_emit_transmute(p, value, dst);
	}

	if (are_types_identical(src, t_cstring) && are_types_identical(dst, t_string)) {
		lbValue c = lb_emit_conv(p, value, t_cstring);
		auto args = array_make<lbValue>(permanent_allocator(), 1);
		args[0] = c;
		lbValue s = lb_emit_runtime_call(p, "cstring_to_string", args);
		return lb_emit_conv(p, s, dst);
	}


	// integer -> boolean
	if (is_type_integer(src) && is_type_boolean(dst)) {
		lbValue res = {};
		res.value = LLVMBuildICmp(p->builder, LLVMIntNE, value.value, LLVMConstNull(lb_type(m, value.type)), "");
		res.type = t_llvm_bool;
		return lb_emit_conv(p, res, t);
	}

	// float -> float
	if (is_type_float(src) && is_type_float(dst)) {
		i64 sz = type_size_of(src);
		i64 dz = type_size_of(dst);


		if (dz == sz) {
			if (types_have_same_internal_endian(src, dst)) {
				lbValue res = {};
				res.type = t;
				res.value = value.value;
				return res;
			} else {
				return lb_emit_byte_swap(p, value, t);
			}
		}

		if (is_type_different_to_arch_endianness(src) || is_type_different_to_arch_endianness(dst)) {
			Type *platform_src_type = integer_endian_type_to_platform_type(src);
			Type *platform_dst_type = integer_endian_type_to_platform_type(dst);
			lbValue res = {};
			res = lb_emit_conv(p, value, platform_src_type);
			res = lb_emit_conv(p, res, platform_dst_type);
			if (is_type_different_to_arch_endianness(dst)) {
				res = lb_emit_byte_swap(p, res, t);
			}
			return lb_emit_conv(p, res, t);
		}


		lbValue res = {};
		res.type = t;

		if (dz >= sz) {
			res.value = LLVMBuildFPExt(p->builder, value.value, lb_type(m, t), "");
		} else {
			res.value = LLVMBuildFPTrunc(p->builder, value.value, lb_type(m, t), "");
		}
		return res;
	}

	if (is_type_complex(src) && is_type_complex(dst)) {
		Type *ft = base_complex_elem_type(dst);
		lbAddr gen = lb_add_local_generated(p, dst, false);
		lbValue gp = lb_addr_get_ptr(p, gen);
		lbValue real = lb_emit_conv(p, lb_emit_struct_ev(p, value, 0), ft);
		lbValue imag = lb_emit_conv(p, lb_emit_struct_ev(p, value, 1), ft);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 0), real);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 1), imag);
		return lb_addr_load(p, gen);
	}

	if (is_type_quaternion(src) && is_type_quaternion(dst)) {
		// @QuaternionLayout
		Type *ft = base_complex_elem_type(dst);
		lbAddr gen = lb_add_local_generated(p, dst, false);
		lbValue gp = lb_addr_get_ptr(p, gen);
		lbValue q0 = lb_emit_conv(p, lb_emit_struct_ev(p, value, 0), ft);
		lbValue q1 = lb_emit_conv(p, lb_emit_struct_ev(p, value, 1), ft);
		lbValue q2 = lb_emit_conv(p, lb_emit_struct_ev(p, value, 2), ft);
		lbValue q3 = lb_emit_conv(p, lb_emit_struct_ev(p, value, 3), ft);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 0), q0);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 1), q1);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 2), q2);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 3), q3);
		return lb_addr_load(p, gen);
	}

	if (is_type_float(src) && is_type_complex(dst)) {
		Type *ft = base_complex_elem_type(dst);
		lbAddr gen = lb_add_local_generated(p, dst, true);
		lbValue gp = lb_addr_get_ptr(p, gen);
		lbValue real = lb_emit_conv(p, value, ft);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 0), real);
		return lb_addr_load(p, gen);
	}
	if (is_type_float(src) && is_type_quaternion(dst)) {
		Type *ft = base_complex_elem_type(dst);
		lbAddr gen = lb_add_local_generated(p, dst, true);
		lbValue gp = lb_addr_get_ptr(p, gen);
		lbValue real = lb_emit_conv(p, value, ft);
		// @QuaternionLayout
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 3), real);
		return lb_addr_load(p, gen);
	}
	if (is_type_complex(src) && is_type_quaternion(dst)) {
		Type *ft = base_complex_elem_type(dst);
		lbAddr gen = lb_add_local_generated(p, dst, true);
		lbValue gp = lb_addr_get_ptr(p, gen);
		lbValue real = lb_emit_conv(p, lb_emit_struct_ev(p, value, 0), ft);
		lbValue imag = lb_emit_conv(p, lb_emit_struct_ev(p, value, 1), ft);
		// @QuaternionLayout
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 3), real);
		lb_emit_store(p, lb_emit_struct_ep(p, gp, 0), imag);
		return lb_addr_load(p, gen);
	}

	// float <-> integer
	if (is_type_float(src) && is_type_integer(dst)) {
		if (is_type_different_to_arch_endianness(src) || is_type_different_to_arch_endianness(dst)) {
			Type *platform_src_type = integer_endian_type_to_platform_type(src);
			Type *platform_dst_type = integer_endian_type_to_platform_type(dst);
			lbValue res = {};
			res = lb_emit_conv(p, value, platform_src_type);
			res = lb_emit_conv(p, res, platform_dst_type);
			if (is_type_different_to_arch_endianness(dst)) {
				res = lb_emit_byte_swap(p, res, t);
			}
			return lb_emit_conv(p, res, t);
		}

		lbValue res = {};
		res.type = t;
		if (is_type_unsigned(dst)) {
			res.value = LLVMBuildFPToUI(p->builder, value.value, lb_type(m, t), "");
		} else {
			res.value = LLVMBuildFPToSI(p->builder, value.value, lb_type(m, t), "");
		}
		return res;
	}
	if (is_type_integer(src) && is_type_float(dst)) {
		if (is_type_different_to_arch_endianness(src) || is_type_different_to_arch_endianness(dst)) {
			Type *platform_src_type = integer_endian_type_to_platform_type(src);
			Type *platform_dst_type = integer_endian_type_to_platform_type(dst);
			lbValue res = {};
			res = lb_emit_conv(p, value, platform_src_type);
			res = lb_emit_conv(p, res, platform_dst_type);
			if (is_type_different_to_arch_endianness(dst)) {
				res = lb_emit_byte_swap(p, res, t);
			}
			return lb_emit_conv(p, res, t);
		}

		if (is_type_integer_128bit(src)) {
			auto args = array_make<lbValue>(temporary_allocator(), 1);
			args[0] = value;
			char const *call = "floattidf";
			if (is_type_unsigned(src)) {
				call = "floattidf_unsigned";
			}
			lbValue res_f64 = lb_emit_runtime_call(p, call, args);
			return lb_emit_conv(p, res_f64, t);
		}

		lbValue res = {};
		res.type = t;
		if (is_type_unsigned(src)) {
			res.value = LLVMBuildUIToFP(p->builder, value.value, lb_type(m, t), "");
		} else {
			res.value = LLVMBuildSIToFP(p->builder, value.value, lb_type(m, t), "");
		}
		return res;
	}

	// Pointer <-> uintptr
	if (is_type_pointer(src) && is_type_uintptr(dst)) {
		lbValue res = {};
		res.type = t;
		res.value = LLVMBuildPtrToInt(p->builder, value.value, lb_type(m, t), "");
		return res;
	}
	if (is_type_uintptr(src) && is_type_pointer(dst)) {
		lbValue res = {};
		res.type = t;
		res.value = LLVMBuildIntToPtr(p->builder, value.value, lb_type(m, t), "");
		return res;
	}

#if 1
	if (is_type_union(dst)) {
		for_array(i, dst->Union.variants) {
			Type *vt = dst->Union.variants[i];
			if (are_types_identical(vt, src_type)) {
				lbAddr parent = lb_add_local_generated(p, t, true);
				lb_emit_store_union_variant(p, parent.addr, value, vt);
				return lb_addr_load(p, parent);
			}
		}
	}
#endif

	// NOTE(bill): This has to be done before 'Pointer <-> Pointer' as it's
	// subtype polymorphism casting
	if (check_is_assignable_to_using_subtype(src_type, t)) {
		Type *st = type_deref(src_type);
		Type *pst = st;
		st = type_deref(st);

		bool st_is_ptr = is_type_pointer(src_type);
		st = base_type(st);

		Type *dt = t;
		bool dt_is_ptr = type_deref(dt) != dt;

		GB_ASSERT(is_type_struct(st) || is_type_raw_union(st));
		String field_name = lookup_subtype_polymorphic_field(p->module->info, t, src_type);
		if (field_name.len > 0) {
			// NOTE(bill): It can be casted
			Selection sel = lookup_field(st, field_name, false, true);
			if (sel.entity != nullptr) {
				if (st_is_ptr) {
					lbValue res = lb_emit_deep_field_gep(p, value, sel);
					Type *rt = res.type;
					if (!are_types_identical(rt, dt) && are_types_identical(type_deref(rt), dt)) {
						res = lb_emit_load(p, res);
					}
					return res;
				} else {
					if (is_type_pointer(value.type)) {
						Type *rt = value.type;
						if (!are_types_identical(rt, dt) && are_types_identical(type_deref(rt), dt)) {
							value = lb_emit_load(p, value);
						} else {
							value = lb_emit_deep_field_gep(p, value, sel);
							return lb_emit_load(p, value);
						}
					}

					return lb_emit_deep_field_ev(p, value, sel);

				}
			} else {
				GB_PANIC("invalid subtype cast  %s.%.*s", type_to_string(src_type), LIT(field_name));
			}
		}
	}



	// Pointer <-> Pointer
	if (is_type_pointer(src) && is_type_pointer(dst)) {
		lbValue res = {};
		res.type = t;
		res.value = LLVMBuildPointerCast(p->builder, value.value, lb_type(m, t), "");
		return res;
	}



	// proc <-> proc
	if (is_type_proc(src) && is_type_proc(dst)) {
		lbValue res = {};
		res.type = t;
		res.value = LLVMBuildPointerCast(p->builder, value.value, lb_type(m, t), "");
		return res;
	}

	// pointer -> proc
	if (is_type_pointer(src) && is_type_proc(dst)) {
		lbValue res = {};
		res.type = t;
		res.value = LLVMBuildPointerCast(p->builder, value.value, lb_type(m, t), "");
		return res;
	}
	// proc -> pointer
	if (is_type_proc(src) && is_type_pointer(dst)) {
		lbValue res = {};
		res.type = t;
		res.value = LLVMBuildPointerCast(p->builder, value.value, lb_type(m, t), "");
		return res;
	}

	// []byte/[]u8 <-> string
	if (is_type_u8_slice(src) && is_type_string(dst)) {
		return lb_emit_transmute(p, value, t);
	}
	if (is_type_string(src) && is_type_u8_slice(dst)) {
		return lb_emit_transmute(p, value, t);
	}

	if (is_type_array_like(dst)) {
		Type *elem = base_array_type(dst);
		lbValue e = lb_emit_conv(p, value, elem);
		// NOTE(bill): Doesn't need to be zero because it will be initialized in the loops
		lbAddr v = lb_add_local_generated(p, t, false);
		isize index_count = cast(isize)get_array_type_count(dst);

		for (isize i = 0; i < index_count; i++) {
			lbValue elem = lb_emit_array_epi(p, v.addr, i);
			lb_emit_store(p, elem, e);
		}
		return lb_addr_load(p, v);
	}

	if (is_type_any(dst)) {
		if (is_type_untyped_nil(src)) {
			return lb_const_nil(p->module, t);
		}
		if (is_type_untyped_undef(src)) {
			return lb_const_undef(p->module, t);
		}

		lbAddr result = lb_add_local_generated(p, t, true);

		Type *st = default_type(src_type);

		lbValue data = lb_address_from_load_or_generate_local(p, value);
		GB_ASSERT_MSG(is_type_pointer(data.type), "%s", type_to_string(data.type));
		GB_ASSERT_MSG(is_type_typed(st), "%s", type_to_string(st));
		data = lb_emit_conv(p, data, t_rawptr);

		lbValue id = lb_typeid(p->module, st);
		lbValue any_data = lb_emit_struct_ep(p, result.addr, 0);
		lbValue any_id   = lb_emit_struct_ep(p, result.addr, 1);

		lb_emit_store(p, any_data, data);
		lb_emit_store(p, any_id,   id);

		return lb_addr_load(p, result);
	}


	i64 src_sz = type_size_of(src);
	i64 dst_sz = type_size_of(dst);

	if (src_sz == dst_sz) {
		// bit_set <-> integer
		if (is_type_integer(src) && is_type_bit_set(dst)) {
			lbValue res = lb_emit_conv(p, value, bit_set_to_int(dst));
			res.type = dst;
			return res;
		}
		if (is_type_bit_set(src) && is_type_integer(dst)) {
			lbValue bs = value;
			bs.type = bit_set_to_int(src);
			return lb_emit_conv(p, bs, dst);
		}

		// typeid <-> integer
		if (is_type_integer(src) && is_type_typeid(dst)) {
			return lb_emit_transmute(p, value, dst);
		}
		if (is_type_typeid(src) && is_type_integer(dst)) {
			return lb_emit_transmute(p, value, dst);
		}
	}



	if (is_type_untyped(src)) {
		if (is_type_string(src) && is_type_string(dst)) {
			lbAddr result = lb_add_local_generated(p, t, false);
			lb_addr_store(p, result, value);
			return lb_addr_load(p, result);
		}
	}

	gb_printf_err("%.*s\n", LIT(p->name));
	gb_printf_err("lb_emit_conv: src -> dst\n");
	gb_printf_err("Not Identical %s != %s\n", type_to_string(src_type), type_to_string(t));
	gb_printf_err("Not Identical %s != %s\n", type_to_string(src), type_to_string(dst));
	gb_printf_err("Not Identical %p != %p\n", src_type, t);
	gb_printf_err("Not Identical %p != %p\n", src, dst);


	GB_PANIC("Invalid type conversion: '%s' to '%s' for procedure '%.*s'",
	         type_to_string(src_type), type_to_string(t),
	         LIT(p->name));

	return {};
}

bool lb_is_type_aggregate(Type *t) {
	t = base_type(t);
	switch (t->kind) {
	case Type_Basic:
		switch (t->Basic.kind) {
		case Basic_string:
		case Basic_any:
			return true;

		case Basic_complex32:
		case Basic_complex64:
		case Basic_complex128:
		case Basic_quaternion64:
		case Basic_quaternion128:
		case Basic_quaternion256:
			return true;
		}
		break;

	case Type_Pointer:
		return false;

	case Type_Array:
	case Type_Slice:
	case Type_Struct:
	case Type_Union:
	case Type_Tuple:
	case Type_DynamicArray:
	case Type_Map:
	case Type_SimdVector:
		return true;

	case Type_Named:
		return lb_is_type_aggregate(t->Named.base);
	}

	return false;
}

lbValue lb_emit_transmute(lbProcedure *p, lbValue value, Type *t) {
	Type *src_type = value.type;
	if (are_types_identical(t, src_type)) {
		return value;
	}

	lbValue res = {};
	res.type = t;


	Type *src = base_type(src_type);
	Type *dst = base_type(t);

	lbModule *m = p->module;

	i64 sz = type_size_of(src);
	i64 dz = type_size_of(dst);

	if (sz != dz) {
		LLVMTypeRef s = lb_type(m, src);
		LLVMTypeRef d = lb_type(m, dst);
		i64 llvm_sz = lb_sizeof(s);
		i64 llvm_dz = lb_sizeof(d);
		GB_ASSERT_MSG(llvm_sz == llvm_dz, "%s %s", LLVMPrintTypeToString(s), LLVMPrintTypeToString(d));
	}

	GB_ASSERT_MSG(sz == dz, "Invalid transmute conversion: '%s' to '%s'", type_to_string(src_type), type_to_string(t));

	// NOTE(bill): Casting between an integer and a pointer cannot be done through a bitcast
	if (is_type_uintptr(src) && is_type_pointer(dst)) {
		res.value = LLVMBuildIntToPtr(p->builder, value.value, lb_type(m, t), "");
		return res;
	}
	if (is_type_pointer(src) && is_type_uintptr(dst)) {
		res.value = LLVMBuildPtrToInt(p->builder, value.value, lb_type(m, t), "");
		return res;
	}
	if (is_type_uintptr(src) && is_type_proc(dst)) {
		res.value = LLVMBuildIntToPtr(p->builder, value.value, lb_type(m, t), "");
		return res;
	}
	if (is_type_proc(src) && is_type_uintptr(dst)) {
		res.value = LLVMBuildPtrToInt(p->builder, value.value, lb_type(m, t), "");
		return res;
	}

	if (is_type_integer(src) && (is_type_pointer(dst) || is_type_cstring(dst))) {
		res.value = LLVMBuildIntToPtr(p->builder, value.value, lb_type(m, t), "");
		return res;
	} else if ((is_type_pointer(src) || is_type_cstring(src)) && is_type_integer(dst)) {
		res.value = LLVMBuildPtrToInt(p->builder, value.value, lb_type(m, t), "");
		return res;
	}

	if (is_type_pointer(src) && is_type_pointer(dst)) {
		res.value = LLVMBuildPointerCast(p->builder, value.value, lb_type(p->module, t), "");
		return res;
	}

	if (lb_is_type_aggregate(src) || lb_is_type_aggregate(dst)) {
		lbValue s = lb_address_from_load_or_generate_local(p, value);
		lbValue d = lb_emit_transmute(p, s, alloc_type_pointer(t));
		return lb_emit_load(p, d);
	}

	res.value = LLVMBuildBitCast(p->builder, value.value, lb_type(p->module, t), "");
	return res;
}


void lb_emit_init_context(lbProcedure *p, lbAddr addr) {
	GB_ASSERT(addr.kind == lbAddr_Context);
	GB_ASSERT(addr.ctx.sel.index.count == 0);

	lbModule *m = p->module;
	auto args = array_make<lbValue>(permanent_allocator(), 1);
	args[0] = addr.addr;
	lb_emit_runtime_call(p, "__init_context", args);
}

lbContextData *lb_push_context_onto_stack_from_implicit_parameter(lbProcedure *p) {
	Type *pt = base_type(p->type);
	GB_ASSERT(pt->kind == Type_Proc);
	GB_ASSERT(pt->Proc.calling_convention == ProcCC_Odin);

	String name = str_lit("__.context_ptr");

	Entity *e = alloc_entity_param(nullptr, make_token_ident(name), t_context_ptr, false, false);
	e->flags |= EntityFlag_NoAlias;

	LLVMValueRef context_ptr = LLVMGetParam(p->value, LLVMCountParams(p->value)-1);
	LLVMSetValueName2(context_ptr, cast(char const *)name.text, name.len);
	context_ptr = LLVMBuildPointerCast(p->builder, context_ptr, lb_type(p->module, e->type), "");

	lbValue param = {context_ptr, e->type};
	lb_add_entity(p->module, e, param);
	lbAddr ctx_addr = {};
	ctx_addr.kind = lbAddr_Context;
	ctx_addr.addr = param;

	lbContextData *cd = array_add_and_get(&p->context_stack);
	cd->ctx = ctx_addr;
	cd->scope_index = -1;
	cd->uses = +1; // make sure it has been used already
	return cd;
}

lbContextData *lb_push_context_onto_stack(lbProcedure *p, lbAddr ctx) {
	ctx.kind = lbAddr_Context;
	lbContextData *cd = array_add_and_get(&p->context_stack);
	cd->ctx = ctx;
	cd->scope_index = p->scope_index;
	return cd;
}


lbAddr lb_find_or_generate_context_ptr(lbProcedure *p) {
	if (p->context_stack.count > 0) {
		return p->context_stack[p->context_stack.count-1].ctx;
	}

	Type *pt = base_type(p->type);
	GB_ASSERT(pt->kind == Type_Proc);
	GB_ASSERT(pt->Proc.calling_convention != ProcCC_Odin);

	lbAddr c = lb_add_local_generated(p, t_context, true);
	c.kind = lbAddr_Context;
	lb_emit_init_context(p, c);
	lb_push_context_onto_stack(p, c);
	lb_add_debug_context_variable(p, c);

	return c;
}

lbValue lb_address_from_load_or_generate_local(lbProcedure *p, lbValue value) {
	if (LLVMIsALoadInst(value.value)) {
		lbValue res = {};
		res.value = LLVMGetOperand(value.value, 0);
		res.type = alloc_type_pointer(value.type);
		return res;
	}

	GB_ASSERT(is_type_typed(value.type));

	lbAddr res = lb_add_local_generated(p, value.type, false);
	lb_addr_store(p, res, value);
	return res.addr;
}
lbValue lb_address_from_load(lbProcedure *p, lbValue value) {
	if (LLVMIsALoadInst(value.value)) {
		lbValue res = {};
		res.value = LLVMGetOperand(value.value, 0);
		res.type = alloc_type_pointer(value.type);
		return res;
	}

	GB_PANIC("lb_address_from_load");
	return {};
}

lbValue lb_copy_value_to_ptr(lbProcedure *p, lbValue val, Type *new_type, i64 alignment) {
	i64 type_alignment = type_align_of(new_type);
	if (alignment < type_alignment) {
		alignment = type_alignment;
	}
	GB_ASSERT_MSG(are_types_identical(new_type, val.type), "%s %s", type_to_string(new_type), type_to_string(val.type));

	lbAddr ptr = lb_add_local_generated(p, new_type, false);
	LLVMSetAlignment(ptr.addr.value, cast(unsigned)alignment);
	lb_addr_store(p, ptr, val);
	// ptr.kind = lbAddr_Context;
	return ptr.addr;
}

lbValue lb_emit_struct_ep(lbProcedure *p, lbValue s, i32 index) {
	GB_ASSERT(is_type_pointer(s.type));
	Type *t = base_type(type_deref(s.type));
	Type *result_type = nullptr;

	if (is_type_relative_pointer(t)) {
		s = lb_addr_get_ptr(p, lb_addr(s));
	}

	if (is_type_struct(t)) {
		result_type = get_struct_field_type(t, index);
	} else if (is_type_union(t)) {
		GB_ASSERT(index == -1);
		return lb_emit_union_tag_ptr(p, s);
	} else if (is_type_tuple(t)) {
		GB_ASSERT(t->Tuple.variables.count > 0);
		result_type = t->Tuple.variables[index]->type;
	} else if (is_type_complex(t)) {
		Type *ft = base_complex_elem_type(t);
		switch (index) {
		case 0: result_type = ft; break;
		case 1: result_type = ft; break;
		}
	} else if (is_type_quaternion(t)) {
		Type *ft = base_complex_elem_type(t);
		switch (index) {
		case 0: result_type = ft; break;
		case 1: result_type = ft; break;
		case 2: result_type = ft; break;
		case 3: result_type = ft; break;
		}
	} else if (is_type_slice(t)) {
		switch (index) {
		case 0: result_type = alloc_type_pointer(t->Slice.elem); break;
		case 1: result_type = t_int; break;
		}
	} else if (is_type_string(t)) {
		switch (index) {
		case 0: result_type = t_u8_ptr; break;
		case 1: result_type = t_int;    break;
		}
	} else if (is_type_any(t)) {
		switch (index) {
		case 0: result_type = t_rawptr; break;
		case 1: result_type = t_typeid; break;
		}
	} else if (is_type_dynamic_array(t)) {
		switch (index) {
		case 0: result_type = alloc_type_pointer(t->DynamicArray.elem); break;
		case 1: result_type = t_int;       break;
		case 2: result_type = t_int;       break;
		case 3: result_type = t_allocator; break;
		}
	} else if (is_type_map(t)) {
		init_map_internal_types(t);
		Type *itp = alloc_type_pointer(t->Map.internal_type);
		s = lb_emit_transmute(p, s, itp);

		Type *gst = t->Map.internal_type;
		GB_ASSERT(gst->kind == Type_Struct);
		switch (index) {
		case 0: result_type = get_struct_field_type(gst, 0); break;
		case 1: result_type = get_struct_field_type(gst, 1); break;
		}
	} else if (is_type_array(t)) {
		return lb_emit_array_epi(p, s, index);
	} else if (is_type_relative_slice(t)) {
		switch (index) {
		case 0: result_type = t->RelativeSlice.base_integer; break;
		case 1: result_type = t->RelativeSlice.base_integer; break;
		}
	} else {
		GB_PANIC("TODO(bill): struct_gep type: %s, %d", type_to_string(s.type), index);
	}

	GB_ASSERT_MSG(result_type != nullptr, "%s %d", type_to_string(t), index);

	if (t->kind == Type_Struct && t->Struct.custom_align != 0) {
		index += 1;
	}
	if (lb_is_const(s)) {
		lbModule *m = p->module;
		lbValue res = {};
		LLVMValueRef indices[2] = {llvm_zero(m), LLVMConstInt(lb_type(m, t_i32), index, false)};
		res.value = LLVMConstGEP(s.value, indices, gb_count_of(indices));
		res.type = alloc_type_pointer(result_type);
		return res;
	} else {
		lbValue res = {};
		res.value = LLVMBuildStructGEP(p->builder, s.value, cast(unsigned)index, "");
		res.type = alloc_type_pointer(result_type);
		return res;
	}
}

lbValue lb_emit_struct_ev(lbProcedure *p, lbValue s, i32 index) {
	if (LLVMIsALoadInst(s.value)) {
		lbValue res = {};
		res.value = LLVMGetOperand(s.value, 0);
		res.type = alloc_type_pointer(s.type);
		lbValue ptr = lb_emit_struct_ep(p, res, index);
		return lb_emit_load(p, ptr);
	}

	Type *t = base_type(s.type);
	Type *result_type = nullptr;

	switch (t->kind) {
	case Type_Basic:
		switch (t->Basic.kind) {
		case Basic_string:
			switch (index) {
			case 0: result_type = t_u8_ptr; break;
			case 1: result_type = t_int;    break;
			}
			break;
		case Basic_any:
			switch (index) {
			case 0: result_type = t_rawptr; break;
			case 1: result_type = t_typeid; break;
			}
			break;
		case Basic_complex32:
		case Basic_complex64:
		case Basic_complex128:
		{
			Type *ft = base_complex_elem_type(t);
			switch (index) {
			case 0: result_type = ft; break;
			case 1: result_type = ft; break;
			}
			break;
		}
		case Basic_quaternion64:
		case Basic_quaternion128:
		case Basic_quaternion256:
		{
			Type *ft = base_complex_elem_type(t);
			switch (index) {
			case 0: result_type = ft; break;
			case 1: result_type = ft; break;
			case 2: result_type = ft; break;
			case 3: result_type = ft; break;
			}
			break;
		}
		}
		break;
	case Type_Struct:
		result_type = get_struct_field_type(t, index);
		break;
	case Type_Union:
		GB_ASSERT(index == -1);
		// return lb_emit_union_tag_value(p, s);
		GB_PANIC("lb_emit_union_tag_value");

	case Type_Tuple:
		GB_ASSERT(t->Tuple.variables.count > 0);
		result_type = t->Tuple.variables[index]->type;
		if (t->Tuple.variables.count == 1) {
			return s;
		}
		break;
	case Type_Slice:
		switch (index) {
		case 0: result_type = alloc_type_pointer(t->Slice.elem); break;
		case 1: result_type = t_int; break;
		}
		break;
	case Type_DynamicArray:
		switch (index) {
		case 0: result_type = alloc_type_pointer(t->DynamicArray.elem); break;
		case 1: result_type = t_int;                                    break;
		case 2: result_type = t_int;                                    break;
		case 3: result_type = t_allocator;                              break;
		}
		break;

	case Type_Map:
		{
			init_map_internal_types(t);
			Type *gst = t->Map.generated_struct_type;
			switch (index) {
			case 0: result_type = get_struct_field_type(gst, 0); break;
			case 1: result_type = get_struct_field_type(gst, 1); break;
			}
		}
		break;

	case Type_Array:
		result_type = t->Array.elem;
		break;

	default:
		GB_PANIC("TODO(bill): struct_ev type: %s, %d", type_to_string(s.type), index);
		break;
	}

	GB_ASSERT_MSG(result_type != nullptr, "%s, %d", type_to_string(s.type), index);

	if (t->kind == Type_Struct && t->Struct.custom_align != 0) {
		index += 1;
	}

	lbValue res = {};
	res.value = LLVMBuildExtractValue(p->builder, s.value, cast(unsigned)index, "");
	res.type = result_type;
	return res;
}

lbValue lb_emit_deep_field_gep(lbProcedure *p, lbValue e, Selection sel) {
	GB_ASSERT(sel.index.count > 0);
	Type *type = type_deref(e.type);

	for_array(i, sel.index) {
		i32 index = cast(i32)sel.index[i];
		if (is_type_pointer(type)) {
			type = type_deref(type);
			e = lb_emit_load(p, e);
		}
		type = core_type(type);

		if (is_type_quaternion(type)) {
			e = lb_emit_struct_ep(p, e, index);
		} else if (is_type_raw_union(type)) {
			type = get_struct_field_type(type, index);
			GB_ASSERT(is_type_pointer(e.type));
			e = lb_emit_transmute(p, e, alloc_type_pointer(type));
		} else if (is_type_struct(type)) {
			type = get_struct_field_type(type, index);
			e = lb_emit_struct_ep(p, e, index);
		} else if (type->kind == Type_Union) {
			GB_ASSERT(index == -1);
			type = t_type_info_ptr;
			e = lb_emit_struct_ep(p, e, index);
		} else if (type->kind == Type_Tuple) {
			type = type->Tuple.variables[index]->type;
			e = lb_emit_struct_ep(p, e, index);
		} else if (type->kind == Type_Basic) {
			switch (type->Basic.kind) {
			case Basic_any: {
				if (index == 0) {
					type = t_rawptr;
				} else if (index == 1) {
					type = t_type_info_ptr;
				}
				e = lb_emit_struct_ep(p, e, index);
				break;
			}

			case Basic_string:
				e = lb_emit_struct_ep(p, e, index);
				break;

			default:
				GB_PANIC("un-gep-able type %s", type_to_string(type));
				break;
			}
		} else if (type->kind == Type_Slice) {
			e = lb_emit_struct_ep(p, e, index);
		} else if (type->kind == Type_DynamicArray) {
			e = lb_emit_struct_ep(p, e, index);
		} else if (type->kind == Type_Array) {
			e = lb_emit_array_epi(p, e, index);
		} else if (type->kind == Type_Map) {
			e = lb_emit_struct_ep(p, e, index);
		} else if (type->kind == Type_RelativePointer) {
			e = lb_emit_struct_ep(p, e, index);
		} else {
			GB_PANIC("un-gep-able type %s", type_to_string(type));
		}
	}

	return e;
}


lbValue lb_emit_deep_field_ev(lbProcedure *p, lbValue e, Selection sel) {
	lbValue ptr = lb_address_from_load_or_generate_local(p, e);
	lbValue res = lb_emit_deep_field_gep(p, ptr, sel);
	return lb_emit_load(p, res);
}



void lb_build_defer_stmt(lbProcedure *p, lbDefer const &d) {
	if (p->curr_block == nullptr) {
		return;
	}
	// NOTE(bill): The prev block may defer injection before it's terminator
	LLVMValueRef last_instr = LLVMGetLastInstruction(p->curr_block->block);
	if (last_instr != nullptr && LLVMIsAReturnInst(last_instr)) {
		// NOTE(bill): ReturnStmt defer stuff will be handled previously
		return;
	}

	isize prev_context_stack_count = p->context_stack.count;
	GB_ASSERT(prev_context_stack_count <= p->context_stack.capacity);
	defer (p->context_stack.count = prev_context_stack_count);
	p->context_stack.count = d.context_stack_count;

	lbBlock *b = lb_create_block(p, "defer");
	if (last_instr == nullptr || !LLVMIsATerminatorInst(last_instr)) {
		lb_emit_jump(p, b);
	}

	lb_start_block(p, b);
	if (d.kind == lbDefer_Node) {
		lb_build_stmt(p, d.stmt);
	} else if (d.kind == lbDefer_Instr) {
		// NOTE(bill): Need to make a new copy
		LLVMValueRef instr = LLVMInstructionClone(d.instr.value);
		LLVMInsertIntoBuilder(p->builder, instr);
	} else if (d.kind == lbDefer_Proc) {
		lb_emit_call(p, d.proc.deferred, d.proc.result_as_args);
	}
}

void lb_emit_defer_stmts(lbProcedure *p, lbDeferExitKind kind, lbBlock *block) {
	isize count = p->defer_stmts.count;
	isize i = count;
	while (i --> 0) {
		lbDefer const &d = p->defer_stmts[i];

		if (kind == lbDeferExit_Default) {
			if (p->scope_index == d.scope_index &&
			    d.scope_index > 0) { // TODO(bill): Which is correct: > 0 or > 1?
				lb_build_defer_stmt(p, d);
				array_pop(&p->defer_stmts);
				continue;
			} else {
				break;
			}
		} else if (kind == lbDeferExit_Return) {
			lb_build_defer_stmt(p, d);
		} else if (kind == lbDeferExit_Branch) {
			GB_ASSERT(block != nullptr);
			isize lower_limit = block->scope_index;
			if (lower_limit < d.scope_index) {
				lb_build_defer_stmt(p, d);
			}
		}
	}
}

void lb_add_defer_node(lbProcedure *p, isize scope_index, Ast *stmt) {
	Type *pt = base_type(p->type);
	GB_ASSERT(pt->kind == Type_Proc);
	if (pt->Proc.calling_convention == ProcCC_Odin) {
		GB_ASSERT(p->context_stack.count != 0);
	}

	lbDefer *d = array_add_and_get(&p->defer_stmts);
	d->kind = lbDefer_Node;
	d->scope_index = scope_index;
	d->context_stack_count = p->context_stack.count;
	d->block = p->curr_block;
	d->stmt = stmt;
}

void lb_add_defer_proc(lbProcedure *p, isize scope_index, lbValue deferred, Array<lbValue> const &result_as_args) {
	Type *pt = base_type(p->type);
	GB_ASSERT(pt->kind == Type_Proc);
	if (pt->Proc.calling_convention == ProcCC_Odin) {
		GB_ASSERT(p->context_stack.count != 0);
	}

	lbDefer *d = array_add_and_get(&p->defer_stmts);
	d->kind = lbDefer_Proc;
	d->scope_index = p->scope_index;
	d->block = p->curr_block;
	d->context_stack_count = p->context_stack.count;
	d->proc.deferred = deferred;
	d->proc.result_as_args = result_as_args;
}



Array<lbValue> lb_value_to_array(lbProcedure *p, lbValue value) {
	Array<lbValue> array = {};
	Type *t = base_type(value.type);
	if (t == nullptr) {
		// Do nothing
	} else if (is_type_tuple(t)) {
		GB_ASSERT(t->kind == Type_Tuple);
		auto *rt = &t->Tuple;
		if (rt->variables.count > 0) {
			array = array_make<lbValue>(permanent_allocator(), rt->variables.count);
			for_array(i, rt->variables) {
				lbValue elem = lb_emit_struct_ev(p, value, cast(i32)i);
				array[i] = elem;
			}
		}
	} else {
		array = array_make<lbValue>(permanent_allocator(), 1);
		array[0] = value;
	}
	return array;
}



lbValue lb_emit_call_internal(lbProcedure *p, lbValue value, lbValue return_ptr, Array<lbValue> const &processed_args, Type *abi_rt, lbAddr context_ptr, ProcInlining inlining) {
	GB_ASSERT(p->module->ctx == LLVMGetTypeContext(LLVMTypeOf(value.value)));

	unsigned arg_count = cast(unsigned)processed_args.count;
	if (return_ptr.value != nullptr) {
		arg_count += 1;
	}
	if (context_ptr.addr.value != nullptr) {
		arg_count += 1;
	}

	LLVMValueRef *args = gb_alloc_array(permanent_allocator(), LLVMValueRef, arg_count);
	isize arg_index = 0;
	if (return_ptr.value != nullptr) {
		args[arg_index++] = return_ptr.value;
	}
	for_array(i, processed_args) {
		lbValue arg = processed_args[i];
		args[arg_index++] = arg.value;
	}
	if (context_ptr.addr.value != nullptr) {
		LLVMValueRef cp = context_ptr.addr.value;
		cp = LLVMBuildPointerCast(p->builder, cp, lb_type(p->module, t_rawptr), "");
		args[arg_index++] = cp;
	}
	LLVMBasicBlockRef curr_block = LLVMGetInsertBlock(p->builder);
	GB_ASSERT(curr_block != p->decl_block->block);

	{
		LLVMTypeRef ftp = lb_type(p->module, value.type);
		LLVMTypeRef ft = LLVMGetElementType(ftp);
		LLVMValueRef fn = value.value;
		if (!lb_is_type_kind(LLVMTypeOf(value.value), LLVMFunctionTypeKind)) {
			fn = LLVMBuildPointerCast(p->builder, fn, ftp, "");
		}
		LLVMTypeRef fnp = LLVMGetElementType(LLVMTypeOf(fn));
		GB_ASSERT_MSG(lb_is_type_kind(fnp, LLVMFunctionTypeKind), "%s", LLVMPrintTypeToString(fnp));

		{
			unsigned param_count = LLVMCountParamTypes(fnp);
			GB_ASSERT(arg_count >= param_count);

			LLVMTypeRef *param_types = gb_alloc_array(temporary_allocator(), LLVMTypeRef, param_count);
			LLVMGetParamTypes(fnp, param_types);
			for (unsigned i = 0; i < param_count; i++) {
				LLVMTypeRef param_type = param_types[i];
				LLVMTypeRef arg_type = LLVMTypeOf(args[i]);
				GB_ASSERT_MSG(
					arg_type == param_type,
					"Parameter types do not match: %s != %s, argument: %s",
					LLVMPrintTypeToString(arg_type),
					LLVMPrintTypeToString(param_type),
					LLVMPrintValueToString(args[i])
				);
			}
		}

		LLVMValueRef ret = LLVMBuildCall2(p->builder, fnp, fn, args, arg_count, "");

		switch (inlining) {
		case ProcInlining_none:
			break;
		case ProcInlining_inline:
			LLVMAddCallSiteAttribute(ret, LLVMAttributeIndex_FunctionIndex, lb_create_enum_attribute(p->module->ctx, "alwaysinline"));
			break;
		case ProcInlining_no_inline:
			LLVMAddCallSiteAttribute(ret, LLVMAttributeIndex_FunctionIndex, lb_create_enum_attribute(p->module->ctx, "noinline"));
			break;
		}

		lbValue res = {};
		res.value = ret;
		res.type = abi_rt;
		return res;
	}
}


lbValue lb_lookup_runtime_procedure(lbModule *m, String const &name) {
	AstPackage *pkg = m->info->runtime_package;
	Entity *e = scope_lookup_current(pkg->scope, name);
	return lb_find_procedure_value_from_entity(m, e);
}


lbValue lb_emit_runtime_call(lbProcedure *p, char const *c_name, Array<lbValue> const &args) {
	String name = make_string_c(c_name);
	lbValue proc = lb_lookup_runtime_procedure(p->module, name);
	return lb_emit_call(p, proc, args);
}

lbValue lb_emit_call(lbProcedure *p, lbValue value, Array<lbValue> const &args, ProcInlining inlining, bool use_copy_elision_hint) {
	lbModule *m = p->module;

	Type *pt = base_type(value.type);
	GB_ASSERT(pt->kind == Type_Proc);
	Type *results = pt->Proc.results;

	if (p->entity != nullptr) {
		if (p->entity->flags & EntityFlag_Disabled) {
			return {};
		}
	}

	lbAddr context_ptr = {};
	if (pt->Proc.calling_convention == ProcCC_Odin) {
		context_ptr = lb_find_or_generate_context_ptr(p);
	}

	defer (if (pt->Proc.diverging) {
		LLVMBuildUnreachable(p->builder);
	});

	bool is_c_vararg = pt->Proc.c_vararg;
	isize param_count = pt->Proc.param_count;
	if (is_c_vararg) {
		GB_ASSERT(param_count-1 <= args.count);
		param_count -= 1;
	} else {
		GB_ASSERT_MSG(param_count == args.count, "%td == %td", param_count, args.count);
	}

	lbValue result = {};

	auto processed_args = array_make<lbValue>(permanent_allocator(), 0, args.count);

	{
		lbFunctionType *ft = lb_get_function_type(m, p, pt);
		bool return_by_pointer = ft->ret.kind == lbArg_Indirect;

		unsigned param_index = 0;
		for (isize i = 0; i < param_count; i++) {
			Entity *e = pt->Proc.params->Tuple.variables[i];
			if (e->kind != Entity_Variable) {
				continue;
			}
			GB_ASSERT(e->flags & EntityFlag_Param);

			Type *original_type = e->type;
			lbArgType *arg = &ft->args[param_index];
			if (arg->kind == lbArg_Ignore) {
				continue;
			}

			lbValue x = lb_emit_conv(p, args[i], original_type);
			LLVMTypeRef xt = lb_type(p->module, x.type);

			if (arg->kind == lbArg_Direct) {
				LLVMTypeRef abi_type = arg->cast_type;
				if (!abi_type) {
					abi_type = arg->type;
				}
				if (xt == abi_type) {
					array_add(&processed_args, x);
				} else {
					x.value = OdinLLVMBuildTransmute(p, x.value, abi_type);
					array_add(&processed_args, x);
				}

			} else if (arg->kind == lbArg_Indirect) {
				lbValue ptr = {};
				if (arg->is_byval) {
					ptr = lb_copy_value_to_ptr(p, x, original_type, arg->byval_alignment);
				} else if (is_calling_convention_odin(pt->Proc.calling_convention)) {
					// NOTE(bill): Odin parameters are immutable so the original value can be passed if possible
					// i.e. `T const &` in C++
					ptr = lb_address_from_load_or_generate_local(p, x);
				} else {
					ptr = lb_copy_value_to_ptr(p, x, original_type, 16);
				}
				array_add(&processed_args, ptr);
			}

			param_index += 1;
		}

		if (is_c_vararg) {
			for (isize i = processed_args.count; i < args.count; i++) {
				array_add(&processed_args, args[i]);
			}
		}

		if (inlining == ProcInlining_none) {
			inlining = p->inlining;
		}

		Type *rt = reduce_tuple_to_single_type(results);
		if (return_by_pointer) {
			lbValue return_ptr = {};
			if (use_copy_elision_hint && p->copy_elision_hint.ptr.value != nullptr) {
				if (are_types_identical(type_deref(p->copy_elision_hint.ptr.type), rt)) {
					return_ptr = lb_consume_copy_elision_hint(p);
				}
			}
			if (return_ptr.value == nullptr) {
				lbAddr r = lb_add_local_generated(p, rt, true);
				return_ptr = r.addr;
			}
			GB_ASSERT(is_type_pointer(return_ptr.type));
			lb_emit_call_internal(p, value, return_ptr, processed_args, nullptr, context_ptr, inlining);
			result = lb_emit_load(p, return_ptr);
		} else if (rt != nullptr) {
			result = lb_emit_call_internal(p, value, {}, processed_args, rt, context_ptr, inlining);
			if (ft->ret.cast_type) {
				result.value = OdinLLVMBuildTransmute(p, result.value, ft->ret.cast_type);
			}
			result.value = OdinLLVMBuildTransmute(p, result.value, ft->ret.type);
			result.type = rt;
			if (LLVMTypeOf(result.value) == LLVMInt1TypeInContext(p->module->ctx)) {
				result.type = t_llvm_bool;
			}
			if (!is_type_tuple(rt)) {
				result = lb_emit_conv(p, result, rt);
			}
		} else {
			lb_emit_call_internal(p, value, {}, processed_args, nullptr, context_ptr, inlining);
		}

	}

	Entity **found = map_get(&p->module->procedure_values, hash_pointer(value.value));
	if (found != nullptr) {
		Entity *e = *found;
		if (e != nullptr && entity_has_deferred_procedure(e)) {
			DeferredProcedureKind kind = e->Procedure.deferred_procedure.kind;
			Entity *deferred_entity = e->Procedure.deferred_procedure.entity;
			lbValue deferred = lb_find_procedure_value_from_entity(p->module, deferred_entity);


			auto in_args = args;
			Array<lbValue> result_as_args = {};
			switch (kind) {
			case DeferredProcedure_none:
				break;
			case DeferredProcedure_in:
				result_as_args = in_args;
				break;
			case DeferredProcedure_out:
				result_as_args = lb_value_to_array(p, result);
				break;
			case DeferredProcedure_in_out:
				{
					auto out_args = lb_value_to_array(p, result);
					array_init(&result_as_args, permanent_allocator(), in_args.count + out_args.count);
					array_copy(&result_as_args, in_args, 0);
					array_copy(&result_as_args, out_args, in_args.count);
				}
				break;
			}

			lb_add_defer_proc(p, p->scope_index, deferred, result_as_args);
		}
	}

	return result;
}

lbValue lb_emit_array_ep(lbProcedure *p, lbValue s, lbValue index) {
	Type *t = s.type;
	GB_ASSERT(is_type_pointer(t));
	Type *st = base_type(type_deref(t));
	GB_ASSERT_MSG(is_type_array(st) || is_type_enumerated_array(st), "%s", type_to_string(st));
	GB_ASSERT_MSG(is_type_integer(core_type(index.type)), "%s", type_to_string(index.type));

	LLVMValueRef indices[2] = {};
	indices[0] = llvm_zero(p->module);
	indices[1] = lb_emit_conv(p, index, t_int).value;

	Type *ptr = base_array_type(st);
	lbValue res = {};
	res.value = LLVMBuildGEP(p->builder, s.value, indices, 2, "");
	res.type = alloc_type_pointer(ptr);
	return res;
}

lbValue lb_emit_array_epi(lbProcedure *p, lbValue s, isize index) {
	Type *t = s.type;
	GB_ASSERT(is_type_pointer(t));
	Type *st = base_type(type_deref(t));
	GB_ASSERT_MSG(is_type_array(st) || is_type_enumerated_array(st), "%s", type_to_string(st));

	GB_ASSERT(0 <= index);
	Type *ptr = base_array_type(st);


	LLVMValueRef indices[2] = {
		LLVMConstInt(lb_type(p->module, t_int), 0, false),
		LLVMConstInt(lb_type(p->module, t_int), cast(unsigned)index, false),
	};

	lbValue res = {};
	if (lb_is_const(s)) {
		res.value = LLVMConstGEP(s.value, indices, gb_count_of(indices));
	} else {
		res.value = LLVMBuildGEP(p->builder, s.value, indices, gb_count_of(indices), "");
	}
	res.type = alloc_type_pointer(ptr);
	return res;
}

lbValue lb_emit_ptr_offset(lbProcedure *p, lbValue ptr, lbValue index) {
	LLVMValueRef indices[1] = {index.value};
	lbValue res = {};
	res.type = ptr.type;

	if (lb_is_const(ptr) && lb_is_const(index)) {
		res.value = LLVMConstGEP(ptr.value, indices, 1);
	} else {
		res.value = LLVMBuildGEP(p->builder, ptr.value, indices, 1, "");
	}
	return res;
}

LLVMValueRef llvm_const_slice(lbModule *m, lbValue data, lbValue len) {
	GB_ASSERT(is_type_pointer(data.type));
	GB_ASSERT(are_types_identical(len.type, t_int));
	LLVMValueRef vals[2] = {
		data.value,
		len.value,
	};
	return LLVMConstStructInContext(m->ctx, vals, gb_count_of(vals), false);
}


void lb_fill_slice(lbProcedure *p, lbAddr const &slice, lbValue base_elem, lbValue len) {
	Type *t = lb_addr_type(slice);
	GB_ASSERT(is_type_slice(t));
	lbValue ptr = lb_addr_get_ptr(p, slice);
	lb_emit_store(p, lb_emit_struct_ep(p, ptr, 0), base_elem);
	lb_emit_store(p, lb_emit_struct_ep(p, ptr, 1), len);
}
void lb_fill_string(lbProcedure *p, lbAddr const &string, lbValue base_elem, lbValue len) {
	Type *t = lb_addr_type(string);
	GB_ASSERT(is_type_string(t));
	lbValue ptr = lb_addr_get_ptr(p, string);
	lb_emit_store(p, lb_emit_struct_ep(p, ptr, 0), base_elem);
	lb_emit_store(p, lb_emit_struct_ep(p, ptr, 1), len);
}

lbValue lb_string_elem(lbProcedure *p, lbValue string) {
	Type *t = base_type(string.type);
	GB_ASSERT(t->kind == Type_Basic && t->Basic.kind == Basic_string);
	return lb_emit_struct_ev(p, string, 0);
}
lbValue lb_string_len(lbProcedure *p, lbValue string) {
	Type *t = base_type(string.type);
	GB_ASSERT_MSG(t->kind == Type_Basic && t->Basic.kind == Basic_string, "%s", type_to_string(t));
	return lb_emit_struct_ev(p, string, 1);
}

lbValue lb_cstring_len(lbProcedure *p, lbValue value) {
	GB_ASSERT(is_type_cstring(value.type));
	auto args = array_make<lbValue>(permanent_allocator(), 1);
	args[0] = lb_emit_conv(p, value, t_cstring);
	return lb_emit_runtime_call(p, "cstring_len", args);
}


lbValue lb_array_elem(lbProcedure *p, lbValue array_ptr) {
	Type *t = type_deref(array_ptr.type);
	GB_ASSERT(is_type_array(t));
	return lb_emit_struct_ep(p, array_ptr, 0);
}

lbValue lb_slice_elem(lbProcedure *p, lbValue slice) {
	GB_ASSERT(is_type_slice(slice.type));
	return lb_emit_struct_ev(p, slice, 0);
}
lbValue lb_slice_len(lbProcedure *p, lbValue slice) {
	GB_ASSERT(is_type_slice(slice.type));
	return lb_emit_struct_ev(p, slice, 1);
}
lbValue lb_dynamic_array_elem(lbProcedure *p, lbValue da) {
	GB_ASSERT(is_type_dynamic_array(da.type));
	return lb_emit_struct_ev(p, da, 0);
}
lbValue lb_dynamic_array_len(lbProcedure *p, lbValue da) {
	GB_ASSERT(is_type_dynamic_array(da.type));
	return lb_emit_struct_ev(p, da, 1);
}
lbValue lb_dynamic_array_cap(lbProcedure *p, lbValue da) {
	GB_ASSERT(is_type_dynamic_array(da.type));
	return lb_emit_struct_ev(p, da, 2);
}
lbValue lb_dynamic_array_allocator(lbProcedure *p, lbValue da) {
	GB_ASSERT(is_type_dynamic_array(da.type));
	return lb_emit_struct_ev(p, da, 3);
}

lbValue lb_map_entries(lbProcedure *p, lbValue value) {
	Type *t = base_type(value.type);
	GB_ASSERT_MSG(t->kind == Type_Map, "%s", type_to_string(t));
	init_map_internal_types(t);
	Type *gst = t->Map.generated_struct_type;
	i32 index = 1;
	lbValue entries = lb_emit_struct_ev(p, value, index);
	return entries;
}

lbValue lb_map_entries_ptr(lbProcedure *p, lbValue value) {
	Type *t = base_type(type_deref(value.type));
	GB_ASSERT_MSG(t->kind == Type_Map, "%s", type_to_string(t));
	init_map_internal_types(t);
	Type *gst = t->Map.generated_struct_type;
	i32 index = 1;
	lbValue entries = lb_emit_struct_ep(p, value, index);
	return entries;
}

lbValue lb_map_len(lbProcedure *p, lbValue value) {
	lbValue entries = lb_map_entries(p, value);
	return lb_dynamic_array_len(p, entries);
}

lbValue lb_map_cap(lbProcedure *p, lbValue value) {
	lbValue entries = lb_map_entries(p, value);
	return lb_dynamic_array_cap(p, entries);
}

lbValue lb_soa_struct_len(lbProcedure *p, lbValue value) {
	Type *t = base_type(value.type);
	bool is_ptr = false;
	if (is_type_pointer(t)) {
		is_ptr = true;
		t = base_type(type_deref(t));
	}


	if (t->Struct.soa_kind == StructSoa_Fixed) {
		return lb_const_int(p->module, t_int, t->Struct.soa_count);
	}

	GB_ASSERT(t->Struct.soa_kind == StructSoa_Slice ||
	          t->Struct.soa_kind == StructSoa_Dynamic);

	isize n = 0;
	Type *elem = base_type(t->Struct.soa_elem);
	if (elem->kind == Type_Struct) {
		n = elem->Struct.fields.count;
	} else if (elem->kind == Type_Array) {
		n = elem->Array.count;
	} else {
		GB_PANIC("Unreachable");
	}

	if (is_ptr) {
		lbValue v = lb_emit_struct_ep(p, value, cast(i32)n);
		return lb_emit_load(p, v);
	}
	return lb_emit_struct_ev(p, value, cast(i32)n);
}

lbValue lb_soa_struct_cap(lbProcedure *p, lbValue value) {
	Type *t = base_type(value.type);

	bool is_ptr = false;
	if (is_type_pointer(t)) {
		is_ptr = true;
		t = base_type(type_deref(t));
	}

	if (t->Struct.soa_kind == StructSoa_Fixed) {
		return lb_const_int(p->module, t_int, t->Struct.soa_count);
	}

	GB_ASSERT(t->Struct.soa_kind == StructSoa_Dynamic);

	isize n = 0;
	Type *elem = base_type(t->Struct.soa_elem);
	if (elem->kind == Type_Struct) {
		n = elem->Struct.fields.count+1;
	} else if (elem->kind == Type_Array) {
		n = elem->Array.count+1;
	} else {
		GB_PANIC("Unreachable");
	}

	if (is_ptr) {
		lbValue v = lb_emit_struct_ep(p, value, cast(i32)n);
		return lb_emit_load(p, v);
	}
	return lb_emit_struct_ev(p, value, cast(i32)n);
}

lbValue lb_soa_zip(lbProcedure *p, AstCallExpr *ce, TypeAndValue const &tv) {
	GB_ASSERT(ce->args.count > 0);

	auto slices = slice_make<lbValue>(temporary_allocator(), ce->args.count);
	for_array(i, slices) {
		Ast *arg = ce->args[i];
		if (arg->kind == Ast_FieldValue) {
			arg = arg->FieldValue.value;
		}
		slices[i] = lb_build_expr(p, arg);
	}

	lbValue len = lb_slice_len(p, slices[0]);
	for (isize i = 1; i < slices.count; i++) {
		lbValue other_len = lb_slice_len(p, slices[i]);
		len = lb_emit_min(p, t_int, len, other_len);
	}

	GB_ASSERT(is_type_soa_struct(tv.type));
	lbAddr res = lb_add_local_generated(p, tv.type, true);
	for_array(i, slices) {
		lbValue src = lb_slice_elem(p, slices[i]);
		lbValue dst = lb_emit_struct_ep(p, res.addr, cast(i32)i);
		lb_emit_store(p, dst, src);
	}
	lbValue len_dst = lb_emit_struct_ep(p, res.addr, cast(i32)slices.count);
	lb_emit_store(p, len_dst, len);

	return lb_addr_load(p, res);
}

lbValue lb_soa_unzip(lbProcedure *p, AstCallExpr *ce, TypeAndValue const &tv) {
	GB_ASSERT(ce->args.count == 1);

	lbValue arg = lb_build_expr(p, ce->args[0]);
	Type *t = base_type(arg.type);
	GB_ASSERT(is_type_soa_struct(t) && t->Struct.soa_kind == StructSoa_Slice);

	lbValue len = lb_soa_struct_len(p, arg);

	lbAddr res = lb_add_local_generated(p, tv.type, true);
	if (is_type_tuple(tv.type)) {
		lbValue rp = lb_addr_get_ptr(p, res);
		for (i32 i = 0; i < cast(i32)(t->Struct.fields.count-1); i++) {
			lbValue ptr = lb_emit_struct_ev(p, arg, i);
			lbAddr dst = lb_addr(lb_emit_struct_ep(p, rp, i));
			lb_fill_slice(p, dst, ptr, len);
		}
	} else {
		GB_ASSERT(is_type_slice(tv.type));
		lbValue ptr = lb_emit_struct_ev(p, arg, 0);
		lb_fill_slice(p, res, ptr, len);
	}

	return lb_addr_load(p, res);
}

void lb_emit_try_lhs_rhs(lbProcedure *p, Ast *arg, TypeAndValue const &tv, lbValue *lhs_, lbValue *rhs_) {
	lbValue lhs = {};
	lbValue rhs = {};

	lbValue value = lb_build_expr(p, arg);
	if (is_type_tuple(value.type)) {
		i32 n = cast(i32)(value.type->Tuple.variables.count-1);
		if (value.type->Tuple.variables.count == 2) {
			lhs = lb_emit_struct_ev(p, value, 0);
		} else {
			lbAddr lhs_addr = lb_add_local_generated(p, tv.type, false);
			lbValue lhs_ptr = lb_addr_get_ptr(p, lhs_addr);
			for (i32 i = 0; i < n; i++) {
				lb_emit_store(p, lb_emit_struct_ep(p, lhs_ptr, i), lb_emit_struct_ev(p, value, i));
			}
			lhs = lb_addr_load(p, lhs_addr);
		}
		rhs = lb_emit_struct_ev(p, value, n);
	} else {
		rhs = value;
	}

	GB_ASSERT(rhs.value != nullptr);

	if (lhs_) *lhs_ = lhs;
	if (rhs_) *rhs_ = rhs;
}


lbValue lb_emit_try_has_value(lbProcedure *p, lbValue rhs) {
	lbValue has_value = {};
	if (is_type_boolean(rhs.type)) {
		has_value = rhs;
	} else {
		GB_ASSERT_MSG(type_has_nil(rhs.type), "%s", type_to_string(rhs.type));
		has_value = lb_emit_comp_against_nil(p, Token_CmpEq, rhs);
	}
	GB_ASSERT(has_value.value != nullptr);
	return has_value;
}
// lbValue lb_emit_try(lbProcedure *p, Ast *arg, TypeAndValue const &tv) {
// 	lbValue lhs = {};
// 	lbValue rhs = {};
// 	lb_emit_try_lhs_rhs(p, arg, tv, &lhs, &rhs);

// 	lbBlock *return_block = lb_create_block(p, "try.return", false);
// 	lbBlock *continue_block = lb_create_block(p, "try.continue", false);
// 	lb_emit_if(p, lb_emit_try_has_value(p, rhs), continue_block, return_block);
// 	lb_start_block(p, return_block);

// 	{
// 		Type *proc_type = base_type(p->type);
// 		Type *results = proc_type->Proc.results;
// 		GB_ASSERT(results != nullptr && results->kind == Type_Tuple);
// 		TypeTuple *tuple = &results->Tuple;

// 		GB_ASSERT(tuple->variables.count != 0);

// 		Entity *end_entity = tuple->variables[tuple->variables.count-1];
// 		rhs = lb_emit_conv(p, rhs, end_entity->type);
// 		if (p->type->Proc.has_named_results) {
// 			GB_ASSERT(end_entity->token.string.len != 0);

// 			// NOTE(bill): store the named values before returning
// 			lbValue found = map_must_get(&p->module->values, hash_entity(end_entity));
// 			lb_emit_store(p, found, rhs);

// 			lb_build_return_stmt(p, {});
// 		} else {
// 			GB_ASSERT(tuple->variables.count == 1);
// 			lb_build_return_stmt_internal(p, rhs);
// 		}
// 	}

// 	lb_start_block(p, continue_block);

// 	if (tv.type != nullptr) {
// 		return lb_emit_conv(p, lhs, tv.type);
// 	}
// 	return {};
// }


lbValue lb_emit_or_else(lbProcedure *p, Ast *arg, Ast *else_expr, TypeAndValue const &tv) {
	lbValue lhs = {};
	lbValue rhs = {};
	lb_emit_try_lhs_rhs(p, arg, tv, &lhs, &rhs);

	LLVMValueRef incoming_values[2] = {};
	LLVMBasicBlockRef incoming_blocks[2] = {};

	GB_ASSERT(else_expr != nullptr);
	lbBlock *then  = lb_create_block(p, "or_else.then");
	lbBlock *done  = lb_create_block(p, "or_else.done"); // NOTE(bill): Append later
	lbBlock *else_ = lb_create_block(p, "or_else.else");

	lb_emit_if(p, lb_emit_try_has_value(p, rhs), then, else_);
	lb_start_block(p, then);

	Type *type = default_type(tv.type);

	incoming_values[0] = lb_emit_conv(p, lhs, type).value;

	lb_emit_jump(p, done);
	lb_start_block(p, else_);

	incoming_values[1] = lb_emit_conv(p, lb_build_expr(p, else_expr), type).value;

	lb_emit_jump(p, done);
	lb_start_block(p, done);

	lbValue res = {};
	res.value = LLVMBuildPhi(p->builder, lb_type(p->module, type), "");
	res.type = type;

	GB_ASSERT(p->curr_block->preds.count >= 2);
	incoming_blocks[0] = p->curr_block->preds[0]->block;
	incoming_blocks[1] = p->curr_block->preds[1]->block;

	LLVMAddIncoming(res.value, incoming_values, incoming_blocks, 2);

	return res;
}



lbValue lb_build_builtin_proc(lbProcedure *p, Ast *expr, TypeAndValue const &tv, BuiltinProcId id) {
	ast_node(ce, CallExpr, expr);

	switch (id) {
	case BuiltinProc_DIRECTIVE: {
		ast_node(bd, BasicDirective, ce->proc);
		String name = bd->name.string;
		GB_ASSERT(name == "location");
		String procedure = p->entity->token.string;
		TokenPos pos = ast_token(ce->proc).pos;
		if (ce->args.count > 0) {
			Ast *ident = unselector_expr(ce->args[0]);
			GB_ASSERT(ident->kind == Ast_Ident);
			Entity *e = entity_of_node(ident);
			GB_ASSERT(e != nullptr);

			if (e->parent_proc_decl != nullptr && e->parent_proc_decl->entity != nullptr) {
				procedure = e->parent_proc_decl->entity->token.string;
			} else {
				procedure = str_lit("");
			}
			pos = e->token.pos;

		}
		return lb_emit_source_code_location(p, procedure, pos);
	}

	case BuiltinProc_type_info_of: {
		Ast *arg = ce->args[0];
		TypeAndValue tav = type_and_value_of_expr(arg);
		if (tav.mode == Addressing_Type) {
			Type *t = default_type(type_of_expr(arg));
			return lb_type_info(p->module, t);
		}
		GB_ASSERT(is_type_typeid(tav.type));

		auto args = array_make<lbValue>(permanent_allocator(), 1);
		args[0] = lb_build_expr(p, arg);
		return lb_emit_runtime_call(p, "__type_info_of", args);
	}

	case BuiltinProc_typeid_of: {
		Ast *arg = ce->args[0];
		TypeAndValue tav = type_and_value_of_expr(arg);
		GB_ASSERT(tav.mode == Addressing_Type);
		Type *t = default_type(type_of_expr(arg));
		return lb_typeid(p->module, t);
	}

	case BuiltinProc_len: {
		lbValue v = lb_build_expr(p, ce->args[0]);
		Type *t = base_type(v.type);
		if (is_type_pointer(t)) {
			// IMPORTANT TODO(bill): Should there be a nil pointer check?
			v = lb_emit_load(p, v);
			t = type_deref(t);
		}
		if (is_type_cstring(t)) {
			return lb_cstring_len(p, v);
		} else if (is_type_string(t)) {
			return lb_string_len(p, v);
		} else if (is_type_array(t)) {
			GB_PANIC("Array lengths are constant");
		} else if (is_type_slice(t)) {
			return lb_slice_len(p, v);
		} else if (is_type_dynamic_array(t)) {
			return lb_dynamic_array_len(p, v);
		} else if (is_type_map(t)) {
			return lb_map_len(p, v);
		} else if (is_type_soa_struct(t)) {
			return lb_soa_struct_len(p, v);
		}

		GB_PANIC("Unreachable");
		break;
	}

	case BuiltinProc_cap: {
		lbValue v = lb_build_expr(p, ce->args[0]);
		Type *t = base_type(v.type);
		if (is_type_pointer(t)) {
			// IMPORTANT TODO(bill): Should there be a nil pointer check?
			v = lb_emit_load(p, v);
			t = type_deref(t);
		}
		if (is_type_string(t)) {
			GB_PANIC("Unreachable");
		} else if (is_type_array(t)) {
			GB_PANIC("Array lengths are constant");
		} else if (is_type_slice(t)) {
			return lb_slice_len(p, v);
		} else if (is_type_dynamic_array(t)) {
			return lb_dynamic_array_cap(p, v);
		} else if (is_type_map(t)) {
			return lb_map_cap(p, v);
		} else if (is_type_soa_struct(t)) {
			return lb_soa_struct_cap(p, v);
		}

		GB_PANIC("Unreachable");

		break;
	}

	case BuiltinProc_swizzle: {
		isize index_count = ce->args.count-1;
		if (is_type_simd_vector(tv.type)) {
			lbValue vec = lb_build_expr(p, ce->args[0]);
			if (index_count == 0) {
				return vec;
			}

			unsigned mask_len = cast(unsigned)index_count;
			LLVMValueRef *mask_elems = gb_alloc_array(permanent_allocator(), LLVMValueRef, index_count);
			for (isize i = 1; i < ce->args.count; i++) {
				TypeAndValue tv = type_and_value_of_expr(ce->args[i]);
				GB_ASSERT(is_type_integer(tv.type));
				GB_ASSERT(tv.value.kind == ExactValue_Integer);

				u32 index = cast(u32)big_int_to_i64(&tv.value.value_integer);
				mask_elems[i-1] = LLVMConstInt(lb_type(p->module, t_u32), index, false);
			}

			LLVMValueRef mask = LLVMConstVector(mask_elems, mask_len);

			LLVMValueRef v1 = vec.value;
			LLVMValueRef v2 = vec.value;

			lbValue res = {};
			res.type = tv.type;
			res.value = LLVMBuildShuffleVector(p->builder, v1, v2, mask, "");
			return res;

		}

		lbAddr addr = lb_build_addr(p, ce->args[0]);
		if (index_count == 0) {
			return lb_addr_load(p, addr);
		}
		lbValue src = lb_addr_get_ptr(p, addr);
		// TODO(bill): Should this be zeroed or not?
		lbAddr dst = lb_add_local_generated(p, tv.type, true);
		lbValue dst_ptr = lb_addr_get_ptr(p, dst);

		for (i32 i = 1; i < ce->args.count; i++) {
			TypeAndValue tv = type_and_value_of_expr(ce->args[i]);
			GB_ASSERT(is_type_integer(tv.type));
			GB_ASSERT(tv.value.kind == ExactValue_Integer);

			i32 src_index = cast(i32)big_int_to_i64(&tv.value.value_integer);
			i32 dst_index = i-1;

			lbValue src_elem = lb_emit_array_epi(p, src, src_index);
			lbValue dst_elem = lb_emit_array_epi(p, dst_ptr, dst_index);

			lb_emit_store(p, dst_elem, lb_emit_load(p, src_elem));
		}
		return lb_addr_load(p, dst);
	}

	case BuiltinProc_complex: {
		lbValue real = lb_build_expr(p, ce->args[0]);
		lbValue imag = lb_build_expr(p, ce->args[1]);
		lbAddr dst_addr = lb_add_local_generated(p, tv.type, false);
		lbValue dst = lb_addr_get_ptr(p, dst_addr);

		Type *ft = base_complex_elem_type(tv.type);
		real = lb_emit_conv(p, real, ft);
		imag = lb_emit_conv(p, imag, ft);
		lb_emit_store(p, lb_emit_struct_ep(p, dst, 0), real);
		lb_emit_store(p, lb_emit_struct_ep(p, dst, 1), imag);

		return lb_emit_load(p, dst);
	}

	case BuiltinProc_quaternion: {
		lbValue real = lb_build_expr(p, ce->args[0]);
		lbValue imag = lb_build_expr(p, ce->args[1]);
		lbValue jmag = lb_build_expr(p, ce->args[2]);
		lbValue kmag = lb_build_expr(p, ce->args[3]);

		// @QuaternionLayout
		lbAddr dst_addr = lb_add_local_generated(p, tv.type, false);
		lbValue dst = lb_addr_get_ptr(p, dst_addr);

		Type *ft = base_complex_elem_type(tv.type);
		real = lb_emit_conv(p, real, ft);
		imag = lb_emit_conv(p, imag, ft);
		jmag = lb_emit_conv(p, jmag, ft);
		kmag = lb_emit_conv(p, kmag, ft);
		lb_emit_store(p, lb_emit_struct_ep(p, dst, 3), real);
		lb_emit_store(p, lb_emit_struct_ep(p, dst, 0), imag);
		lb_emit_store(p, lb_emit_struct_ep(p, dst, 1), jmag);
		lb_emit_store(p, lb_emit_struct_ep(p, dst, 2), kmag);

		return lb_emit_load(p, dst);
	}

	case BuiltinProc_real: {
		lbValue val = lb_build_expr(p, ce->args[0]);
		if (is_type_complex(val.type)) {
			lbValue real = lb_emit_struct_ev(p, val, 0);
			return lb_emit_conv(p, real, tv.type);
		} else if (is_type_quaternion(val.type)) {
			// @QuaternionLayout
			lbValue real = lb_emit_struct_ev(p, val, 3);
			return lb_emit_conv(p, real, tv.type);
		}
		GB_PANIC("invalid type for real");
		return {};
	}
	case BuiltinProc_imag: {
		lbValue val = lb_build_expr(p, ce->args[0]);
		if (is_type_complex(val.type)) {
			lbValue imag = lb_emit_struct_ev(p, val, 1);
			return lb_emit_conv(p, imag, tv.type);
		} else if (is_type_quaternion(val.type)) {
			// @QuaternionLayout
			lbValue imag = lb_emit_struct_ev(p, val, 0);
			return lb_emit_conv(p, imag, tv.type);
		}
		GB_PANIC("invalid type for imag");
		return {};
	}
	case BuiltinProc_jmag: {
		lbValue val = lb_build_expr(p, ce->args[0]);
		if (is_type_quaternion(val.type)) {
			// @QuaternionLayout
			lbValue imag = lb_emit_struct_ev(p, val, 1);
			return lb_emit_conv(p, imag, tv.type);
		}
		GB_PANIC("invalid type for jmag");
		return {};
	}
	case BuiltinProc_kmag: {
		lbValue val = lb_build_expr(p, ce->args[0]);
		if (is_type_quaternion(val.type)) {
			// @QuaternionLayout
			lbValue imag = lb_emit_struct_ev(p, val, 2);
			return lb_emit_conv(p, imag, tv.type);
		}
		GB_PANIC("invalid type for kmag");
		return {};
	}

	case BuiltinProc_conj: {
		lbValue val = lb_build_expr(p, ce->args[0]);
		lbValue res = {};
		Type *t = val.type;
		if (is_type_complex(t)) {
			res = lb_addr_get_ptr(p, lb_add_local_generated(p, tv.type, false));
			lbValue real = lb_emit_struct_ev(p, val, 0);
			lbValue imag = lb_emit_struct_ev(p, val, 1);
			imag = lb_emit_unary_arith(p, Token_Sub, imag, imag.type);
			lb_emit_store(p, lb_emit_struct_ep(p, res, 0), real);
			lb_emit_store(p, lb_emit_struct_ep(p, res, 1), imag);
		} else if (is_type_quaternion(t)) {
			// @QuaternionLayout
			res = lb_addr_get_ptr(p, lb_add_local_generated(p, tv.type, false));
			lbValue real = lb_emit_struct_ev(p, val, 3);
			lbValue imag = lb_emit_struct_ev(p, val, 0);
			lbValue jmag = lb_emit_struct_ev(p, val, 1);
			lbValue kmag = lb_emit_struct_ev(p, val, 2);
			imag = lb_emit_unary_arith(p, Token_Sub, imag, imag.type);
			jmag = lb_emit_unary_arith(p, Token_Sub, jmag, jmag.type);
			kmag = lb_emit_unary_arith(p, Token_Sub, kmag, kmag.type);
			lb_emit_store(p, lb_emit_struct_ep(p, res, 3), real);
			lb_emit_store(p, lb_emit_struct_ep(p, res, 0), imag);
			lb_emit_store(p, lb_emit_struct_ep(p, res, 1), jmag);
			lb_emit_store(p, lb_emit_struct_ep(p, res, 2), kmag);
		}
		return lb_emit_load(p, res);
	}

	case BuiltinProc_expand_to_tuple: {
		lbValue val = lb_build_expr(p, ce->args[0]);
		Type *t = base_type(val.type);

		if (!is_type_tuple(tv.type)) {
			if (t->kind == Type_Struct) {
				GB_ASSERT(t->Struct.fields.count == 1);
				return lb_emit_struct_ev(p, val, 0);
			} else if (t->kind == Type_Array) {
				GB_ASSERT(t->Array.count == 1);
				return lb_emit_array_epi(p, val, 0);
			} else {
				GB_PANIC("Unknown type of expand_to_tuple");
			}

		}

		GB_ASSERT(is_type_tuple(tv.type));
		// NOTE(bill): Doesn't need to be zero because it will be initialized in the loops
		lbValue tuple = lb_addr_get_ptr(p, lb_add_local_generated(p, tv.type, false));
		if (t->kind == Type_Struct) {
			for_array(src_index, t->Struct.fields) {
				Entity *field = t->Struct.fields[src_index];
				i32 field_index = field->Variable.field_index;
				lbValue f = lb_emit_struct_ev(p, val, field_index);
				lbValue ep = lb_emit_struct_ep(p, tuple, cast(i32)src_index);
				lb_emit_store(p, ep, f);
			}
		} else if (is_type_array_like(t)) {
			// TODO(bill): Clean-up this code
			lbValue ap = lb_address_from_load_or_generate_local(p, val);
			i32 n = cast(i32)get_array_type_count(t);
			for (i32 i = 0; i < n; i++) {
				lbValue f = lb_emit_load(p, lb_emit_array_epi(p, ap, i));
				lbValue ep = lb_emit_struct_ep(p, tuple, i);
				lb_emit_store(p, ep, f);
			}
		} else {
			GB_PANIC("Unknown type of expand_to_tuple");
		}
		return lb_emit_load(p, tuple);
	}

	case BuiltinProc_min: {
		Type *t = type_of_expr(expr);
		if (ce->args.count == 2) {
			return lb_emit_min(p, t, lb_build_expr(p, ce->args[0]), lb_build_expr(p, ce->args[1]));
		} else {
			lbValue x = lb_build_expr(p, ce->args[0]);
			for (isize i = 1; i < ce->args.count; i++) {
				x = lb_emit_min(p, t, x, lb_build_expr(p, ce->args[i]));
			}
			return x;
		}
	}

	case BuiltinProc_max: {
		Type *t = type_of_expr(expr);
		if (ce->args.count == 2) {
			return lb_emit_max(p, t, lb_build_expr(p, ce->args[0]), lb_build_expr(p, ce->args[1]));
		} else {
			lbValue x = lb_build_expr(p, ce->args[0]);
			for (isize i = 1; i < ce->args.count; i++) {
				x = lb_emit_max(p, t, x, lb_build_expr(p, ce->args[i]));
			}
			return x;
		}
	}

	case BuiltinProc_abs: {
		lbValue x = lb_build_expr(p, ce->args[0]);
		Type *t = x.type;
		if (is_type_unsigned(t)) {
			return x;
		}
		if (is_type_quaternion(t)) {
			i64 sz = 8*type_size_of(t);
			auto args = array_make<lbValue>(permanent_allocator(), 1);
			args[0] = x;
			switch (sz) {
			case 64:  return lb_emit_runtime_call(p, "abs_quaternion64", args);
			case 128: return lb_emit_runtime_call(p, "abs_quaternion128", args);
			case 256: return lb_emit_runtime_call(p, "abs_quaternion256", args);
			}
			GB_PANIC("Unknown complex type");
		} else if (is_type_complex(t)) {
			i64 sz = 8*type_size_of(t);
			auto args = array_make<lbValue>(permanent_allocator(), 1);
			args[0] = x;
			switch (sz) {
			case 32:  return lb_emit_runtime_call(p, "abs_complex32",  args);
			case 64:  return lb_emit_runtime_call(p, "abs_complex64",  args);
			case 128: return lb_emit_runtime_call(p, "abs_complex128", args);
			}
			GB_PANIC("Unknown complex type");
		}
		lbValue zero = lb_const_nil(p->module, t);
		lbValue cond = lb_emit_comp(p, Token_Lt, x, zero);
		lbValue neg = lb_emit_unary_arith(p, Token_Sub, x, t);
		return lb_emit_select(p, cond, neg, x);
	}

	case BuiltinProc_clamp:
		return lb_emit_clamp(p, type_of_expr(expr),
		                     lb_build_expr(p, ce->args[0]),
		                     lb_build_expr(p, ce->args[1]),
		                     lb_build_expr(p, ce->args[2]));


	case BuiltinProc_soa_zip:
		return lb_soa_zip(p, ce, tv);
	case BuiltinProc_soa_unzip:
		return lb_soa_unzip(p, ce, tv);

	case BuiltinProc_or_else:
		return lb_emit_or_else(p, ce->args[0], ce->args[1], tv);

	// "Intrinsics"

	case BuiltinProc_alloca:
		{
			lbValue sz = lb_build_expr(p, ce->args[0]);
			i64 al = exact_value_to_i64(type_and_value_of_expr(ce->args[1]).value);

			lbValue res = {};
			res.type = t_u8_ptr;
			res.value = LLVMBuildArrayAlloca(p->builder, lb_type(p->module, t_u8), sz.value, "");
			LLVMSetAlignment(res.value, cast(unsigned)al);
			return res;
		}

	case BuiltinProc_cpu_relax:
		if (build_context.metrics.arch == TargetArch_386 ||
		    build_context.metrics.arch == TargetArch_amd64) {
			LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidTypeInContext(p->module->ctx), nullptr, 0, false);
			LLVMValueRef the_asm = LLVMGetInlineAsm(func_type,
				cast(char *)"pause", 5,
				cast(char *)"", 0,
				/*HasSideEffects*/true, /*IsAlignStack*/false,
				LLVMInlineAsmDialectATT
			);
			GB_ASSERT(the_asm != nullptr);
			LLVMBuildCall2(p->builder, func_type, the_asm, nullptr, 0, "");
		} else if (build_context.metrics.arch == TargetArch_arm64) {
			LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidTypeInContext(p->module->ctx), nullptr, 0, false);
			LLVMValueRef the_asm = LLVMGetInlineAsm(func_type,
				cast(char *)"yield", 5,
				cast(char *)"", 0,
				/*HasSideEffects*/true, /*IsAlignStack*/false,
				LLVMInlineAsmDialectATT
			);
			GB_ASSERT(the_asm != nullptr);
			LLVMBuildCall2(p->builder, func_type, the_asm, nullptr, 0, "");
		}
		return {};


	case BuiltinProc_debug_trap:
	case BuiltinProc_trap:
		{
			char const *name = nullptr;
			switch (id) {
			case BuiltinProc_debug_trap: name = "llvm.debugtrap"; break;
			case BuiltinProc_trap:       name = "llvm.trap";      break;
			}

			unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
			GB_ASSERT_MSG(id != 0, "Unable to find %s", name);
			LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, nullptr, 0);

			LLVMBuildCall(p->builder, ip, nullptr, 0, "");
			if (id == BuiltinProc_trap) {
				LLVMBuildUnreachable(p->builder);
			}
			return {};
		}

	case BuiltinProc_read_cycle_counter:
		{
			char const *name = "llvm.readcyclecounter";
			unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
			GB_ASSERT_MSG(id != 0, "Unable to find %s", name);
			LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, nullptr, 0);

			lbValue res = {};
			res.value = LLVMBuildCall(p->builder, ip, nullptr, 0, "");
			res.type = tv.type;
			return res;
		}

	case BuiltinProc_count_trailing_zeros:
		return lb_emit_count_trailing_zeros(p, lb_build_expr(p, ce->args[0]), tv.type);
	case BuiltinProc_count_leading_zeros:
		return lb_emit_count_leading_zeros(p, lb_build_expr(p, ce->args[0]), tv.type);

	case BuiltinProc_count_ones:
		return lb_emit_count_ones(p, lb_build_expr(p, ce->args[0]), tv.type);
	case BuiltinProc_count_zeros:
		return lb_emit_count_zeros(p, lb_build_expr(p, ce->args[0]), tv.type);

	case BuiltinProc_reverse_bits:
		return lb_emit_reverse_bits(p, lb_build_expr(p, ce->args[0]), tv.type);

	case BuiltinProc_byte_swap:
		{
			lbValue x = lb_build_expr(p, ce->args[0]);
			x = lb_emit_conv(p, x, tv.type);
			return lb_emit_byte_swap(p, x, tv.type);
		}

	case BuiltinProc_overflow_add:
	case BuiltinProc_overflow_sub:
	case BuiltinProc_overflow_mul:
		{
			Type *main_type = tv.type;
			Type *type = main_type;
			if (is_type_tuple(main_type)) {
				type = main_type->Tuple.variables[0]->type;
			}

			lbValue x = lb_build_expr(p, ce->args[0]);
			lbValue y = lb_build_expr(p, ce->args[1]);
			x = lb_emit_conv(p, x, type);
			y = lb_emit_conv(p, y, type);

			char const *name = nullptr;
			if (is_type_unsigned(type)) {
				switch (id) {
				case BuiltinProc_overflow_add: name = "llvm.uadd.with.overflow"; break;
				case BuiltinProc_overflow_sub: name = "llvm.usub.with.overflow"; break;
				case BuiltinProc_overflow_mul: name = "llvm.umul.with.overflow"; break;
				}
			} else {
				switch (id) {
				case BuiltinProc_overflow_add: name = "llvm.sadd.with.overflow"; break;
				case BuiltinProc_overflow_sub: name = "llvm.ssub.with.overflow"; break;
				case BuiltinProc_overflow_mul: name = "llvm.smul.with.overflow"; break;
				}
			}
			LLVMTypeRef types[1] = {lb_type(p->module, type)};
			unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
			GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
			LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

			LLVMValueRef args[2] = {};
			args[0] = x.value;
			args[1] = y.value;

			lbValue res = {};
			res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");

			if (is_type_tuple(main_type)) {
				Type *res_type = nullptr;
				gbAllocator a = permanent_allocator();
				res_type = alloc_type_tuple();
				array_init(&res_type->Tuple.variables, a, 2);
				res_type->Tuple.variables[0] = alloc_entity_field(nullptr, blank_token, type,        false, 0);
				res_type->Tuple.variables[1] = alloc_entity_field(nullptr, blank_token, t_llvm_bool, false, 1);

				res.type = res_type;
			} else {
				res.value = LLVMBuildExtractValue(p->builder, res.value, 0, "");
				res.type = type;
			}
			return res;
		}

	case BuiltinProc_sqrt:
		{
			Type *type = tv.type;

			lbValue x = lb_build_expr(p, ce->args[0]);
			x = lb_emit_conv(p, x, type);

			char const *name = "llvm.sqrt";
			LLVMTypeRef types[1] = {lb_type(p->module, type)};
			unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
			GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
			LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

			LLVMValueRef args[1] = {};
			args[0] = x.value;

			lbValue res = {};
			res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
			res.type = type;
			return res;
		}

	case BuiltinProc_mem_copy:
	case BuiltinProc_mem_copy_non_overlapping:
		{
			lbValue dst = lb_build_expr(p, ce->args[0]);
			lbValue src = lb_build_expr(p, ce->args[1]);
			lbValue len = lb_build_expr(p, ce->args[2]);
			dst = lb_emit_conv(p, dst, t_rawptr);
			src = lb_emit_conv(p, src, t_rawptr);
			len = lb_emit_conv(p, len, t_int);

			bool is_inlinable = false;

			if (ce->args[2]->tav.mode == Addressing_Constant) {
				ExactValue ev = exact_value_to_integer(ce->args[2]->tav.value);
				i64 const_len = exact_value_to_i64(ev);
				// TODO(bill): Determine when it is better to do the `*.inline` versions
				if (const_len <= 4*build_context.word_size) {
					is_inlinable = true;
				}
			}

			char const *name = nullptr;
			switch (id) {
			case BuiltinProc_mem_copy:
				if (is_inlinable) {
					name = "llvm.memmove.inline";
				} else {
					name = "llvm.memmove";
				}
				break;
			case BuiltinProc_mem_copy_non_overlapping:
				if (is_inlinable) {
					name = "llvm.memcpy.line";
				} else {
					name = "llvm.memcpy";
				}
				break;
			}

			LLVMTypeRef types[3] = {
				lb_type(p->module, t_rawptr),
				lb_type(p->module, t_rawptr),
				lb_type(p->module, t_int)
			};
			unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
			GB_ASSERT_MSG(id != 0, "Unable to find %s.%s.%s.%s", name, LLVMPrintTypeToString(types[0]), LLVMPrintTypeToString(types[1]), LLVMPrintTypeToString(types[2]));
			LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

			LLVMValueRef args[4] = {};
			args[0] = dst.value;
			args[1] = src.value;
			args[2] = len.value;
			args[3] = LLVMConstInt(LLVMInt1TypeInContext(p->module->ctx), 0, false); // is_volatile parameter

			LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");

			return {};
		}

	case BuiltinProc_mem_zero:
		{
			lbValue ptr = lb_build_expr(p, ce->args[0]);
			lbValue len = lb_build_expr(p, ce->args[1]);
			ptr = lb_emit_conv(p, ptr, t_rawptr);
			len = lb_emit_conv(p, len, t_int);

			unsigned alignment = 1;
			lb_mem_zero_ptr_internal(p, ptr.value, len.value, alignment);
			return {};
		}

	case BuiltinProc_ptr_offset:
		{
			lbValue ptr = lb_build_expr(p, ce->args[0]);
			lbValue len = lb_build_expr(p, ce->args[1]);
			len = lb_emit_conv(p, len, t_int);

			LLVMValueRef indices[1] = {
				len.value,
			};

			lbValue res = {};
			res.type = tv.type;
			res.value = LLVMBuildGEP(p->builder, ptr.value, indices, gb_count_of(indices), "");
			return res;
		}
	case BuiltinProc_ptr_sub:
		{
			lbValue ptr0 = lb_build_expr(p, ce->args[0]);
			lbValue ptr1 = lb_build_expr(p, ce->args[1]);

			LLVMTypeRef type_int = lb_type(p->module, t_int);
			LLVMValueRef diff = LLVMBuildPtrDiff(p->builder, ptr0.value, ptr1.value, "");
			diff = LLVMBuildIntCast2(p->builder, diff, type_int, /*signed*/true, "");

			lbValue res = {};
			res.type = t_int;
			res.value = diff;
			return res;
		}



	case BuiltinProc_atomic_fence:
		LLVMBuildFence(p->builder, LLVMAtomicOrderingSequentiallyConsistent, false, "");
		return {};
	case BuiltinProc_atomic_fence_acq:
		LLVMBuildFence(p->builder, LLVMAtomicOrderingAcquire, false, "");
		return {};
	case BuiltinProc_atomic_fence_rel:
		LLVMBuildFence(p->builder, LLVMAtomicOrderingRelease, false, "");
		return {};
	case BuiltinProc_atomic_fence_acqrel:
		LLVMBuildFence(p->builder, LLVMAtomicOrderingAcquireRelease, false, "");
		return {};

	case BuiltinProc_volatile_store:
	case BuiltinProc_atomic_store:
	case BuiltinProc_atomic_store_rel:
	case BuiltinProc_atomic_store_relaxed:
	case BuiltinProc_atomic_store_unordered: {
		lbValue dst = lb_build_expr(p, ce->args[0]);
		lbValue val = lb_build_expr(p, ce->args[1]);
		val = lb_emit_conv(p, val, type_deref(dst.type));

		LLVMValueRef instr = LLVMBuildStore(p->builder, val.value, dst.value);
		switch (id) {
		case BuiltinProc_volatile_store:         LLVMSetVolatile(instr, true);                                     break;
		case BuiltinProc_atomic_store:           LLVMSetOrdering(instr, LLVMAtomicOrderingSequentiallyConsistent); break;
		case BuiltinProc_atomic_store_rel:       LLVMSetOrdering(instr, LLVMAtomicOrderingRelease);                break;
		case BuiltinProc_atomic_store_relaxed:   LLVMSetOrdering(instr, LLVMAtomicOrderingMonotonic);              break;
		case BuiltinProc_atomic_store_unordered: LLVMSetOrdering(instr, LLVMAtomicOrderingUnordered);              break;
		}

		LLVMSetAlignment(instr, cast(unsigned)type_align_of(type_deref(dst.type)));

		return {};
	}

	case BuiltinProc_volatile_load:
	case BuiltinProc_atomic_load:
	case BuiltinProc_atomic_load_acq:
	case BuiltinProc_atomic_load_relaxed:
	case BuiltinProc_atomic_load_unordered: {
		lbValue dst = lb_build_expr(p, ce->args[0]);

		LLVMValueRef instr = LLVMBuildLoad(p->builder, dst.value, "");
		switch (id) {
		case BuiltinProc_volatile_load:         LLVMSetVolatile(instr, true);                                     break;
		case BuiltinProc_atomic_load:           LLVMSetOrdering(instr, LLVMAtomicOrderingSequentiallyConsistent); break;
		case BuiltinProc_atomic_load_acq:       LLVMSetOrdering(instr, LLVMAtomicOrderingAcquire);                break;
		case BuiltinProc_atomic_load_relaxed:   LLVMSetOrdering(instr, LLVMAtomicOrderingMonotonic);              break;
		case BuiltinProc_atomic_load_unordered: LLVMSetOrdering(instr, LLVMAtomicOrderingUnordered);              break;
		}
		LLVMSetAlignment(instr, cast(unsigned)type_align_of(type_deref(dst.type)));

		lbValue res = {};
		res.value = instr;
		res.type = type_deref(dst.type);
		return res;
	}

	case BuiltinProc_atomic_add:
	case BuiltinProc_atomic_add_acq:
	case BuiltinProc_atomic_add_rel:
	case BuiltinProc_atomic_add_acqrel:
	case BuiltinProc_atomic_add_relaxed:
	case BuiltinProc_atomic_sub:
	case BuiltinProc_atomic_sub_acq:
	case BuiltinProc_atomic_sub_rel:
	case BuiltinProc_atomic_sub_acqrel:
	case BuiltinProc_atomic_sub_relaxed:
	case BuiltinProc_atomic_and:
	case BuiltinProc_atomic_and_acq:
	case BuiltinProc_atomic_and_rel:
	case BuiltinProc_atomic_and_acqrel:
	case BuiltinProc_atomic_and_relaxed:
	case BuiltinProc_atomic_nand:
	case BuiltinProc_atomic_nand_acq:
	case BuiltinProc_atomic_nand_rel:
	case BuiltinProc_atomic_nand_acqrel:
	case BuiltinProc_atomic_nand_relaxed:
	case BuiltinProc_atomic_or:
	case BuiltinProc_atomic_or_acq:
	case BuiltinProc_atomic_or_rel:
	case BuiltinProc_atomic_or_acqrel:
	case BuiltinProc_atomic_or_relaxed:
	case BuiltinProc_atomic_xor:
	case BuiltinProc_atomic_xor_acq:
	case BuiltinProc_atomic_xor_rel:
	case BuiltinProc_atomic_xor_acqrel:
	case BuiltinProc_atomic_xor_relaxed:
	case BuiltinProc_atomic_xchg:
	case BuiltinProc_atomic_xchg_acq:
	case BuiltinProc_atomic_xchg_rel:
	case BuiltinProc_atomic_xchg_acqrel:
	case BuiltinProc_atomic_xchg_relaxed: {
		lbValue dst = lb_build_expr(p, ce->args[0]);
		lbValue val = lb_build_expr(p, ce->args[1]);
		val = lb_emit_conv(p, val, type_deref(dst.type));

		LLVMAtomicRMWBinOp op = {};
		LLVMAtomicOrdering ordering = {};

		switch (id) {
		case BuiltinProc_atomic_add:          op = LLVMAtomicRMWBinOpAdd;  ordering = LLVMAtomicOrderingSequentiallyConsistent; break;
		case BuiltinProc_atomic_add_acq:      op = LLVMAtomicRMWBinOpAdd;  ordering = LLVMAtomicOrderingAcquire; break;
		case BuiltinProc_atomic_add_rel:      op = LLVMAtomicRMWBinOpAdd;  ordering = LLVMAtomicOrderingRelease; break;
		case BuiltinProc_atomic_add_acqrel:   op = LLVMAtomicRMWBinOpAdd;  ordering = LLVMAtomicOrderingAcquireRelease; break;
		case BuiltinProc_atomic_add_relaxed:  op = LLVMAtomicRMWBinOpAdd;  ordering = LLVMAtomicOrderingMonotonic; break;
		case BuiltinProc_atomic_sub:          op = LLVMAtomicRMWBinOpSub;  ordering = LLVMAtomicOrderingSequentiallyConsistent; break;
		case BuiltinProc_atomic_sub_acq:      op = LLVMAtomicRMWBinOpSub;  ordering = LLVMAtomicOrderingAcquire; break;
		case BuiltinProc_atomic_sub_rel:      op = LLVMAtomicRMWBinOpSub;  ordering = LLVMAtomicOrderingRelease; break;
		case BuiltinProc_atomic_sub_acqrel:   op = LLVMAtomicRMWBinOpSub;  ordering = LLVMAtomicOrderingAcquireRelease; break;
		case BuiltinProc_atomic_sub_relaxed:  op = LLVMAtomicRMWBinOpSub;  ordering = LLVMAtomicOrderingMonotonic; break;
		case BuiltinProc_atomic_and:          op = LLVMAtomicRMWBinOpAnd;  ordering = LLVMAtomicOrderingSequentiallyConsistent; break;
		case BuiltinProc_atomic_and_acq:      op = LLVMAtomicRMWBinOpAnd;  ordering = LLVMAtomicOrderingAcquire; break;
		case BuiltinProc_atomic_and_rel:      op = LLVMAtomicRMWBinOpAnd;  ordering = LLVMAtomicOrderingRelease; break;
		case BuiltinProc_atomic_and_acqrel:   op = LLVMAtomicRMWBinOpAnd;  ordering = LLVMAtomicOrderingAcquireRelease; break;
		case BuiltinProc_atomic_and_relaxed:  op = LLVMAtomicRMWBinOpAnd;  ordering = LLVMAtomicOrderingMonotonic; break;
		case BuiltinProc_atomic_nand:         op = LLVMAtomicRMWBinOpNand; ordering = LLVMAtomicOrderingSequentiallyConsistent; break;
		case BuiltinProc_atomic_nand_acq:     op = LLVMAtomicRMWBinOpNand; ordering = LLVMAtomicOrderingAcquire; break;
		case BuiltinProc_atomic_nand_rel:     op = LLVMAtomicRMWBinOpNand; ordering = LLVMAtomicOrderingRelease; break;
		case BuiltinProc_atomic_nand_acqrel:  op = LLVMAtomicRMWBinOpNand; ordering = LLVMAtomicOrderingAcquireRelease; break;
		case BuiltinProc_atomic_nand_relaxed: op = LLVMAtomicRMWBinOpNand; ordering = LLVMAtomicOrderingMonotonic; break;
		case BuiltinProc_atomic_or:           op = LLVMAtomicRMWBinOpOr;   ordering = LLVMAtomicOrderingSequentiallyConsistent; break;
		case BuiltinProc_atomic_or_acq:       op = LLVMAtomicRMWBinOpOr;   ordering = LLVMAtomicOrderingAcquire; break;
		case BuiltinProc_atomic_or_rel:       op = LLVMAtomicRMWBinOpOr;   ordering = LLVMAtomicOrderingRelease; break;
		case BuiltinProc_atomic_or_acqrel:    op = LLVMAtomicRMWBinOpOr;   ordering = LLVMAtomicOrderingAcquireRelease; break;
		case BuiltinProc_atomic_or_relaxed:   op = LLVMAtomicRMWBinOpOr;   ordering = LLVMAtomicOrderingMonotonic; break;
		case BuiltinProc_atomic_xor:          op = LLVMAtomicRMWBinOpXor;  ordering = LLVMAtomicOrderingSequentiallyConsistent; break;
		case BuiltinProc_atomic_xor_acq:      op = LLVMAtomicRMWBinOpXor;  ordering = LLVMAtomicOrderingAcquire; break;
		case BuiltinProc_atomic_xor_rel:      op = LLVMAtomicRMWBinOpXor;  ordering = LLVMAtomicOrderingRelease; break;
		case BuiltinProc_atomic_xor_acqrel:   op = LLVMAtomicRMWBinOpXor;  ordering = LLVMAtomicOrderingAcquireRelease; break;
		case BuiltinProc_atomic_xor_relaxed:  op = LLVMAtomicRMWBinOpXor;  ordering = LLVMAtomicOrderingMonotonic; break;
		case BuiltinProc_atomic_xchg:         op = LLVMAtomicRMWBinOpXchg; ordering = LLVMAtomicOrderingSequentiallyConsistent; break;
		case BuiltinProc_atomic_xchg_acq:     op = LLVMAtomicRMWBinOpXchg; ordering = LLVMAtomicOrderingAcquire; break;
		case BuiltinProc_atomic_xchg_rel:     op = LLVMAtomicRMWBinOpXchg; ordering = LLVMAtomicOrderingRelease; break;
		case BuiltinProc_atomic_xchg_acqrel:  op = LLVMAtomicRMWBinOpXchg; ordering = LLVMAtomicOrderingAcquireRelease; break;
		case BuiltinProc_atomic_xchg_relaxed: op = LLVMAtomicRMWBinOpXchg; ordering = LLVMAtomicOrderingMonotonic; break;
		}

		lbValue res = {};
		res.value = LLVMBuildAtomicRMW(p->builder, op, dst.value, val.value, ordering, false);
		res.type = tv.type;
		return res;
	}

	case BuiltinProc_atomic_cxchg:
	case BuiltinProc_atomic_cxchg_acq:
	case BuiltinProc_atomic_cxchg_rel:
	case BuiltinProc_atomic_cxchg_acqrel:
	case BuiltinProc_atomic_cxchg_relaxed:
	case BuiltinProc_atomic_cxchg_failrelaxed:
	case BuiltinProc_atomic_cxchg_failacq:
	case BuiltinProc_atomic_cxchg_acq_failrelaxed:
	case BuiltinProc_atomic_cxchg_acqrel_failrelaxed:
	case BuiltinProc_atomic_cxchgweak:
	case BuiltinProc_atomic_cxchgweak_acq:
	case BuiltinProc_atomic_cxchgweak_rel:
	case BuiltinProc_atomic_cxchgweak_acqrel:
	case BuiltinProc_atomic_cxchgweak_relaxed:
	case BuiltinProc_atomic_cxchgweak_failrelaxed:
	case BuiltinProc_atomic_cxchgweak_failacq:
	case BuiltinProc_atomic_cxchgweak_acq_failrelaxed:
	case BuiltinProc_atomic_cxchgweak_acqrel_failrelaxed: {
		Type *type = expr->tav.type;

		lbValue address = lb_build_expr(p, ce->args[0]);
		Type *elem = type_deref(address.type);
		lbValue old_value = lb_build_expr(p, ce->args[1]);
		lbValue new_value = lb_build_expr(p, ce->args[2]);
		old_value = lb_emit_conv(p, old_value, elem);
		new_value = lb_emit_conv(p, new_value, elem);

		LLVMAtomicOrdering success_ordering = {};
		LLVMAtomicOrdering failure_ordering = {};
		LLVMBool weak = false;

		switch (id) {
		case BuiltinProc_atomic_cxchg:                        success_ordering = LLVMAtomicOrderingSequentiallyConsistent; failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = false; break;
		case BuiltinProc_atomic_cxchg_acq:                    success_ordering = LLVMAtomicOrderingAcquire;                failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = false; break;
		case BuiltinProc_atomic_cxchg_rel:                    success_ordering = LLVMAtomicOrderingRelease;                failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = false; break;
		case BuiltinProc_atomic_cxchg_acqrel:                 success_ordering = LLVMAtomicOrderingAcquireRelease;         failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = false; break;
		case BuiltinProc_atomic_cxchg_relaxed:                success_ordering = LLVMAtomicOrderingMonotonic;              failure_ordering = LLVMAtomicOrderingMonotonic;              weak = false; break;
		case BuiltinProc_atomic_cxchg_failrelaxed:            success_ordering = LLVMAtomicOrderingSequentiallyConsistent; failure_ordering = LLVMAtomicOrderingMonotonic;              weak = false; break;
		case BuiltinProc_atomic_cxchg_failacq:                success_ordering = LLVMAtomicOrderingSequentiallyConsistent; failure_ordering = LLVMAtomicOrderingAcquire;                weak = false; break;
		case BuiltinProc_atomic_cxchg_acq_failrelaxed:        success_ordering = LLVMAtomicOrderingAcquire;                failure_ordering = LLVMAtomicOrderingMonotonic;              weak = false; break;
		case BuiltinProc_atomic_cxchg_acqrel_failrelaxed:     success_ordering = LLVMAtomicOrderingAcquireRelease;         failure_ordering = LLVMAtomicOrderingMonotonic;              weak = false; break;
		case BuiltinProc_atomic_cxchgweak:                    success_ordering = LLVMAtomicOrderingSequentiallyConsistent; failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = false; break;
		case BuiltinProc_atomic_cxchgweak_acq:                success_ordering = LLVMAtomicOrderingAcquire;                failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = true;  break;
		case BuiltinProc_atomic_cxchgweak_rel:                success_ordering = LLVMAtomicOrderingRelease;                failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = true;  break;
		case BuiltinProc_atomic_cxchgweak_acqrel:             success_ordering = LLVMAtomicOrderingAcquireRelease;         failure_ordering = LLVMAtomicOrderingSequentiallyConsistent; weak = true;  break;
		case BuiltinProc_atomic_cxchgweak_relaxed:            success_ordering = LLVMAtomicOrderingMonotonic;              failure_ordering = LLVMAtomicOrderingMonotonic;              weak = true;  break;
		case BuiltinProc_atomic_cxchgweak_failrelaxed:        success_ordering = LLVMAtomicOrderingSequentiallyConsistent; failure_ordering = LLVMAtomicOrderingMonotonic;              weak = true;  break;
		case BuiltinProc_atomic_cxchgweak_failacq:            success_ordering = LLVMAtomicOrderingSequentiallyConsistent; failure_ordering = LLVMAtomicOrderingAcquire;                weak = true;  break;
		case BuiltinProc_atomic_cxchgweak_acq_failrelaxed:    success_ordering = LLVMAtomicOrderingAcquire;                failure_ordering = LLVMAtomicOrderingMonotonic;              weak = true;  break;
		case BuiltinProc_atomic_cxchgweak_acqrel_failrelaxed: success_ordering = LLVMAtomicOrderingAcquireRelease;         failure_ordering = LLVMAtomicOrderingMonotonic;              weak = true;  break;
		}

		// TODO(bill): Figure out how to make it weak
		LLVMBool single_threaded = weak;

		LLVMValueRef value = LLVMBuildAtomicCmpXchg(
			p->builder, address.value,
			old_value.value, new_value.value,
			success_ordering,
			failure_ordering,
			single_threaded
		);

		if (tv.type->kind == Type_Tuple) {
			Type *fix_typed = alloc_type_tuple();
			array_init(&fix_typed->Tuple.variables, permanent_allocator(), 2);
			fix_typed->Tuple.variables[0] = tv.type->Tuple.variables[0];
			fix_typed->Tuple.variables[1] = alloc_entity_field(nullptr, blank_token, t_llvm_bool, false, 1);

			lbValue res = {};
			res.value = value;
			res.type = fix_typed;
			return res;
		} else {
			lbValue res = {};
			res.value = LLVMBuildExtractValue(p->builder, value, 0, "");
			res.type = tv.type;
			return res;
		}
	}


	case BuiltinProc_type_equal_proc:
		return lb_get_equal_proc_for_type(p->module, ce->args[0]->tav.type);

	case BuiltinProc_type_hasher_proc:
		return lb_get_hasher_proc_for_type(p->module, ce->args[0]->tav.type);

	case BuiltinProc_fixed_point_mul:
	case BuiltinProc_fixed_point_div:
	case BuiltinProc_fixed_point_mul_sat:
	case BuiltinProc_fixed_point_div_sat:
		{
			bool do_bswap = is_type_different_to_arch_endianness(tv.type);
			Type *platform_type = integer_endian_type_to_platform_type(tv.type);

			lbValue x     = lb_emit_conv(p, lb_build_expr(p, ce->args[0]), platform_type);
			lbValue y     = lb_emit_conv(p, lb_build_expr(p, ce->args[1]), platform_type);
			lbValue scale = lb_emit_conv(p, lb_build_expr(p, ce->args[2]), t_i32);

			char const *name = nullptr;
			if (is_type_unsigned(tv.type)) {
				switch (id) {
				case BuiltinProc_fixed_point_mul:     name = "llvm.umul.fix";     break;
				case BuiltinProc_fixed_point_div:     name = "llvm.udiv.fix";     break;
				case BuiltinProc_fixed_point_mul_sat: name = "llvm.umul.fix.sat"; break;
				case BuiltinProc_fixed_point_div_sat: name = "llvm.udiv.fix.sat"; break;
				}
			} else {
				switch (id) {
				case BuiltinProc_fixed_point_mul:     name = "llvm.smul.fix";     break;
				case BuiltinProc_fixed_point_div:     name = "llvm.sdiv.fix";     break;
				case BuiltinProc_fixed_point_mul_sat: name = "llvm.smul.fix.sat"; break;
				case BuiltinProc_fixed_point_div_sat: name = "llvm.sdiv.fix.sat"; break;
				}
			}
			GB_ASSERT(name != nullptr);

			LLVMTypeRef types[1] = {lb_type(p->module, platform_type)};
			unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
			GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
			LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

			lbValue res = {};

			LLVMValueRef args[3] = {};
			args[0] = x.value;
			args[1] = y.value;
			args[2] = scale.value;

			res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
			res.type = platform_type;
			return lb_emit_conv(p, res, tv.type);
		}

	case BuiltinProc_expect:
		{
			Type *t = default_type(tv.type);
			lbValue x = lb_emit_conv(p, lb_build_expr(p, ce->args[0]), t);
			lbValue y = lb_emit_conv(p, lb_build_expr(p, ce->args[1]), t);

			char const *name = "llvm.expect";

			LLVMTypeRef types[1] = {lb_type(p->module, t)};
			unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
			GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
			LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

			lbValue res = {};

			LLVMValueRef args[2] = {};
			args[0] = x.value;
			args[1] = y.value;

			res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
			res.type = t;
			return lb_emit_conv(p, res, t);
		}
	}

	GB_PANIC("Unhandled built-in procedure %.*s", LIT(builtin_procs[id].name));
	return {};
}


lbValue lb_handle_param_value(lbProcedure *p, Type *parameter_type, ParameterValue const &param_value, TokenPos const &pos) {
	switch (param_value.kind) {
	case ParameterValue_Constant:
		if (is_type_constant_type(parameter_type)) {
			auto res = lb_const_value(p->module, parameter_type, param_value.value);
			return res;
		} else {
			ExactValue ev = param_value.value;
			lbValue arg = {};
			Type *type = type_of_expr(param_value.original_ast_expr);
			if (type != nullptr) {
				arg = lb_const_value(p->module, type, ev);
			} else {
				arg = lb_const_value(p->module, parameter_type, param_value.value);
			}
			return lb_emit_conv(p, arg, parameter_type);
		}

	case ParameterValue_Nil:
		return lb_const_nil(p->module, parameter_type);
	case ParameterValue_Location:
		{
			String proc_name = {};
			if (p->entity != nullptr) {
				proc_name = p->entity->token.string;
			}
			return lb_emit_source_code_location(p, proc_name, pos);
		}
	case ParameterValue_Value:
		return lb_build_expr(p, param_value.ast_value);
	}
	return lb_const_nil(p->module, parameter_type);
}


lbValue lb_build_call_expr_internal(lbProcedure *p, Ast *expr);

lbValue lb_build_call_expr(lbProcedure *p, Ast *expr) {
	expr = unparen_expr(expr);
	ast_node(ce, CallExpr, expr);

	if (ce->sce_temp_data) {
		return *(lbValue *)ce->sce_temp_data;
	}

	lbValue res = lb_build_call_expr_internal(p, expr);

	if (ce->optional_ok_one) { // TODO(bill): Minor hack for #optional_ok procedures
		GB_ASSERT(is_type_tuple(res.type));
		GB_ASSERT(res.type->Tuple.variables.count == 2);
		return lb_emit_struct_ev(p, res, 0);
	}
	return res;
}
lbValue lb_build_call_expr_internal(lbProcedure *p, Ast *expr) {
	lbModule *m = p->module;

	TypeAndValue tv = type_and_value_of_expr(expr);

	ast_node(ce, CallExpr, expr);

	TypeAndValue proc_tv = type_and_value_of_expr(ce->proc);
	AddressingMode proc_mode = proc_tv.mode;
	if (proc_mode == Addressing_Type) {
		GB_ASSERT(ce->args.count == 1);
		lbValue x = lb_build_expr(p, ce->args[0]);
		lbValue y = lb_emit_conv(p, x, tv.type);
		return y;
	}

	Ast *pexpr = unparen_expr(ce->proc);
	if (proc_mode == Addressing_Builtin) {
		Entity *e = entity_of_node(pexpr);
		BuiltinProcId id = BuiltinProc_Invalid;
		if (e != nullptr) {
			id = cast(BuiltinProcId)e->Builtin.id;
		} else {
			id = BuiltinProc_DIRECTIVE;
		}
		return lb_build_builtin_proc(p, expr, tv, id);
	}

	// NOTE(bill): Regular call
	lbValue value = {};
	Ast *proc_expr = unparen_expr(ce->proc);
	if (proc_expr->tav.mode == Addressing_Constant) {
		ExactValue v = proc_expr->tav.value;
		switch (v.kind) {
		case ExactValue_Integer:
			{
				u64 u = big_int_to_u64(&v.value_integer);
				lbValue x = {};
				x.value = LLVMConstInt(lb_type(m, t_uintptr), u, false);
				x.type = t_uintptr;
				x = lb_emit_conv(p, x, t_rawptr);
				value = lb_emit_conv(p, x, proc_expr->tav.type);
				break;
			}
		case ExactValue_Pointer:
			{
				u64 u = cast(u64)v.value_pointer;
				lbValue x = {};
				x.value = LLVMConstInt(lb_type(m, t_uintptr), u, false);
				x.type = t_uintptr;
				x = lb_emit_conv(p, x, t_rawptr);
				value = lb_emit_conv(p, x, proc_expr->tav.type);
				break;
			}
		}
	}

	Entity *proc_entity = entity_of_node(proc_expr);
	if (proc_entity != nullptr) {
		if (proc_entity->flags & EntityFlag_Disabled) {
			return {};
		}
	}

	if (value.value == nullptr) {
		value = lb_build_expr(p, proc_expr);
	}

	GB_ASSERT(value.value != nullptr);
	Type *proc_type_ = base_type(value.type);
	GB_ASSERT(proc_type_->kind == Type_Proc);
	TypeProc *pt = &proc_type_->Proc;

	if (is_call_expr_field_value(ce)) {
		auto args = array_make<lbValue>(permanent_allocator(), pt->param_count);

		for_array(arg_index, ce->args) {
			Ast *arg = ce->args[arg_index];
			ast_node(fv, FieldValue, arg);
			GB_ASSERT(fv->field->kind == Ast_Ident);
			String name = fv->field->Ident.token.string;
			isize index = lookup_procedure_parameter(pt, name);
			GB_ASSERT(index >= 0);
			TypeAndValue tav = type_and_value_of_expr(fv->value);
			if (tav.mode == Addressing_Type) {
				args[index] = lb_const_nil(m, tav.type);
			} else {
				args[index] = lb_build_expr(p, fv->value);
			}
		}
		TypeTuple *params = &pt->params->Tuple;
		for (isize i = 0; i < args.count; i++) {
			Entity *e = params->variables[i];
			if (e->kind == Entity_TypeName) {
				args[i] = lb_const_nil(m, e->type);
			} else if (e->kind == Entity_Constant) {
				continue;
			} else {
				GB_ASSERT(e->kind == Entity_Variable);
				if (args[i].value == nullptr) {
					args[i] = lb_handle_param_value(p, e->type, e->Variable.param_value, ast_token(expr).pos);
				} else {
					args[i] = lb_emit_conv(p, args[i], e->type);
				}
			}
		}

		for (isize i = 0; i < args.count; i++) {
			Entity *e = params->variables[i];
			if (args[i].type == nullptr) {
				continue;
			} else if (is_type_untyped_nil(args[i].type)) {
				args[i] = lb_const_nil(m, e->type);
			} else if (is_type_untyped_undef(args[i].type)) {
				args[i] = lb_const_undef(m, e->type);
			}
		}

		return lb_emit_call(p, value, args, ce->inlining, p->copy_elision_hint.ast == expr);
	}

	isize arg_index = 0;

	isize arg_count = 0;
	for_array(i, ce->args) {
		Ast *arg = ce->args[i];
		TypeAndValue tav = type_and_value_of_expr(arg);
		GB_ASSERT_MSG(tav.mode != Addressing_Invalid, "%s %s", expr_to_string(arg), expr_to_string(expr));
		GB_ASSERT_MSG(tav.mode != Addressing_ProcGroup, "%s", expr_to_string(arg));
		Type *at = tav.type;
		if (at->kind == Type_Tuple) {
			arg_count += at->Tuple.variables.count;
		} else {
			arg_count++;
		}
	}

	isize param_count = 0;
	if (pt->params) {
		GB_ASSERT(pt->params->kind == Type_Tuple);
		param_count = pt->params->Tuple.variables.count;
	}

	auto args = array_make<lbValue>(permanent_allocator(), cast(isize)gb_max(param_count, arg_count));
	isize variadic_index = pt->variadic_index;
	bool variadic = pt->variadic && variadic_index >= 0;
	bool vari_expand = ce->ellipsis.pos.line != 0;
	bool is_c_vararg = pt->c_vararg;

	String proc_name = {};
	if (p->entity != nullptr) {
		proc_name = p->entity->token.string;
	}
	TokenPos pos = ast_token(ce->proc).pos;

	TypeTuple *param_tuple = nullptr;
	if (pt->params) {
		GB_ASSERT(pt->params->kind == Type_Tuple);
		param_tuple = &pt->params->Tuple;
	}

	for_array(i, ce->args) {
		Ast *arg = ce->args[i];
		TypeAndValue arg_tv = type_and_value_of_expr(arg);
		if (arg_tv.mode == Addressing_Type) {
			args[arg_index++] = lb_const_nil(m, arg_tv.type);
		} else {
			lbValue a = lb_build_expr(p, arg);
			Type *at = a.type;
			if (at->kind == Type_Tuple) {
				for_array(i, at->Tuple.variables) {
					Entity *e = at->Tuple.variables[i];
					lbValue v = lb_emit_struct_ev(p, a, cast(i32)i);
					args[arg_index++] = v;
				}
			} else {
				args[arg_index++] = a;
			}
		}
	}


	if (param_count > 0) {
		GB_ASSERT_MSG(pt->params != nullptr, "%s %td", expr_to_string(expr), pt->param_count);
		GB_ASSERT(param_count < 1000000);

		if (arg_count < param_count) {
			isize end = cast(isize)param_count;
			if (variadic) {
				end = variadic_index;
			}
			while (arg_index < end) {
				Entity *e = param_tuple->variables[arg_index];
				GB_ASSERT(e->kind == Entity_Variable);
				args[arg_index++] = lb_handle_param_value(p, e->type, e->Variable.param_value, ast_token(expr).pos);
			}
		}

		if (is_c_vararg) {
			GB_ASSERT(variadic);
			GB_ASSERT(!vari_expand);
			isize i = 0;
			for (; i < variadic_index; i++) {
				Entity *e = param_tuple->variables[i];
				if (e->kind == Entity_Variable) {
					args[i] = lb_emit_conv(p, args[i], e->type);
				}
			}
			Type *variadic_type = param_tuple->variables[i]->type;
			GB_ASSERT(is_type_slice(variadic_type));
			variadic_type = base_type(variadic_type)->Slice.elem;
			if (!is_type_any(variadic_type)) {
				for (; i < arg_count; i++) {
					args[i] = lb_emit_conv(p, args[i], variadic_type);
				}
			} else {
				for (; i < arg_count; i++) {
					args[i] = lb_emit_conv(p, args[i], default_type(args[i].type));
				}
			}
		} else if (variadic) {
			isize i = 0;
			for (; i < variadic_index; i++) {
				Entity *e = param_tuple->variables[i];
				if (e->kind == Entity_Variable) {
					args[i] = lb_emit_conv(p, args[i], e->type);
				}
			}
			if (!vari_expand) {
				Type *variadic_type = param_tuple->variables[i]->type;
				GB_ASSERT(is_type_slice(variadic_type));
				variadic_type = base_type(variadic_type)->Slice.elem;
				for (; i < arg_count; i++) {
					args[i] = lb_emit_conv(p, args[i], variadic_type);
				}
			}
		} else {
			for (isize i = 0; i < param_count; i++) {
				Entity *e = param_tuple->variables[i];
				if (e->kind == Entity_Variable) {
					if (args[i].value == nullptr) {
						continue;
					}
					GB_ASSERT_MSG(args[i].value != nullptr, "%.*s", LIT(e->token.string));
					args[i] = lb_emit_conv(p, args[i], e->type);
				}
			}
		}

		if (variadic && !vari_expand && !is_c_vararg) {
			// variadic call argument generation
			Type *slice_type = param_tuple->variables[variadic_index]->type;
			Type *elem_type  = base_type(slice_type)->Slice.elem;
			lbAddr slice = lb_add_local_generated(p, slice_type, true);
			isize slice_len = arg_count+1 - (variadic_index+1);

			if (slice_len > 0) {
				lbAddr base_array = lb_add_local_generated(p, alloc_type_array(elem_type, slice_len), true);

				for (isize i = variadic_index, j = 0; i < arg_count; i++, j++) {
					lbValue addr = lb_emit_array_epi(p, base_array.addr, cast(i32)j);
					lb_emit_store(p, addr, args[i]);
				}

				lbValue base_elem = lb_emit_array_epi(p, base_array.addr, 0);
				lbValue len = lb_const_int(m, t_int, slice_len);
				lb_fill_slice(p, slice, base_elem, len);
			}

			arg_count = param_count;
			args[variadic_index] = lb_addr_load(p, slice);
		}
	}

	if (variadic && variadic_index+1 < param_count) {
		for (isize i = variadic_index+1; i < param_count; i++) {
			Entity *e = param_tuple->variables[i];
			args[i] = lb_handle_param_value(p, e->type, e->Variable.param_value, ast_token(expr).pos);
		}
	}

	isize final_count = param_count;
	if (is_c_vararg) {
		final_count = arg_count;
	}

	if (param_tuple != nullptr) {
		for (isize i = 0; i < gb_min(args.count, param_tuple->variables.count); i++) {
			Entity *e = param_tuple->variables[i];
			if (args[i].type == nullptr) {
				continue;
			} else if (is_type_untyped_nil(args[i].type)) {
				args[i] = lb_const_nil(m, e->type);
			} else if (is_type_untyped_undef(args[i].type)) {
				args[i] = lb_const_undef(m, e->type);
			}
		}
	}

	auto call_args = array_slice(args, 0, final_count);
	return lb_emit_call(p, value, call_args, ce->inlining, p->copy_elision_hint.ast == expr);
}

bool lb_is_const(lbValue value) {
	LLVMValueRef v = value.value;
	if (is_type_untyped_nil(value.type) || is_type_untyped_undef(value.type)) {
		// TODO(bill): Is this correct behaviour?
		return true;
	}
	if (LLVMIsConstant(v)) {
		return true;
	}
	return false;
}


bool lb_is_const_or_global(lbValue value) {
	if (lb_is_const(value)) {
		return true;
	}
	if (LLVMGetValueKind(value.value) == LLVMGlobalVariableValueKind) {
		LLVMTypeRef t = LLVMGetElementType(LLVMTypeOf(value.value));
		if (!lb_is_type_kind(t, LLVMPointerTypeKind)) {
			return false;
		}
		LLVMTypeRef elem = LLVMGetElementType(t);
		return lb_is_type_kind(elem, LLVMFunctionTypeKind);
	}
	return false;
}


bool lb_is_const_nil(lbValue value) {
	LLVMValueRef v = value.value;
	if (LLVMIsConstant(v)) {
		if (LLVMIsAConstantAggregateZero(v)) {
			return true;
		} else if (LLVMIsAConstantPointerNull(v)) {
			return true;
		}
	}
	return false;
}

String lb_get_const_string(lbModule *m, lbValue value) {
	GB_ASSERT(lb_is_const(value));
	GB_ASSERT(LLVMIsConstant(value.value));

	Type *t = base_type(value.type);
	GB_ASSERT(are_types_identical(t, t_string));



	unsigned     ptr_indices[1] = {0};
	unsigned     len_indices[1] = {1};
	LLVMValueRef underlying_ptr = LLVMConstExtractValue(value.value, ptr_indices, gb_count_of(ptr_indices));
	LLVMValueRef underlying_len = LLVMConstExtractValue(value.value, len_indices, gb_count_of(len_indices));

	GB_ASSERT(LLVMGetConstOpcode(underlying_ptr) == LLVMGetElementPtr);
	underlying_ptr = LLVMGetOperand(underlying_ptr, 0);
	GB_ASSERT(LLVMIsAGlobalVariable(underlying_ptr));
	underlying_ptr = LLVMGetInitializer(underlying_ptr);

	size_t length = 0;
	char const *text = LLVMGetAsString(underlying_ptr, &length);

	isize real_length = cast(isize)LLVMConstIntGetSExtValue(underlying_len);

	return make_string(cast(u8 const *)text, real_length);
}


void lb_emit_increment(lbProcedure *p, lbValue addr) {
	GB_ASSERT(is_type_pointer(addr.type));
	Type *type = type_deref(addr.type);
	lbValue v_one = lb_const_value(p->module, type, exact_value_i64(1));
	lb_emit_store(p, addr, lb_emit_arith(p, Token_Add, lb_emit_load(p, addr), v_one, type));

}

lbValue lb_emit_byte_swap(lbProcedure *p, lbValue value, Type *end_type) {
	GB_ASSERT(type_size_of(value.type) == type_size_of(end_type));

	if (type_size_of(value.type) < 2) {
		return value;
	}

	Type *original_type = value.type;
	if (is_type_float(original_type)) {
		i64 sz = type_size_of(original_type);
		Type *integer_type = nullptr;
		switch (sz) {
		case 2: integer_type = t_u16; break;
		case 4: integer_type = t_u32; break;
		case 8: integer_type = t_u64; break;
		}
		GB_ASSERT(integer_type != nullptr);
		value = lb_emit_transmute(p, value, integer_type);
	}

	char const *name = "llvm.bswap";
	LLVMTypeRef types[1] = {lb_type(p->module, value.type)};
	unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
	GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
	LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

	LLVMValueRef args[1] = {};
	args[0] = value.value;

	lbValue res = {};
	res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
	res.type = value.type;

	if (is_type_float(original_type)) {
		res = lb_emit_transmute(p, res, original_type);
	}
	res.type = end_type;
	return res;
}


lbValue lb_emit_count_ones(lbProcedure *p, lbValue x, Type *type) {
	x = lb_emit_conv(p, x, type);

	char const *name = "llvm.ctpop";
	LLVMTypeRef types[1] = {lb_type(p->module, type)};
	unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
	GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
	LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

	LLVMValueRef args[1] = {};
	args[0] = x.value;

	lbValue res = {};
	res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
	res.type = type;
	return res;
}

lbValue lb_emit_count_zeros(lbProcedure *p, lbValue x, Type *type) {
	i64 sz = 8*type_size_of(type);
	lbValue size = lb_const_int(p->module, type, cast(u64)sz);
	lbValue count = lb_emit_count_ones(p, x, type);
	return lb_emit_arith(p, Token_Sub, size, count, type);
}



lbValue lb_emit_count_trailing_zeros(lbProcedure *p, lbValue x, Type *type) {
	x = lb_emit_conv(p, x, type);

	char const *name = "llvm.cttz";
	LLVMTypeRef types[1] = {lb_type(p->module, type)};
	unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
	GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
	LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

	LLVMValueRef args[2] = {};
	args[0] = x.value;
	args[1] = LLVMConstNull(LLVMInt1TypeInContext(p->module->ctx));

	lbValue res = {};
	res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
	res.type = type;
	return res;
}

lbValue lb_emit_count_leading_zeros(lbProcedure *p, lbValue x, Type *type) {
	x = lb_emit_conv(p, x, type);

	char const *name = "llvm.ctlz";
	LLVMTypeRef types[1] = {lb_type(p->module, type)};
	unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
	GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
	LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

	LLVMValueRef args[2] = {};
	args[0] = x.value;
	args[1] = LLVMConstNull(LLVMInt1TypeInContext(p->module->ctx));

	lbValue res = {};
	res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
	res.type = type;
	return res;
}



lbValue lb_emit_reverse_bits(lbProcedure *p, lbValue x, Type *type) {
	x = lb_emit_conv(p, x, type);

	char const *name = "llvm.bitreverse";
	LLVMTypeRef types[1] = {lb_type(p->module, type)};
	unsigned id = LLVMLookupIntrinsicID(name, gb_strlen(name));
	GB_ASSERT_MSG(id != 0, "Unable to find %s.%s", name, LLVMPrintTypeToString(types[0]));
	LLVMValueRef ip = LLVMGetIntrinsicDeclaration(p->module->mod, id, types, gb_count_of(types));

	LLVMValueRef args[1] = {};
	args[0] = x.value;

	lbValue res = {};
	res.value = LLVMBuildCall(p->builder, ip, args, gb_count_of(args), "");
	res.type = type;
	return res;
}


lbValue lb_emit_bit_set_card(lbProcedure *p, lbValue x) {
	GB_ASSERT(is_type_bit_set(x.type));
	Type *underlying = bit_set_to_int(x.type);
	lbValue card = lb_emit_count_ones(p, x, underlying);
	return lb_emit_conv(p, card, t_int);
}


lbLoopData lb_loop_start(lbProcedure *p, isize count, Type *index_type) {
	lbLoopData data = {};

	lbValue max = lb_const_int(p->module, t_int, count);

	data.idx_addr = lb_add_local_generated(p, index_type, true);

	data.body = lb_create_block(p, "loop.body");
	data.done = lb_create_block(p, "loop.done");
	data.loop = lb_create_block(p, "loop.loop");

	lb_emit_jump(p, data.loop);
	lb_start_block(p, data.loop);

	data.idx = lb_addr_load(p, data.idx_addr);

	lbValue cond = lb_emit_comp(p, Token_Lt, data.idx, max);
	lb_emit_if(p, cond, data.body, data.done);
	lb_start_block(p, data.body);

	return data;
}

void lb_loop_end(lbProcedure *p, lbLoopData const &data) {
	if (data.idx_addr.addr.value != nullptr) {
		lb_emit_increment(p, data.idx_addr.addr);
		lb_emit_jump(p, data.loop);
		lb_start_block(p, data.done);
	}
}

lbValue lb_emit_comp_against_nil(lbProcedure *p, TokenKind op_kind, lbValue x) {
	lbValue res = {};
	res.type = t_llvm_bool;
	Type *t = x.type;
	if (is_type_enum(t)) {
		if (op_kind == Token_CmpEq) {
			res.value = LLVMBuildIsNull(p->builder, x.value, "");
		} else if (op_kind == Token_NotEq) {
			res.value = LLVMBuildIsNotNull(p->builder, x.value, "");
		}
		return res;
	} else if (is_type_pointer(t)) {
		if (op_kind == Token_CmpEq) {
			res.value = LLVMBuildIsNull(p->builder, x.value, "");
		} else if (op_kind == Token_NotEq) {
			res.value = LLVMBuildIsNotNull(p->builder, x.value, "");
		}
		return res;
	} else if (is_type_cstring(t)) {
		lbValue ptr = lb_emit_conv(p, x, t_u8_ptr);
		if (op_kind == Token_CmpEq) {
			res.value = LLVMBuildIsNull(p->builder, ptr.value, "");
		} else if (op_kind == Token_NotEq) {
			res.value = LLVMBuildIsNotNull(p->builder, ptr.value, "");
		}
		return res;
	} else if (is_type_proc(t)) {
		if (op_kind == Token_CmpEq) {
			res.value = LLVMBuildIsNull(p->builder, x.value, "");
		} else if (op_kind == Token_NotEq) {
			res.value = LLVMBuildIsNotNull(p->builder, x.value, "");
		}
		return res;
	} else if (is_type_any(t)) {
		// TODO(bill): is this correct behaviour for nil comparison for any?
		lbValue data = lb_emit_struct_ev(p, x, 0);
		lbValue ti   = lb_emit_struct_ev(p, x, 1);
		if (op_kind == Token_CmpEq) {
			LLVMValueRef a =  LLVMBuildIsNull(p->builder, data.value, "");
			LLVMValueRef b =  LLVMBuildIsNull(p->builder, ti.value, "");
			res.value = LLVMBuildOr(p->builder, a, b, "");
			return res;
		} else if (op_kind == Token_NotEq) {
			LLVMValueRef a =  LLVMBuildIsNotNull(p->builder, data.value, "");
			LLVMValueRef b =  LLVMBuildIsNotNull(p->builder, ti.value, "");
			res.value = LLVMBuildAnd(p->builder, a, b, "");
			return res;
		}
	} else if (is_type_slice(t)) {
		lbValue data = lb_emit_struct_ev(p, x, 0);
		if (op_kind == Token_CmpEq) {
			res.value = LLVMBuildIsNull(p->builder, data.value, "");
			return res;
		} else if (op_kind == Token_NotEq) {
			res.value = LLVMBuildIsNotNull(p->builder, data.value, "");
			return res;
		}
	} else if (is_type_dynamic_array(t)) {
		lbValue data = lb_emit_struct_ev(p, x, 0);
		if (op_kind == Token_CmpEq) {
			res.value = LLVMBuildIsNull(p->builder, data.value, "");
			return res;
		} else if (op_kind == Token_NotEq) {
			res.value = LLVMBuildIsNotNull(p->builder, data.value, "");
			return res;
		}
	} else if (is_type_map(t)) {
		lbValue hashes = lb_emit_struct_ev(p, x, 0);
		lbValue data = lb_emit_struct_ev(p, hashes, 0);
		return lb_emit_comp(p, op_kind, data, lb_zero(p->module, data.type));
	} else if (is_type_union(t)) {
		if (type_size_of(t) == 0) {
			if (op_kind == Token_CmpEq) {
				return lb_const_bool(p->module, t_llvm_bool, true);
			} else if (op_kind == Token_NotEq) {
				return lb_const_bool(p->module, t_llvm_bool, false);
			}
		} else if (is_type_union_maybe_pointer(t)) {
			lbValue tag = lb_emit_transmute(p, x, t_rawptr);
			return lb_emit_comp_against_nil(p, op_kind, tag);
		} else {
			lbValue tag = lb_emit_union_tag_value(p, x);
			return lb_emit_comp(p, op_kind, tag, lb_zero(p->module, tag.type));
		}
	} else if (is_type_typeid(t)) {
		lbValue invalid_typeid = lb_const_value(p->module, t_typeid, exact_value_i64(0));
		return lb_emit_comp(p, op_kind, x, invalid_typeid);
	} else if (is_type_soa_struct(t)) {
		Type *bt = base_type(t);
		if (bt->Struct.soa_kind == StructSoa_Slice) {
			LLVMValueRef the_value = {};
			if (bt->Struct.fields.count == 0) {
				lbValue len = lb_soa_struct_len(p, x);
				the_value = len.value;
			} else {
				lbValue first_field = lb_emit_struct_ev(p, x, 0);
				the_value = first_field.value;
			}
			if (op_kind == Token_CmpEq) {
				res.value = LLVMBuildIsNull(p->builder, the_value, "");
				return res;
			} else if (op_kind == Token_NotEq) {
				res.value = LLVMBuildIsNotNull(p->builder, the_value, "");
				return res;
			}
		} else if (bt->Struct.soa_kind == StructSoa_Dynamic) {
			LLVMValueRef the_value = {};
			if (bt->Struct.fields.count == 0) {
				lbValue cap = lb_soa_struct_cap(p, x);
				the_value = cap.value;
			} else {
				lbValue first_field = lb_emit_struct_ev(p, x, 0);
				the_value = first_field.value;
			}
			if (op_kind == Token_CmpEq) {
				res.value = LLVMBuildIsNull(p->builder, the_value, "");
				return res;
			} else if (op_kind == Token_NotEq) {
				res.value = LLVMBuildIsNotNull(p->builder, the_value, "");
				return res;
			}
		}
	} else if (is_type_struct(t) && type_has_nil(t)) {
		auto args = array_make<lbValue>(permanent_allocator(), 2);
		lbValue lhs = lb_address_from_load_or_generate_local(p, x);
		args[0] = lb_emit_conv(p, lhs, t_rawptr);
		args[1] = lb_const_int(p->module, t_int, type_size_of(t));
		lbValue val = lb_emit_runtime_call(p, "memory_compare_zero", args);
		lbValue res = lb_emit_comp(p, op_kind, val, lb_const_int(p->module, t_int, 0));
		return res;
	}
	return {};
}

lbValue lb_get_equal_proc_for_type(lbModule *m, Type *type) {
	Type *original_type = type;
	type = base_type(type);
	GB_ASSERT(is_type_comparable(type));

	Type *pt = alloc_type_pointer(type);
	LLVMTypeRef ptr_type = lb_type(m, pt);

	auto key = hash_type(type);
	lbProcedure **found = map_get(&m->equal_procs, key);
	lbProcedure *compare_proc = nullptr;
	if (found) {
		compare_proc = *found;
		GB_ASSERT(compare_proc != nullptr);
		return {compare_proc->value, compare_proc->type};
	}

	static u32 proc_index = 0;

	char buf[16] = {};
	isize n = gb_snprintf(buf, 16, "__$equal%u", ++proc_index);
	char *str = gb_alloc_str_len(permanent_allocator(), buf, n-1);
	String proc_name = make_string_c(str);

	lbProcedure *p = lb_create_dummy_procedure(m, proc_name, t_equal_proc);
	map_set(&m->equal_procs, key, p);
	lb_begin_procedure_body(p);

	LLVMValueRef x = LLVMGetParam(p->value, 0);
	LLVMValueRef y = LLVMGetParam(p->value, 1);
	x = LLVMBuildPointerCast(p->builder, x, ptr_type, "");
	y = LLVMBuildPointerCast(p->builder, y, ptr_type, "");
	lbValue lhs = {x, pt};
	lbValue rhs = {y, pt};


	lbBlock *block_same_ptr = lb_create_block(p, "same_ptr");
	lbBlock *block_diff_ptr = lb_create_block(p, "diff_ptr");

	lbValue same_ptr = lb_emit_comp(p, Token_CmpEq, lhs, rhs);
	lb_emit_if(p, same_ptr, block_same_ptr, block_diff_ptr);
	lb_start_block(p, block_same_ptr);
	LLVMBuildRet(p->builder, LLVMConstInt(lb_type(m, t_bool), 1, false));

	lb_start_block(p, block_diff_ptr);

	if (type->kind == Type_Struct) {
		type_set_offsets(type);

		lbBlock *block_false = lb_create_block(p, "bfalse");
		lbValue res = lb_const_bool(m, t_bool, true);

		for_array(i, type->Struct.fields) {
			lbBlock *next_block = lb_create_block(p, "btrue");

			lbValue pleft  = lb_emit_struct_ep(p, lhs, cast(i32)i);
			lbValue pright = lb_emit_struct_ep(p, rhs, cast(i32)i);
			lbValue left = lb_emit_load(p, pleft);
			lbValue right = lb_emit_load(p, pright);
			lbValue ok = lb_emit_comp(p, Token_CmpEq, left, right);

			lb_emit_if(p, ok, next_block, block_false);

			lb_emit_jump(p, next_block);
			lb_start_block(p, next_block);
		}

		LLVMBuildRet(p->builder, LLVMConstInt(lb_type(m, t_bool), 1, false));

		lb_start_block(p, block_false);

		LLVMBuildRet(p->builder, LLVMConstInt(lb_type(m, t_bool), 0, false));
	} else if (type->kind == Type_Union) {
		if (is_type_union_maybe_pointer(type)) {
			Type *v = type->Union.variants[0];
			Type *pv = alloc_type_pointer(v);

			lbValue left = lb_emit_load(p, lb_emit_conv(p, lhs, pv));
			lbValue right = lb_emit_load(p, lb_emit_conv(p, rhs, pv));

			lbValue ok = lb_emit_comp(p, Token_CmpEq, left, right);
			ok = lb_emit_conv(p, ok, t_bool);
			LLVMBuildRet(p->builder, ok.value);
		} else {
			lbBlock *block_false = lb_create_block(p, "bfalse");
			lbBlock *block_switch = lb_create_block(p, "bswitch");

			lbValue left_tag  = lb_emit_load(p, lb_emit_union_tag_ptr(p, lhs));
			lbValue right_tag = lb_emit_load(p, lb_emit_union_tag_ptr(p, rhs));

			lbValue tag_eq = lb_emit_comp(p, Token_CmpEq, left_tag, right_tag);
			lb_emit_if(p, tag_eq, block_switch, block_false);

			lb_start_block(p, block_switch);
			LLVMValueRef v_switch = LLVMBuildSwitch(p->builder, left_tag.value, block_false->block, cast(unsigned)type->Union.variants.count);


			for_array(i, type->Union.variants) {
				lbBlock *case_block = lb_create_block(p, "bcase");
				lb_start_block(p, case_block);

				Type *v = type->Union.variants[i];
				lbValue case_tag = lb_const_union_tag(p->module, type, v);

				Type *vp = alloc_type_pointer(v);

				lbValue left  = lb_emit_load(p, lb_emit_conv(p, lhs, vp));
				lbValue right = lb_emit_load(p, lb_emit_conv(p, rhs, vp));
				lbValue ok = lb_emit_comp(p, Token_CmpEq, left, right);
				ok = lb_emit_conv(p, ok, t_bool);

				LLVMBuildRet(p->builder, ok.value);


				LLVMAddCase(v_switch, case_tag.value, case_block->block);
			}

			lb_start_block(p, block_false);

			LLVMBuildRet(p->builder, LLVMConstInt(lb_type(m, t_bool), 0, false));
		}

	} else {
		lbValue left = lb_emit_load(p, lhs);
		lbValue right = lb_emit_load(p, rhs);
		lbValue ok = lb_emit_comp(p, Token_CmpEq, left, right);
		ok = lb_emit_conv(p, ok, t_bool);
		LLVMBuildRet(p->builder, ok.value);
	}

	lb_end_procedure_body(p);

	compare_proc = p;
	return {compare_proc->value, compare_proc->type};
}

lbValue lb_simple_compare_hash(lbProcedure *p, Type *type, lbValue data, lbValue seed) {
	GB_ASSERT_MSG(is_type_simple_compare(type), "%s", type_to_string(type));

	i64 sz = type_size_of(type);

	if (1 <= sz && sz <= 16) {
		char name[20] = {};
		gb_snprintf(name, 20, "default_hasher%d", cast(i32)sz);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = data;
		args[1] = seed;
		return lb_emit_runtime_call(p, name, args);
	}

	auto args = array_make<lbValue>(permanent_allocator(), 3);
	args[0] = data;
	args[1] = seed;
	args[2] = lb_const_int(p->module, t_int, type_size_of(type));
	return lb_emit_runtime_call(p, "default_hasher_n", args);
}

lbValue lb_get_hasher_proc_for_type(lbModule *m, Type *type) {
	Type *original_type = type;
	type = core_type(type);
	GB_ASSERT(is_type_valid_for_keys(type));

	Type *pt = alloc_type_pointer(type);
	LLVMTypeRef ptr_type = lb_type(m, pt);

	auto key = hash_type(type);
	lbProcedure **found = map_get(&m->hasher_procs, key);
	if (found) {
		GB_ASSERT(*found != nullptr);
		return {(*found)->value, (*found)->type};
	}

	static u32 proc_index = 0;

	char buf[16] = {};
	isize n = gb_snprintf(buf, 16, "__$hasher%u", ++proc_index);
	char *str = gb_alloc_str_len(permanent_allocator(), buf, n-1);
	String proc_name = make_string_c(str);

	lbProcedure *p = lb_create_dummy_procedure(m, proc_name, t_hasher_proc);
	map_set(&m->hasher_procs, key, p);
	lb_begin_procedure_body(p);
	defer (lb_end_procedure_body(p));

	LLVMValueRef x = LLVMGetParam(p->value, 0);
	LLVMValueRef y = LLVMGetParam(p->value, 1);
	lbValue data = {x, t_rawptr};
	lbValue seed = {y, t_uintptr};

	LLVMAttributeRef nonnull_attr = lb_create_enum_attribute(m->ctx, "nonnull");
	LLVMAddAttributeAtIndex(p->value, 1+0, nonnull_attr);

	if (is_type_simple_compare(type)) {
		lbValue res = lb_simple_compare_hash(p, type, data, seed);
		LLVMBuildRet(p->builder, res.value);
		return {p->value, p->type};
	}

	if (type->kind == Type_Struct)  {
		type_set_offsets(type);
		data = lb_emit_conv(p, data, t_u8_ptr);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		for_array(i, type->Struct.fields) {
			i64 offset = type->Struct.offsets[i];
			Entity *field = type->Struct.fields[i];
			lbValue field_hasher = lb_get_hasher_proc_for_type(m, field->type);
			lbValue ptr = lb_emit_ptr_offset(p, data, lb_const_int(m, t_uintptr, offset));

			args[0] = ptr;
			args[1] = seed;
			seed = lb_emit_call(p, field_hasher, args);
		}
		LLVMBuildRet(p->builder, seed.value);
	} else if (type->kind == Type_Union)  {
		auto args = array_make<lbValue>(permanent_allocator(), 2);

		if (is_type_union_maybe_pointer(type)) {
			Type *v = type->Union.variants[0];
			lbValue variant_hasher = lb_get_hasher_proc_for_type(m, v);

			args[0] = data;
			args[1] = seed;
			lbValue res = lb_emit_call(p, variant_hasher, args);
			LLVMBuildRet(p->builder, res.value);
		}

		lbBlock *end_block = lb_create_block(p, "bend");
		data = lb_emit_conv(p, data, pt);

		lbValue tag_ptr = lb_emit_union_tag_ptr(p, data);
		lbValue tag = lb_emit_load(p, tag_ptr);

		LLVMValueRef v_switch = LLVMBuildSwitch(p->builder, tag.value, end_block->block, cast(unsigned)type->Union.variants.count);

		for_array(i, type->Union.variants) {
			lbBlock *case_block = lb_create_block(p, "bcase");
			lb_start_block(p, case_block);

			Type *v = type->Union.variants[i];
			Type *vp = alloc_type_pointer(v);
			lbValue case_tag = lb_const_union_tag(p->module, type, v);

			lbValue variant_hasher = lb_get_hasher_proc_for_type(m, v);

			args[0] = data;
			args[1] = seed;
			lbValue res = lb_emit_call(p, variant_hasher, args);
			LLVMBuildRet(p->builder, res.value);

			LLVMAddCase(v_switch, case_tag.value, case_block->block);
		}

		lb_start_block(p, end_block);
		LLVMBuildRet(p->builder, seed.value);

	} else if (type->kind == Type_Array) {
		lbAddr pres = lb_add_local_generated(p, t_uintptr, false);
		lb_addr_store(p, pres, seed);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		lbValue elem_hasher = lb_get_hasher_proc_for_type(m, type->Array.elem);

		auto loop_data = lb_loop_start(p, type->Array.count, t_i32);

		data = lb_emit_conv(p, data, pt);

		lbValue ptr = lb_emit_array_ep(p, data, loop_data.idx);
		args[0] = ptr;
		args[1] = lb_addr_load(p, pres);
		lbValue new_seed = lb_emit_call(p, elem_hasher, args);
		lb_addr_store(p, pres, new_seed);

		lb_loop_end(p, loop_data);

		lbValue res = lb_addr_load(p, pres);
		LLVMBuildRet(p->builder, res.value);
	} else if (type->kind == Type_EnumeratedArray) {
		lbAddr res = lb_add_local_generated(p, t_uintptr, false);
		lb_addr_store(p, res, seed);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		lbValue elem_hasher = lb_get_hasher_proc_for_type(m, type->EnumeratedArray.elem);

		auto loop_data = lb_loop_start(p, type->EnumeratedArray.count, t_i32);

		data = lb_emit_conv(p, data, pt);

		lbValue ptr = lb_emit_array_ep(p, data, loop_data.idx);
		args[0] = ptr;
		args[1] = lb_addr_load(p, res);
		lbValue new_seed = lb_emit_call(p, elem_hasher, args);
		lb_addr_store(p, res, new_seed);

		lb_loop_end(p, loop_data);

		lbValue vres = lb_addr_load(p, res);
		LLVMBuildRet(p->builder, vres.value);
	} else if (is_type_cstring(type)) {
		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = data;
		args[1] = seed;
		lbValue res = lb_emit_runtime_call(p, "default_hasher_cstring", args);
		LLVMBuildRet(p->builder, res.value);
	} else if (is_type_string(type)) {
		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = data;
		args[1] = seed;
		lbValue res = lb_emit_runtime_call(p, "default_hasher_string", args);
		LLVMBuildRet(p->builder, res.value);
	} else {
		GB_PANIC("Unhandled type for hasher: %s", type_to_string(type));
	}

	return {p->value, p->type};
}

lbValue lb_compare_records(lbProcedure *p, TokenKind op_kind, lbValue left, lbValue right, Type *type) {
	GB_ASSERT((is_type_struct(type) || is_type_union(type)) && is_type_comparable(type));
	lbValue left_ptr  = lb_address_from_load_or_generate_local(p, left);
	lbValue right_ptr = lb_address_from_load_or_generate_local(p, right);
	lbValue res = {};
	if (is_type_simple_compare(type)) {
		// TODO(bill): Test to see if this is actually faster!!!!
		auto args = array_make<lbValue>(permanent_allocator(), 3);
		args[0] = lb_emit_conv(p, left_ptr, t_rawptr);
		args[1] = lb_emit_conv(p, right_ptr, t_rawptr);
		args[2] = lb_const_int(p->module, t_int, type_size_of(type));
		res = lb_emit_runtime_call(p, "memory_equal", args);
	} else {
		lbValue value = lb_get_equal_proc_for_type(p->module, type);
		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = lb_emit_conv(p, left_ptr, t_rawptr);
		args[1] = lb_emit_conv(p, right_ptr, t_rawptr);
		res = lb_emit_call(p, value, args);
	}
	if (op_kind == Token_NotEq) {
		res = lb_emit_unary_arith(p, Token_Not, res, res.type);
	}
	return res;
}


lbValue lb_emit_comp(lbProcedure *p, TokenKind op_kind, lbValue left, lbValue right) {
	Type *a = core_type(left.type);
	Type *b = core_type(right.type);

	GB_ASSERT(gb_is_between(op_kind, Token__ComparisonBegin+1, Token__ComparisonEnd-1));

	lbValue nil_check = {};
	if (is_type_untyped_nil(left.type)) {
		nil_check = lb_emit_comp_against_nil(p, op_kind, right);
	} else if (is_type_untyped_nil(right.type)) {
		nil_check = lb_emit_comp_against_nil(p, op_kind, left);
	}
	if (nil_check.value != nullptr) {
		return nil_check;
	}

	if (are_types_identical(a, b)) {
		// NOTE(bill): No need for a conversion
	} else if (lb_is_const(left) || lb_is_const_nil(left)) {
		left = lb_emit_conv(p, left, right.type);
	} else if (lb_is_const(right) || lb_is_const_nil(right)) {
		right = lb_emit_conv(p, right, left.type);
	} else {
		Type *lt = left.type;
		Type *rt = right.type;

		lt = left.type;
		rt = right.type;
		i64 ls = type_size_of(lt);
		i64 rs = type_size_of(rt);

		// NOTE(bill): Quick heuristic, larger types are usually the target type
		if (ls < rs) {
			left = lb_emit_conv(p, left, rt);
		} else if (ls > rs) {
			right = lb_emit_conv(p, right, lt);
		} else {
			if (is_type_union(rt)) {
				left = lb_emit_conv(p, left, rt);
			} else {
				right = lb_emit_conv(p, right, lt);
			}
		}
	}

	if (is_type_array(a) || is_type_enumerated_array(a)) {
		Type *tl = base_type(a);
		lbValue lhs = lb_address_from_load_or_generate_local(p, left);
		lbValue rhs = lb_address_from_load_or_generate_local(p, right);


		TokenKind cmp_op = Token_And;
		lbValue res = lb_const_bool(p->module, t_llvm_bool, true);
		if (op_kind == Token_NotEq) {
			res = lb_const_bool(p->module, t_llvm_bool, false);
			cmp_op = Token_Or;
		} else if (op_kind == Token_CmpEq) {
			res = lb_const_bool(p->module, t_llvm_bool, true);
			cmp_op = Token_And;
		}

		bool inline_array_arith = type_size_of(tl) <= build_context.max_align;
		i32 count = 0;
		switch (tl->kind) {
		case Type_Array:           count = cast(i32)tl->Array.count;           break;
		case Type_EnumeratedArray: count = cast(i32)tl->EnumeratedArray.count; break;
		}

		if (inline_array_arith) {
			// inline
			lbAddr val = lb_add_local_generated(p, t_bool, false);
			lb_addr_store(p, val, res);
			for (i32 i = 0; i < count; i++) {
				lbValue x = lb_emit_load(p, lb_emit_array_epi(p, lhs, i));
				lbValue y = lb_emit_load(p, lb_emit_array_epi(p, rhs, i));
				lbValue cmp = lb_emit_comp(p, op_kind, x, y);
				lbValue new_res = lb_emit_arith(p, cmp_op, lb_addr_load(p, val), cmp, t_bool);
				lb_addr_store(p, val, lb_emit_conv(p, new_res, t_bool));
			}

			return lb_addr_load(p, val);
		} else {
			if (is_type_simple_compare(tl) && (op_kind == Token_CmpEq || op_kind == Token_NotEq)) {
				// TODO(bill): Test to see if this is actually faster!!!!
				auto args = array_make<lbValue>(permanent_allocator(), 3);
				args[0] = lb_emit_conv(p, lhs, t_rawptr);
				args[1] = lb_emit_conv(p, rhs, t_rawptr);
				args[2] = lb_const_int(p->module, t_int, type_size_of(tl));
				lbValue val = lb_emit_runtime_call(p, "memory_compare", args);
				lbValue res = lb_emit_comp(p, op_kind, val, lb_const_nil(p->module, val.type));
				return lb_emit_conv(p, res, t_bool);
			} else {
				lbAddr val = lb_add_local_generated(p, t_bool, false);
				lb_addr_store(p, val, res);
				auto loop_data = lb_loop_start(p, count, t_i32);
				{
					lbValue i = loop_data.idx;
					lbValue x = lb_emit_load(p, lb_emit_array_ep(p, lhs, i));
					lbValue y = lb_emit_load(p, lb_emit_array_ep(p, rhs, i));
					lbValue cmp = lb_emit_comp(p, op_kind, x, y);
					lbValue new_res = lb_emit_arith(p, cmp_op, lb_addr_load(p, val), cmp, t_bool);
					lb_addr_store(p, val, lb_emit_conv(p, new_res, t_bool));
				}
				lb_loop_end(p, loop_data);

				return lb_addr_load(p, val);
			}
		}
	}


	if ((is_type_struct(a) || is_type_union(a)) && is_type_comparable(a)) {
		return lb_compare_records(p, op_kind, left, right, a);
	}

	if ((is_type_struct(b) || is_type_union(b)) && is_type_comparable(b)) {
		return lb_compare_records(p, op_kind, left, right, b);
	}

	if (is_type_string(a)) {
		if (is_type_cstring(a)) {
			left  = lb_emit_conv(p, left, t_string);
			right = lb_emit_conv(p, right, t_string);
		}

		char const *runtime_procedure = nullptr;
		switch (op_kind) {
		case Token_CmpEq: runtime_procedure = "string_eq"; break;
		case Token_NotEq: runtime_procedure = "string_ne"; break;
		case Token_Lt:    runtime_procedure = "string_lt"; break;
		case Token_Gt:    runtime_procedure = "string_gt"; break;
		case Token_LtEq:  runtime_procedure = "string_le"; break;
		case Token_GtEq:  runtime_procedure = "string_gt"; break;
		}
		GB_ASSERT(runtime_procedure != nullptr);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = left;
		args[1] = right;
		return lb_emit_runtime_call(p, runtime_procedure, args);
	}

	if (is_type_complex(a)) {
		char const *runtime_procedure = "";
		i64 sz = 8*type_size_of(a);
		switch (sz) {
		case 32:
			switch (op_kind) {
			case Token_CmpEq: runtime_procedure = "complex32_eq"; break;
			case Token_NotEq: runtime_procedure = "complex32_ne"; break;
			}
			break;
		case 64:
			switch (op_kind) {
			case Token_CmpEq: runtime_procedure = "complex64_eq"; break;
			case Token_NotEq: runtime_procedure = "complex64_ne"; break;
			}
			break;
		case 128:
			switch (op_kind) {
			case Token_CmpEq: runtime_procedure = "complex128_eq"; break;
			case Token_NotEq: runtime_procedure = "complex128_ne"; break;
			}
			break;
		}
		GB_ASSERT(runtime_procedure != nullptr);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = left;
		args[1] = right;
		return lb_emit_runtime_call(p, runtime_procedure, args);
	}

	if (is_type_quaternion(a)) {
		char const *runtime_procedure = "";
		i64 sz = 8*type_size_of(a);
		switch (sz) {
		case 64:
			switch (op_kind) {
			case Token_CmpEq: runtime_procedure = "quaternion64_eq"; break;
			case Token_NotEq: runtime_procedure = "quaternion64_ne"; break;
			}
			break;
		case 128:
			switch (op_kind) {
			case Token_CmpEq: runtime_procedure = "quaternion128_eq"; break;
			case Token_NotEq: runtime_procedure = "quaternion128_ne"; break;
			}
			break;
		case 256:
			switch (op_kind) {
			case Token_CmpEq: runtime_procedure = "quaternion256_eq"; break;
			case Token_NotEq: runtime_procedure = "quaternion256_ne"; break;
			}
			break;
		}
		GB_ASSERT(runtime_procedure != nullptr);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = left;
		args[1] = right;
		return lb_emit_runtime_call(p, runtime_procedure, args);
	}

	if (is_type_bit_set(a)) {
		switch (op_kind) {
		case Token_Lt:
		case Token_LtEq:
		case Token_Gt:
		case Token_GtEq:
			{
				Type *it = bit_set_to_int(a);
				lbValue lhs = lb_emit_transmute(p, left, it);
				lbValue rhs = lb_emit_transmute(p, right, it);
				lbValue res = lb_emit_arith(p, Token_And, lhs, rhs, it);

				if (op_kind == Token_Lt || op_kind == Token_LtEq) {
					// (lhs & rhs) == lhs
					res.value = LLVMBuildICmp(p->builder, LLVMIntEQ, res.value, lhs.value, "");
					res.type = t_llvm_bool;
				} else if (op_kind == Token_Gt || op_kind == Token_GtEq) {
					// (lhs & rhs) == rhs
					res.value = LLVMBuildICmp(p->builder, LLVMIntEQ, res.value, rhs.value, "");
					res.type = t_llvm_bool;
				}

				// NOTE(bill): Strict subsets
				if (op_kind == Token_Lt || op_kind == Token_Gt) {
					// res &~ (lhs == rhs)
					lbValue eq = {};
					eq.value = LLVMBuildICmp(p->builder, LLVMIntEQ, lhs.value, rhs.value, "");
					eq.type = t_llvm_bool;
					res = lb_emit_arith(p, Token_AndNot, res, eq, t_llvm_bool);
				}

				return res;
			}

		case Token_CmpEq:
		case Token_NotEq:
			{
				LLVMIntPredicate pred = {};
				switch (op_kind) {
				case Token_CmpEq: pred = LLVMIntEQ;  break;
				case Token_NotEq: pred = LLVMIntNE;  break;
				}
				lbValue res = {};
				res.type = t_llvm_bool;
				res.value = LLVMBuildICmp(p->builder, pred, left.value, right.value, "");
				return res;
			}
		}
	}

	if (op_kind != Token_CmpEq && op_kind != Token_NotEq) {
		Type *t = left.type;
		if (is_type_integer(t) && is_type_different_to_arch_endianness(t)) {
			Type *platform_type = integer_endian_type_to_platform_type(t);
			lbValue x = lb_emit_byte_swap(p, left, platform_type);
			lbValue y = lb_emit_byte_swap(p, right, platform_type);
			left = x;
			right = y;
		} else if (is_type_float(t) && is_type_different_to_arch_endianness(t)) {
			Type *platform_type = integer_endian_type_to_platform_type(t);
			lbValue x = lb_emit_conv(p, left, platform_type);
			lbValue y = lb_emit_conv(p, right, platform_type);
			left = x;
			right = y;
		}
	}

	a = core_type(left.type);
	b = core_type(right.type);


	lbValue res = {};
	res.type = t_llvm_bool;
	if (is_type_integer(a) ||
	    is_type_boolean(a) ||
	    is_type_pointer(a) ||
	    is_type_proc(a) ||
	    is_type_enum(a)) {
		LLVMIntPredicate pred = {};
		if (is_type_unsigned(left.type)) {
			switch (op_kind) {
			case Token_Gt:   pred = LLVMIntUGT; break;
			case Token_GtEq: pred = LLVMIntUGE; break;
			case Token_Lt:   pred = LLVMIntULT; break;
			case Token_LtEq: pred = LLVMIntULE; break;
			}
		} else {
			switch (op_kind) {
			case Token_Gt:   pred = LLVMIntSGT; break;
			case Token_GtEq: pred = LLVMIntSGE; break;
			case Token_Lt:   pred = LLVMIntSLT; break;
			case Token_LtEq: pred = LLVMIntSLE; break;
			}
		}
		switch (op_kind) {
		case Token_CmpEq: pred = LLVMIntEQ;  break;
		case Token_NotEq: pred = LLVMIntNE;  break;
		}
		LLVMValueRef lhs = left.value;
		LLVMValueRef rhs = right.value;
		if (LLVMTypeOf(lhs) != LLVMTypeOf(rhs)) {
			if (lb_is_type_kind(LLVMTypeOf(lhs), LLVMPointerTypeKind)) {
				rhs = LLVMBuildPointerCast(p->builder, rhs, LLVMTypeOf(lhs), "");
			}
		}

		res.value = LLVMBuildICmp(p->builder, pred, lhs, rhs, "");
	} else if (is_type_float(a)) {
		LLVMRealPredicate pred = {};
		switch (op_kind) {
		case Token_CmpEq: pred = LLVMRealOEQ; break;
		case Token_Gt:    pred = LLVMRealOGT; break;
		case Token_GtEq:  pred = LLVMRealOGE; break;
		case Token_Lt:    pred = LLVMRealOLT; break;
		case Token_LtEq:  pred = LLVMRealOLE; break;
		case Token_NotEq: pred = LLVMRealONE; break;
		}
		res.value = LLVMBuildFCmp(p->builder, pred, left.value, right.value, "");
	} else if (is_type_typeid(a)) {
		LLVMIntPredicate pred = {};
		switch (op_kind) {
		case Token_Gt:   pred = LLVMIntUGT; break;
		case Token_GtEq: pred = LLVMIntUGE; break;
		case Token_Lt:   pred = LLVMIntULT; break;
		case Token_LtEq: pred = LLVMIntULE; break;
		case Token_CmpEq: pred = LLVMIntEQ;  break;
		case Token_NotEq: pred = LLVMIntNE;  break;
		}
		res.value = LLVMBuildICmp(p->builder, pred, left.value, right.value, "");
	} else {
		GB_PANIC("Unhandled comparison kind %s (%s) %.*s %s (%s)", type_to_string(left.type), type_to_string(base_type(left.type)), LIT(token_strings[op_kind]), type_to_string(right.type), type_to_string(base_type(right.type)));
	}

	return res;
}


lbValue lb_generate_anonymous_proc_lit(lbModule *m, String const &prefix_name, Ast *expr, lbProcedure *parent) {
	lbProcedure **found = map_get(&m->gen->anonymous_proc_lits, hash_pointer(expr));
	if (found) {
		return lb_find_procedure_value_from_entity(m, (*found)->entity);
	}

	ast_node(pl, ProcLit, expr);

	// NOTE(bill): Generate a new name
	// parent$count
	isize name_len = prefix_name.len + 1 + 8 + 1;
	char *name_text = gb_alloc_array(permanent_allocator(), char, name_len);
	i32 name_id = cast(i32)m->gen->anonymous_proc_lits.entries.count;

	name_len = gb_snprintf(name_text, name_len, "%.*s$anon-%d", LIT(prefix_name), name_id);
	String name = make_string((u8 *)name_text, name_len-1);

	Type *type = type_of_expr(expr);

	Token token = {};
	token.pos = ast_token(expr).pos;
	token.kind = Token_Ident;
	token.string = name;
	Entity *e = alloc_entity_procedure(nullptr, token, type, pl->tags);
	e->file = expr->file;
	e->decl_info = pl->decl;
	e->code_gen_module = m;
	lbProcedure *p = lb_create_procedure(m, e);

	lbValue value = {};
	value.value = p->value;
	value.type = p->type;

	array_add(&m->procedures_to_generate, p);
	if (parent != nullptr) {
		array_add(&parent->children, p);
	} else {
		string_map_set(&m->members, name, value);
	}

	map_set(&m->anonymous_proc_lits, hash_pointer(expr), p);
	map_set(&m->gen->anonymous_proc_lits, hash_pointer(expr), p);

	return value;
}

lbValue lb_emit_union_cast_only_ok_check(lbProcedure *p, lbValue value, Type *type, TokenPos pos) {
	GB_ASSERT(is_type_tuple(type));
	lbModule *m = p->module;

	Type *src_type = value.type;
	bool is_ptr = is_type_pointer(src_type);


	// IMPORTANT NOTE(bill): This assumes that the value is completely ignored
	// so when it does an assignment, it complete ignores the value.
	// Just make it two booleans and ignore the first one
	//
	// _, ok := x.(T);
	//
	Type *ok_type = type->Tuple.variables[1]->type;
	Type *gen_tuple_types[2] = {};
	gen_tuple_types[0] = ok_type;
	gen_tuple_types[1] = ok_type;

	Type *gen_tuple = alloc_type_tuple_from_field_types(gen_tuple_types, gb_count_of(gen_tuple_types), false, true);

	lbAddr v = lb_add_local_generated(p, gen_tuple, false);

	if (is_ptr) {
		value = lb_emit_load(p, value);
	}
	Type *src = base_type(type_deref(src_type));
	GB_ASSERT_MSG(is_type_union(src), "%s", type_to_string(src_type));
	Type *dst = type->Tuple.variables[0]->type;

	lbValue cond = {};

	if (is_type_union_maybe_pointer(src)) {
		lbValue data = lb_emit_transmute(p, value, dst);
		cond = lb_emit_comp_against_nil(p, Token_NotEq, data);
	} else {
		lbValue tag = lb_emit_union_tag_value(p, value);
		lbValue dst_tag = lb_const_union_tag(m, src, dst);
		cond = lb_emit_comp(p, Token_CmpEq, tag, dst_tag);
	}

	lbValue gep1 = lb_emit_struct_ep(p, v.addr, 1);
	lb_emit_store(p, gep1, cond);

	return lb_addr_load(p, v);
}

lbValue lb_emit_union_cast(lbProcedure *p, lbValue value, Type *type, TokenPos pos) {
	lbModule *m = p->module;

	Type *src_type = value.type;
	bool is_ptr = is_type_pointer(src_type);

	bool is_tuple = true;
	Type *tuple = type;
	if (type->kind != Type_Tuple) {
		is_tuple = false;
		tuple = make_optional_ok_type(type);
	}

	lbAddr v = lb_add_local_generated(p, tuple, true);

	if (is_ptr) {
		value = lb_emit_load(p, value);
	}
	Type *src = base_type(type_deref(src_type));
	GB_ASSERT_MSG(is_type_union(src), "%s", type_to_string(src_type));
	Type *dst = tuple->Tuple.variables[0]->type;

	lbValue value_  = lb_address_from_load_or_generate_local(p, value);

	lbValue tag = {};
	lbValue dst_tag = {};
	lbValue cond = {};
	lbValue data = {};

	lbValue gep0 = lb_emit_struct_ep(p, v.addr, 0);
	lbValue gep1 = lb_emit_struct_ep(p, v.addr, 1);

	if (is_type_union_maybe_pointer(src)) {
		data = lb_emit_load(p, lb_emit_conv(p, value_, gep0.type));
	} else {
		tag     = lb_emit_load(p, lb_emit_union_tag_ptr(p, value_));
		dst_tag = lb_const_union_tag(m, src, dst);
	}

	lbBlock *ok_block = lb_create_block(p, "union_cast.ok");
	lbBlock *end_block = lb_create_block(p, "union_cast.end");

	if (data.value != nullptr) {
		GB_ASSERT(is_type_union_maybe_pointer(src));
		cond = lb_emit_comp_against_nil(p, Token_NotEq, data);
	} else {
		cond = lb_emit_comp(p, Token_CmpEq, tag, dst_tag);
	}

	lb_emit_if(p, cond, ok_block, end_block);
	lb_start_block(p, ok_block);



	if (data.value == nullptr) {
		data = lb_emit_load(p, lb_emit_conv(p, value_, gep0.type));
	}
	lb_emit_store(p, gep0, data);
	lb_emit_store(p, gep1, lb_const_bool(m, t_bool, true));

	lb_emit_jump(p, end_block);
	lb_start_block(p, end_block);

	if (!is_tuple) {
		{
			// NOTE(bill): Panic on invalid conversion
			Type *dst_type = tuple->Tuple.variables[0]->type;

			lbValue ok = lb_emit_load(p, lb_emit_struct_ep(p, v.addr, 1));
			auto args = array_make<lbValue>(permanent_allocator(), 7);
			args[0] = ok;

			args[1] = lb_const_string(m, get_file_path_string(pos.file_id));
			args[2] = lb_const_int(m, t_i32, pos.line);
			args[3] = lb_const_int(m, t_i32, pos.column);

			args[4] = lb_typeid(m, src_type);
			args[5] = lb_typeid(m, dst_type);
			args[6] = lb_emit_conv(p, value_, t_rawptr);
			lb_emit_runtime_call(p, "type_assertion_check2", args);
		}

		return lb_emit_load(p, lb_emit_struct_ep(p, v.addr, 0));
	}
	return lb_addr_load(p, v);
}

lbAddr lb_emit_any_cast_addr(lbProcedure *p, lbValue value, Type *type, TokenPos pos) {
	lbModule *m = p->module;

	Type *src_type = value.type;

	if (is_type_pointer(src_type)) {
		value = lb_emit_load(p, value);
	}

	bool is_tuple = true;
	Type *tuple = type;
	if (type->kind != Type_Tuple) {
		is_tuple = false;
		tuple = make_optional_ok_type(type);
	}
	Type *dst_type = tuple->Tuple.variables[0]->type;

	lbAddr v = lb_add_local_generated(p, tuple, true);

	lbValue dst_typeid = lb_typeid(m, dst_type);
	lbValue any_typeid = lb_emit_struct_ev(p, value, 1);


	lbBlock *ok_block = lb_create_block(p, "any_cast.ok");
	lbBlock *end_block = lb_create_block(p, "any_cast.end");
	lbValue cond = lb_emit_comp(p, Token_CmpEq, any_typeid, dst_typeid);
	lb_emit_if(p, cond, ok_block, end_block);
	lb_start_block(p, ok_block);

	lbValue gep0 = lb_emit_struct_ep(p, v.addr, 0);
	lbValue gep1 = lb_emit_struct_ep(p, v.addr, 1);

	lbValue any_data = lb_emit_struct_ev(p, value, 0);
	lbValue ptr = lb_emit_conv(p, any_data, alloc_type_pointer(dst_type));
	lb_emit_store(p, gep0, lb_emit_load(p, ptr));
	lb_emit_store(p, gep1, lb_const_bool(m, t_bool, true));

	lb_emit_jump(p, end_block);
	lb_start_block(p, end_block);

	if (!is_tuple) {
		// NOTE(bill): Panic on invalid conversion

		lbValue ok = lb_emit_load(p, lb_emit_struct_ep(p, v.addr, 1));
		auto args = array_make<lbValue>(permanent_allocator(), 7);
		args[0] = ok;

		args[1] = lb_const_string(m, get_file_path_string(pos.file_id));
		args[2] = lb_const_int(m, t_i32, pos.line);
		args[3] = lb_const_int(m, t_i32, pos.column);

		args[4] = any_typeid;
		args[5] = dst_typeid;
		args[6] = lb_emit_struct_ev(p, value, 0);;
		lb_emit_runtime_call(p, "type_assertion_check2", args);

		return lb_addr(lb_emit_struct_ep(p, v.addr, 0));
	}
	return v;
}
lbValue lb_emit_any_cast(lbProcedure *p, lbValue value, Type *type, TokenPos pos) {
	return lb_addr_load(p, lb_emit_any_cast_addr(p, value, type, pos));
}


lbValue lb_find_ident(lbProcedure *p, lbModule *m, Entity *e, Ast *expr) {
	auto *found = map_get(&m->values, hash_entity(e));
	if (found) {
		auto v = *found;
		// NOTE(bill): This is because pointers are already pointers in LLVM
		if (is_type_proc(v.type)) {
			return v;
		}
		return lb_emit_load(p, v);
	} else if (e != nullptr && e->kind == Entity_Variable) {
		return lb_addr_load(p, lb_build_addr(p, expr));
	}

	if (e->kind == Entity_Procedure) {
		return lb_find_procedure_value_from_entity(m, e);
	}
	if (USE_SEPARTE_MODULES) {
		lbModule *other_module = lb_pkg_module(m->gen, e->pkg);
		if (other_module != m) {

			String name = lb_get_entity_name(other_module, e);

			lb_set_entity_from_other_modules_linkage_correctly(other_module, e, name);

			lbValue g = {};
			g.value = LLVMAddGlobal(m->mod, lb_type(m, e->type), alloc_cstring(permanent_allocator(), name));
			g.type = alloc_type_pointer(e->type);
			LLVMSetLinkage(g.value, LLVMExternalLinkage);

			lb_add_entity(m, e, g);
			lb_add_member(m, name, g);
			return lb_emit_load(p, g);
		}
	}

	String pkg = {};
	if (e->pkg) {
		pkg = e->pkg->name;
	}
	gb_printf_err("Error in: %s\n", token_pos_to_string(ast_token(expr).pos));
	GB_PANIC("nullptr value for expression from identifier: %.*s.%.*s (%p) : %s @ %p", LIT(pkg), LIT(e->token.string), e, type_to_string(e->type), expr);
	return {};
}

bool lb_is_expr_constant_zero(Ast *expr) {
	GB_ASSERT(expr != nullptr);
	auto v = exact_value_to_integer(expr->tav.value);
	if (v.kind == ExactValue_Integer) {
		return big_int_cmp_zero(&v.value_integer) == 0;
	}
	return false;
}

lbValue lb_build_unary_and(lbProcedure *p, Ast *expr) {
	ast_node(ue, UnaryExpr, expr);
	auto tv = type_and_value_of_expr(expr);


	Ast *ue_expr = unparen_expr(ue->expr);
	if (ue_expr->kind == Ast_IndexExpr && tv.mode == Addressing_OptionalOkPtr && is_type_tuple(tv.type)) {
		Type *tuple = tv.type;

		Type *map_type = type_of_expr(ue_expr->IndexExpr.expr);
		Type *ot = base_type(map_type);
		Type *t = base_type(type_deref(ot));
		bool deref = t != ot;
		GB_ASSERT(t->kind == Type_Map);
		ast_node(ie, IndexExpr, ue_expr);

		lbValue map_val = lb_build_addr_ptr(p, ie->expr);
		if (deref) {
			map_val = lb_emit_load(p, map_val);
		}

		lbValue key = lb_build_expr(p, ie->index);
		key = lb_emit_conv(p, key, t->Map.key);

		Type *result_type = type_of_expr(expr);
		lbAddr addr = lb_addr_map(map_val, key, t, alloc_type_pointer(t->Map.value));
		lbValue ptr = lb_addr_get_ptr(p, addr);

		lbValue ok = lb_emit_comp_against_nil(p, Token_NotEq, ptr);
		ok = lb_emit_conv(p, ok, tuple->Tuple.variables[1]->type);

		lbAddr res = lb_add_local_generated(p, tuple, false);
		lbValue gep0 = lb_emit_struct_ep(p, res.addr, 0);
		lbValue gep1 = lb_emit_struct_ep(p, res.addr, 1);
		lb_emit_store(p, gep0, ptr);
		lb_emit_store(p, gep1, ok);
		return lb_addr_load(p, res);

	} if (ue_expr->kind == Ast_CompoundLit) {
		lbValue v = lb_build_expr(p, ue->expr);

		Type *type = v.type;
		lbAddr addr = {};
		if (p->is_startup) {
			addr = lb_add_global_generated(p->module, type, v);
		} else {
			addr = lb_add_local_generated(p, type, false);
		}
		lb_addr_store(p, addr, v);
		return addr.addr;

	} else if (ue_expr->kind == Ast_TypeAssertion) {
		if (is_type_tuple(tv.type)) {
			Type *tuple = tv.type;
			Type *ptr_type = tuple->Tuple.variables[0]->type;
			Type *ok_type = tuple->Tuple.variables[1]->type;

			ast_node(ta, TypeAssertion, ue_expr);
			TokenPos pos = ast_token(expr).pos;
			Type *type = type_of_expr(ue_expr);
			GB_ASSERT(!is_type_tuple(type));

			lbValue e = lb_build_expr(p, ta->expr);
			Type *t = type_deref(e.type);
			if (is_type_union(t)) {
				lbValue v = e;
				if (!is_type_pointer(v.type)) {
					v = lb_address_from_load_or_generate_local(p, v);
				}
				Type *src_type = type_deref(v.type);
				Type *dst_type = type;

				lbValue src_tag = {};
				lbValue dst_tag = {};
				if (is_type_union_maybe_pointer(src_type)) {
					src_tag = lb_emit_comp_against_nil(p, Token_NotEq, v);
					dst_tag = lb_const_bool(p->module, t_bool, true);
				} else {
					src_tag = lb_emit_load(p, lb_emit_union_tag_ptr(p, v));
					dst_tag = lb_const_union_tag(p->module, src_type, dst_type);
				}

				lbValue ok = lb_emit_comp(p, Token_CmpEq, src_tag, dst_tag);

				lbValue data_ptr = lb_emit_conv(p, v, ptr_type);
				lbAddr res = lb_add_local_generated(p, tuple, true);
				lbValue gep0 = lb_emit_struct_ep(p, res.addr, 0);
				lbValue gep1 = lb_emit_struct_ep(p, res.addr, 1);
				lb_emit_store(p, gep0, lb_emit_select(p, ok, data_ptr, lb_const_nil(p->module, ptr_type)));
				lb_emit_store(p, gep1, lb_emit_conv(p, ok, ok_type));
				return lb_addr_load(p, res);
			} else if (is_type_any(t)) {
				lbValue v = e;
				if (is_type_pointer(v.type)) {
					v = lb_emit_load(p, v);
				}

				lbValue data_ptr = lb_emit_conv(p, lb_emit_struct_ev(p, v, 0), ptr_type);
				lbValue any_id = lb_emit_struct_ev(p, v, 1);
				lbValue id = lb_typeid(p->module, type);

				lbValue ok = lb_emit_comp(p, Token_CmpEq, any_id, id);

				lbAddr res = lb_add_local_generated(p, tuple, false);
				lbValue gep0 = lb_emit_struct_ep(p, res.addr, 0);
				lbValue gep1 = lb_emit_struct_ep(p, res.addr, 1);
				lb_emit_store(p, gep0, lb_emit_select(p, ok, data_ptr, lb_const_nil(p->module, ptr_type)));
				lb_emit_store(p, gep1, lb_emit_conv(p, ok, ok_type));
				return lb_addr_load(p, res);
			} else {
				GB_PANIC("TODO(bill): type assertion %s", type_to_string(type));
			}

		} else {
			GB_ASSERT(is_type_pointer(tv.type));

			ast_node(ta, TypeAssertion, ue_expr);
			TokenPos pos = ast_token(expr).pos;
			Type *type = type_of_expr(ue_expr);
			GB_ASSERT(!is_type_tuple(type));

			lbValue e = lb_build_expr(p, ta->expr);
			Type *t = type_deref(e.type);
			if (is_type_union(t)) {
				lbValue v = e;
				if (!is_type_pointer(v.type)) {
					v = lb_address_from_load_or_generate_local(p, v);
				}
				Type *src_type = type_deref(v.type);
				Type *dst_type = type;

				lbValue src_tag = {};
				lbValue dst_tag = {};
				if (is_type_union_maybe_pointer(src_type)) {
					src_tag = lb_emit_comp_against_nil(p, Token_NotEq, v);
					dst_tag = lb_const_bool(p->module, t_bool, true);
				} else {
					src_tag = lb_emit_load(p, lb_emit_union_tag_ptr(p, v));
					dst_tag = lb_const_union_tag(p->module, src_type, dst_type);
				}

				lbValue ok = lb_emit_comp(p, Token_CmpEq, src_tag, dst_tag);
				auto args = array_make<lbValue>(permanent_allocator(), 6);
				args[0] = ok;

				args[1] = lb_find_or_add_entity_string(p->module, get_file_path_string(pos.file_id));
				args[2] = lb_const_int(p->module, t_i32, pos.line);
				args[3] = lb_const_int(p->module, t_i32, pos.column);

				args[4] = lb_typeid(p->module, src_type);
				args[5] = lb_typeid(p->module, dst_type);
				lb_emit_runtime_call(p, "type_assertion_check", args);

				lbValue data_ptr = v;
				return lb_emit_conv(p, data_ptr, tv.type);
			} else if (is_type_any(t)) {
				lbValue v = e;
				if (is_type_pointer(v.type)) {
					v = lb_emit_load(p, v);
				}

				lbValue data_ptr = lb_emit_struct_ev(p, v, 0);
				lbValue any_id = lb_emit_struct_ev(p, v, 1);
				lbValue id = lb_typeid(p->module, type);


				lbValue ok = lb_emit_comp(p, Token_CmpEq, any_id, id);
				auto args = array_make<lbValue>(permanent_allocator(), 6);
				args[0] = ok;

				args[1] = lb_find_or_add_entity_string(p->module, get_file_path_string(pos.file_id));
				args[2] = lb_const_int(p->module, t_i32, pos.line);
				args[3] = lb_const_int(p->module, t_i32, pos.column);

				args[4] = any_id;
				args[5] = id;
				lb_emit_runtime_call(p, "type_assertion_check", args);

				return lb_emit_conv(p, data_ptr, tv.type);
			} else {
				GB_PANIC("TODO(bill): type assertion %s", type_to_string(type));
			}
		}
	}

	return lb_build_addr_ptr(p, ue->expr);
}

lbValue lb_build_expr(lbProcedure *p, Ast *expr) {
	lbModule *m = p->module;

	u16 prev_state_flags = p->state_flags;
	defer (p->state_flags = prev_state_flags);

	if (expr->state_flags != 0) {
		u16 in = expr->state_flags;
		u16 out = p->state_flags;

		if (in & StateFlag_bounds_check) {
			out |= StateFlag_bounds_check;
			out &= ~StateFlag_no_bounds_check;
		} else if (in & StateFlag_no_bounds_check) {
			out |= StateFlag_no_bounds_check;
			out &= ~StateFlag_bounds_check;
		}

		p->state_flags = out;
	}

	expr = unparen_expr(expr);

	TokenPos expr_pos = ast_token(expr).pos;
	TypeAndValue tv = type_and_value_of_expr(expr);
	GB_ASSERT_MSG(tv.mode != Addressing_Invalid, "invalid expression '%s' (tv.mode = %d, tv.type = %s) @ %s\n Current Proc: %.*s : %s", expr_to_string(expr), tv.mode, type_to_string(tv.type), token_pos_to_string(expr_pos), LIT(p->name), type_to_string(p->type));

	if (tv.value.kind != ExactValue_Invalid) {
		// NOTE(bill): The commented out code below is just for debug purposes only
		// GB_ASSERT_MSG(!is_type_untyped(tv.type), "%s @ %s\n%s", type_to_string(tv.type), token_pos_to_string(expr_pos), expr_to_string(expr));
		// if (is_type_untyped(tv.type)) {
		// 	gb_printf_err("%s %s\n", token_pos_to_string(expr_pos), expr_to_string(expr));
		// }

		// NOTE(bill): Short on constant values
		return lb_const_value(p->module, tv.type, tv.value);
	}

	#if 0
	LLVMMetadataRef prev_debug_location = nullptr;
	if (p->debug_info != nullptr) {
		prev_debug_location = LLVMGetCurrentDebugLocation2(p->builder);
		LLVMSetCurrentDebugLocation2(p->builder, lb_debug_location_from_ast(p, expr));
	}
	defer (if (prev_debug_location != nullptr) {
		LLVMSetCurrentDebugLocation2(p->builder, prev_debug_location);
	});
	#endif

	switch (expr->kind) {
	case_ast_node(bl, BasicLit, expr);
		TokenPos pos = bl->token.pos;
		GB_PANIC("Non-constant basic literal %s - %.*s", token_pos_to_string(pos), LIT(token_strings[bl->token.kind]));
	case_end;

	case_ast_node(bd, BasicDirective, expr);
		TokenPos pos = bd->token.pos;
		GB_PANIC("Non-constant basic literal %s - %.*s", token_pos_to_string(pos), LIT(bd->name.string));
	case_end;

	case_ast_node(i, Implicit, expr);
		return lb_addr_load(p, lb_build_addr(p, expr));
	case_end;

	case_ast_node(u, Undef, expr)
		lbValue res = {};
		if (is_type_untyped(tv.type)) {
			res.value = nullptr;
			res.type  = t_untyped_undef;
		} else {
			res.value = LLVMGetUndef(lb_type(m, tv.type));
			res.type  = tv.type;
		}
		return res;
	case_end;

	case_ast_node(i, Ident, expr);
		Entity *e = entity_from_expr(expr);
		e = strip_entity_wrapping(e);

		GB_ASSERT_MSG(e != nullptr, "%s", expr_to_string(expr));
		if (e->kind == Entity_Builtin) {
			Token token = ast_token(expr);
			GB_PANIC("TODO(bill): lb_build_expr Entity_Builtin '%.*s'\n"
			         "\t at %s", LIT(builtin_procs[e->Builtin.id].name),
			         token_pos_to_string(token.pos));
			return {};
		} else if (e->kind == Entity_Nil) {
			lbValue res = {};
			res.value = nullptr;
			res.type = e->type;
			return res;
		}
		GB_ASSERT(e->kind != Entity_ProcGroup);

		return lb_find_ident(p, m, e, expr);
	case_end;

	case_ast_node(de, DerefExpr, expr);
		return lb_addr_load(p, lb_build_addr(p, expr));
	case_end;

	case_ast_node(se, SelectorExpr, expr);
		TypeAndValue tav = type_and_value_of_expr(expr);
		GB_ASSERT(tav.mode != Addressing_Invalid);
		return lb_addr_load(p, lb_build_addr(p, expr));
	case_end;

	case_ast_node(ise, ImplicitSelectorExpr, expr);
		TypeAndValue tav = type_and_value_of_expr(expr);
		GB_ASSERT(tav.mode == Addressing_Constant);

		return lb_const_value(p->module, tv.type, tv.value);
	case_end;

	case_ast_node(se, SelectorCallExpr, expr);
		GB_ASSERT(se->modified_call);
		TypeAndValue tav = type_and_value_of_expr(expr);
		GB_ASSERT(tav.mode != Addressing_Invalid);
		lbValue res = lb_build_call_expr(p, se->call);

		ast_node(ce, CallExpr, se->call);
		ce->sce_temp_data = gb_alloc_copy(permanent_allocator(), &res, gb_size_of(res));

		return res;
	case_end;

	case_ast_node(te, TernaryIfExpr, expr);
		LLVMValueRef incoming_values[2] = {};
		LLVMBasicBlockRef incoming_blocks[2] = {};

		GB_ASSERT(te->y != nullptr);
		lbBlock *then  = lb_create_block(p, "if.then");
		lbBlock *done  = lb_create_block(p, "if.done"); // NOTE(bill): Append later
		lbBlock *else_ = lb_create_block(p, "if.else");

		lbValue cond = lb_build_cond(p, te->cond, then, else_);
		lb_start_block(p, then);

		Type *type = default_type(type_of_expr(expr));

		lb_open_scope(p, nullptr);
		incoming_values[0] = lb_emit_conv(p, lb_build_expr(p, te->x), type).value;
		lb_close_scope(p, lbDeferExit_Default, nullptr);

		lb_emit_jump(p, done);
		lb_start_block(p, else_);

		lb_open_scope(p, nullptr);
		incoming_values[1] = lb_emit_conv(p, lb_build_expr(p, te->y), type).value;
		lb_close_scope(p, lbDeferExit_Default, nullptr);

		lb_emit_jump(p, done);
		lb_start_block(p, done);

		lbValue res = {};
		res.value = LLVMBuildPhi(p->builder, lb_type(p->module, type), "");
		res.type = type;

		GB_ASSERT(p->curr_block->preds.count >= 2);
		incoming_blocks[0] = p->curr_block->preds[0]->block;
		incoming_blocks[1] = p->curr_block->preds[1]->block;

		LLVMAddIncoming(res.value, incoming_values, incoming_blocks, 2);

		return res;
	case_end;

	case_ast_node(te, TernaryWhenExpr, expr);
		TypeAndValue tav = type_and_value_of_expr(te->cond);
		GB_ASSERT(tav.mode == Addressing_Constant);
		GB_ASSERT(tav.value.kind == ExactValue_Bool);
		if (tav.value.value_bool) {
			return lb_build_expr(p, te->x);
		} else {
			return lb_build_expr(p, te->y);
		}
	case_end;

	case_ast_node(ta, TypeAssertion, expr);
		TokenPos pos = ast_token(expr).pos;
		Type *type = tv.type;
		lbValue e = lb_build_expr(p, ta->expr);
		Type *t = type_deref(e.type);
		if (is_type_union(t)) {
			if (ta->ignores[0]) {
				// NOTE(bill): This is not needed for optimization levels other than 0
				return lb_emit_union_cast_only_ok_check(p, e, type, pos);
			}
			return lb_emit_union_cast(p, e, type, pos);
		} else if (is_type_any(t)) {
			return lb_emit_any_cast(p, e, type, pos);
		} else {
			GB_PANIC("TODO(bill): type assertion %s", type_to_string(e.type));
		}
	case_end;

	case_ast_node(tc, TypeCast, expr);
		lbValue e = lb_build_expr(p, tc->expr);
		switch (tc->token.kind) {
		case Token_cast:
			return lb_emit_conv(p, e, tv.type);
		case Token_transmute:
			return lb_emit_transmute(p, e, tv.type);
		}
		GB_PANIC("Invalid AST TypeCast");
	case_end;

	case_ast_node(ac, AutoCast, expr);
		lbValue value = lb_build_expr(p, ac->expr);
		return lb_emit_conv(p, value, tv.type);
	case_end;

	case_ast_node(ue, UnaryExpr, expr);
		switch (ue->op.kind) {
		case Token_And:
			return lb_build_unary_and(p, expr);
		default:
			{
				lbValue v = lb_build_expr(p, ue->expr);
				return lb_emit_unary_arith(p, ue->op.kind, v, tv.type);
			}
		}
	case_end;

	case_ast_node(be, BinaryExpr, expr);
		return lb_build_binary_expr(p, expr);
	case_end;

	case_ast_node(pl, ProcLit, expr);
		return lb_generate_anonymous_proc_lit(p->module, p->name, expr, p);
	case_end;

	case_ast_node(cl, CompoundLit, expr);
		return lb_addr_load(p, lb_build_addr(p, expr));
	case_end;

	case_ast_node(ce, CallExpr, expr);
		return lb_build_call_expr(p, expr);
	case_end;

	case_ast_node(se, SliceExpr, expr);
		if (is_type_slice(type_of_expr(se->expr))) {
			// NOTE(bill): Quick optimization
			if (se->high == nullptr &&
			    (se->low == nullptr || lb_is_expr_constant_zero(se->low))) {
				return lb_build_expr(p, se->expr);
			}
		}
		return lb_addr_load(p, lb_build_addr(p, expr));
	case_end;

	case_ast_node(ie, IndexExpr, expr);
		return lb_addr_load(p, lb_build_addr(p, expr));
	case_end;

	case_ast_node(ia, InlineAsmExpr, expr);
		Type *t = type_of_expr(expr);
		GB_ASSERT(is_type_asm_proc(t));

		String asm_string = {};
		String constraints_string = {};

		TypeAndValue tav;
		tav = type_and_value_of_expr(ia->asm_string);
		GB_ASSERT(is_type_string(tav.type));
		GB_ASSERT(tav.value.kind == ExactValue_String);
		asm_string = tav.value.value_string;

		tav = type_and_value_of_expr(ia->constraints_string);
		GB_ASSERT(is_type_string(tav.type));
		GB_ASSERT(tav.value.kind == ExactValue_String);
		constraints_string = tav.value.value_string;


		LLVMInlineAsmDialect dialect = LLVMInlineAsmDialectATT;
		switch (ia->dialect) {
		case InlineAsmDialect_Default: dialect = LLVMInlineAsmDialectATT;   break;
		case InlineAsmDialect_ATT:     dialect = LLVMInlineAsmDialectATT;   break;
		case InlineAsmDialect_Intel:   dialect = LLVMInlineAsmDialectIntel; break;
		default: GB_PANIC("Unhandled inline asm dialect"); break;
		}

		LLVMTypeRef func_type = LLVMGetElementType(lb_type(p->module, t));
		LLVMValueRef the_asm = LLVMGetInlineAsm(func_type,
			cast(char *)asm_string.text, cast(size_t)asm_string.len,
			cast(char *)constraints_string.text, cast(size_t)constraints_string.len,
			ia->has_side_effects, ia->is_align_stack, dialect
		);
		GB_ASSERT(the_asm != nullptr);
		return {the_asm, t};
	case_end;
	}

	GB_PANIC("lb_build_expr: %.*s", LIT(ast_strings[expr->kind]));

	return {};
}

lbAddr lb_get_soa_variable_addr(lbProcedure *p, Entity *e) {
	return map_must_get(&p->module->soa_values, hash_entity(e));
}
lbValue lb_get_using_variable(lbProcedure *p, Entity *e) {
	GB_ASSERT(e->kind == Entity_Variable && e->flags & EntityFlag_Using);
	String name = e->token.string;
	Entity *parent = e->using_parent;
	Selection sel = lookup_field(parent->type, name, false);
	GB_ASSERT(sel.entity != nullptr);
	lbValue *pv = map_get(&p->module->values, hash_entity(parent));

	lbValue v = {};

	if (pv == nullptr && parent->flags & EntityFlag_SoaPtrField) {
		// NOTE(bill): using SOA value (probably from for-in statement)
		lbAddr parent_addr = lb_get_soa_variable_addr(p, parent);
		v = lb_addr_get_ptr(p, parent_addr);
	} else if (pv != nullptr) {
		v = *pv;
	} else {
		GB_ASSERT_MSG(e->using_expr != nullptr, "%.*s", LIT(name));
		v = lb_build_addr_ptr(p, e->using_expr);
	}
	GB_ASSERT(v.value != nullptr);
	GB_ASSERT_MSG(parent->type == type_deref(v.type), "%s %s", type_to_string(parent->type), type_to_string(v.type));
	lbValue ptr = lb_emit_deep_field_gep(p, v, sel);
	if (parent->scope) {
		if ((parent->scope->flags & (ScopeFlag_File|ScopeFlag_Pkg)) == 0) {
			lb_add_debug_local_variable(p, ptr.value, e->type, e->token);
		}
	} else {
		lb_add_debug_local_variable(p, ptr.value, e->type, e->token);
	}
	return ptr;
}


lbAddr lb_build_addr_from_entity(lbProcedure *p, Entity *e, Ast *expr) {
	GB_ASSERT(e != nullptr);
	if (e->kind == Entity_Constant) {
		Type *t = default_type(type_of_expr(expr));
		lbValue v = lb_const_value(p->module, t, e->Constant.value);
		lbAddr g = lb_add_global_generated(p->module, t, v);
		return g;
	}


	lbValue v = {};
	lbValue *found = map_get(&p->module->values, hash_entity(e));
	if (found) {
		v = *found;
	} else if (e->kind == Entity_Variable && e->flags & EntityFlag_Using) {
		// NOTE(bill): Calculate the using variable every time
		v = lb_get_using_variable(p, e);
	} else if (e->flags & EntityFlag_SoaPtrField) {
		return lb_get_soa_variable_addr(p, e);
	}


	if (v.value == nullptr) {
		return lb_addr(lb_find_value_from_entity(p->module, e));

		// error(expr, "%.*s Unknown value: %.*s, entity: %p %.*s",
		//       LIT(p->name),
		//       LIT(e->token.string), e, LIT(entity_strings[e->kind]));
		// GB_PANIC("Unknown value");
	}

	return lb_addr(v);
}

lbValue lb_gen_map_header(lbProcedure *p, lbValue map_val_ptr, Type *map_type) {
	GB_ASSERT_MSG(is_type_pointer(map_val_ptr.type), "%s", type_to_string(map_val_ptr.type));
	lbAddr h = lb_add_local_generated(p, t_map_header, false); // all the values will be initialzed later
	map_type = base_type(map_type);
	GB_ASSERT(map_type->kind == Type_Map);

	Type *key_type = map_type->Map.key;
	Type *val_type = map_type->Map.value;

	// NOTE(bill): Removes unnecessary allocation if split gep
	lbValue gep0 = lb_emit_struct_ep(p, h.addr, 0);
	lbValue m = lb_emit_conv(p, map_val_ptr, type_deref(gep0.type));
	lb_emit_store(p, gep0, m);

	i64 entry_size   = type_size_of  (map_type->Map.entry_type);
	i64 entry_align  = type_align_of (map_type->Map.entry_type);

	i64 key_offset = type_offset_of(map_type->Map.entry_type, 2);
	i64 key_size   = type_size_of  (map_type->Map.key);

	i64 value_offset = type_offset_of(map_type->Map.entry_type, 3);
	i64 value_size   = type_size_of  (map_type->Map.value);

	lb_emit_store(p, lb_emit_struct_ep(p, h.addr, 1), lb_get_equal_proc_for_type(p->module, key_type));
	lb_emit_store(p, lb_emit_struct_ep(p, h.addr, 2), lb_const_int(p->module, t_int, entry_size));
	lb_emit_store(p, lb_emit_struct_ep(p, h.addr, 3), lb_const_int(p->module, t_int, entry_align));
	lb_emit_store(p, lb_emit_struct_ep(p, h.addr, 4), lb_const_int(p->module, t_uintptr, key_offset));
	lb_emit_store(p, lb_emit_struct_ep(p, h.addr, 5), lb_const_int(p->module, t_int, key_size));
	lb_emit_store(p, lb_emit_struct_ep(p, h.addr, 6), lb_const_int(p->module, t_uintptr, value_offset));
	lb_emit_store(p, lb_emit_struct_ep(p, h.addr, 7), lb_const_int(p->module, t_int, value_size));

	return lb_addr_load(p, h);
}

lbValue lb_const_hash(lbModule *m, lbValue key, Type *key_type) {
	if (true) {
		return {};
	}

	lbValue hashed_key = {};


	if (lb_is_const(key)) {
		u64 hash = 0xcbf29ce484222325;
		if (is_type_cstring(key_type)) {
			size_t length = 0;
			char const *text = LLVMGetAsString(key.value, &length);
			hash = fnv64a(text, cast(isize)length);
		} else if (is_type_string(key_type)) {
			unsigned data_indices[] = {0};
			unsigned len_indices[] = {1};
			LLVMValueRef data = LLVMConstExtractValue(key.value, data_indices, gb_count_of(data_indices));
			LLVMValueRef len  = LLVMConstExtractValue(key.value, len_indices,  gb_count_of(len_indices));
			isize length = LLVMConstIntGetSExtValue(len);
			char const *text = nullptr;
			if (false && length != 0) {
				if (LLVMGetConstOpcode(data) != LLVMGetElementPtr) {
					return {};
				}
				// TODO(bill): THIS IS BROKEN! THIS NEEDS FIXING :P

				size_t ulength = 0;
				text = LLVMGetAsString(data, &ulength);
				gb_printf_err("%td %td %s\n", length, ulength, text);
				length = gb_min(length, cast(isize)ulength);
			}
			hash = fnv64a(text, cast(isize)length);
		} else {
			return {};
		}
		// TODO(bill): other const hash types

		if (build_context.word_size == 4) {
			hash &= 0xffffffffull;
		}
		hashed_key = lb_const_int(m, t_uintptr, hash);
	}

	return hashed_key;
}

lbValue lb_gen_map_hash(lbProcedure *p, lbValue key, Type *key_type) {
	Type *hash_type = t_u64;
	lbAddr v = lb_add_local_generated(p, t_map_hash, true);
	lbValue vp = lb_addr_get_ptr(p, v);
	Type *t = base_type(key.type);
	key = lb_emit_conv(p, key, key_type);

	lbValue key_ptr = lb_address_from_load_or_generate_local(p, key);
	key_ptr = lb_emit_conv(p, key_ptr, t_rawptr);

	lbValue hashed_key = lb_const_hash(p->module, key, key_type);
	if (hashed_key.value == nullptr) {
		lbValue hasher = lb_get_hasher_proc_for_type(p->module, key_type);

		auto args = array_make<lbValue>(permanent_allocator(), 2);
		args[0] = key_ptr;
		args[1] = lb_const_int(p->module, t_uintptr, 0);
		hashed_key = lb_emit_call(p, hasher, args);
	}

	lb_emit_store(p, lb_emit_struct_ep(p, vp, 0), hashed_key);
	lb_emit_store(p, lb_emit_struct_ep(p, vp, 1), key_ptr);

	return lb_addr_load(p, v);
}

void lb_insert_dynamic_map_key_and_value(lbProcedure *p, lbAddr addr, Type *map_type,
                                         lbValue map_key, lbValue map_value, Ast *node) {
	map_type = base_type(map_type);
	GB_ASSERT(map_type->kind == Type_Map);

	lbValue h = lb_gen_map_header(p, addr.addr, map_type);
	lbValue key = lb_gen_map_hash(p, map_key, map_type->Map.key);
	lbValue v = lb_emit_conv(p, map_value, map_type->Map.value);

	lbAddr value_addr = lb_add_local_generated(p, v.type, false);
	lb_addr_store(p, value_addr, v);

	auto args = array_make<lbValue>(permanent_allocator(), 4);
	args[0] = h;
	args[1] = key;
	args[2] = lb_emit_conv(p, value_addr.addr, t_rawptr);
	args[3] = lb_emit_source_code_location(p, node);
	lb_emit_runtime_call(p, "__dynamic_map_set", args);
}


lbAddr lb_build_addr(lbProcedure *p, Ast *expr) {
	expr = unparen_expr(expr);

	switch (expr->kind) {
	case_ast_node(i, Implicit, expr);
		lbAddr v = {};
		switch (i->kind) {
		case Token_context:
			v = lb_find_or_generate_context_ptr(p);
			break;
		}

		GB_ASSERT(v.addr.value != nullptr);
		return v;
	case_end;

	case_ast_node(i, Ident, expr);
		if (is_blank_ident(expr)) {
			lbAddr val = {};
			return val;
		}
		String name = i->token.string;
		Entity *e = entity_of_node(expr);
		return lb_build_addr_from_entity(p, e, expr);
	case_end;

	case_ast_node(se, SelectorExpr, expr);
		Ast *sel = unparen_expr(se->selector);
		if (sel->kind == Ast_Ident) {
			String selector = sel->Ident.token.string;
			TypeAndValue tav = type_and_value_of_expr(se->expr);

			if (tav.mode == Addressing_Invalid) {
				// NOTE(bill): Imports
				Entity *imp = entity_of_node(se->expr);
				if (imp != nullptr) {
					GB_ASSERT(imp->kind == Entity_ImportName);
				}
				return lb_build_addr(p, unparen_expr(se->selector));
			}


			Type *type = base_type(tav.type);
			if (tav.mode == Addressing_Type) { // Addressing_Type
				GB_PANIC("Unreachable");
			}

			if (se->swizzle_count > 0) {
				Type *array_type = base_type(type_deref(tav.type));
				GB_ASSERT(array_type->kind == Type_Array);
				u8 swizzle_count = se->swizzle_count;
				u8 swizzle_indices_raw = se->swizzle_indices;
				u8 swizzle_indices[4] = {};
				for (u8 i = 0; i < swizzle_count; i++) {
					u8 index = swizzle_indices_raw>>(i*2) & 3;
					swizzle_indices[i] = index;
				}
				lbValue a = {};
				if (is_type_pointer(tav.type)) {
					a = lb_build_expr(p, se->expr);
				} else {
					lbAddr addr = lb_build_addr(p, se->expr);
					a = lb_addr_get_ptr(p, addr);
				}

				GB_ASSERT(is_type_array(expr->tav.type));
				return lb_addr_swizzle(a, expr->tav.type, swizzle_count, swizzle_indices);
			}

			Selection sel = lookup_field(type, selector, false);
			GB_ASSERT(sel.entity != nullptr);

			{
				lbAddr addr = lb_build_addr(p, se->expr);
				if (addr.kind == lbAddr_Map) {
					lbValue v = lb_addr_load(p, addr);
					lbValue a = lb_address_from_load_or_generate_local(p, v);
					a = lb_emit_deep_field_gep(p, a, sel);
					return lb_addr(a);
				} else if (addr.kind == lbAddr_Context) {
					GB_ASSERT(sel.index.count > 0);
					if (addr.ctx.sel.index.count >= 0) {
						sel = selection_combine(addr.ctx.sel, sel);
					}
					addr.ctx.sel = sel;
					addr.kind = lbAddr_Context;
					return addr;
				} else if (addr.kind == lbAddr_SoaVariable) {
					lbValue index = addr.soa.index;
					i32 first_index = sel.index[0];
					Selection sub_sel = sel;
					sub_sel.index.data += 1;
					sub_sel.index.count -= 1;

					lbValue arr = lb_emit_struct_ep(p, addr.addr, first_index);

					Type *t = base_type(type_deref(addr.addr.type));
					GB_ASSERT(is_type_soa_struct(t));

					if (addr.soa.index_expr != nullptr && (!lb_is_const(addr.soa.index) || t->Struct.soa_kind != StructSoa_Fixed)) {
						lbValue len = lb_soa_struct_len(p, addr.addr);
						lb_emit_bounds_check(p, ast_token(addr.soa.index_expr), addr.soa.index, len);
					}

					lbValue item = {};

					if (t->Struct.soa_kind == StructSoa_Fixed) {
						item = lb_emit_array_ep(p, arr, index);
					} else {
						item = lb_emit_ptr_offset(p, lb_emit_load(p, arr), index);
					}
					if (sub_sel.index.count > 0) {
						item = lb_emit_deep_field_gep(p, item, sub_sel);
					}
					return lb_addr(item);
				} else if (addr.kind == lbAddr_Swizzle) {
					GB_ASSERT(sel.index.count > 0);
					// NOTE(bill): just patch the index in place
					sel.index[0] = addr.swizzle.indices[sel.index[0]];
				}
				lbValue a = lb_addr_get_ptr(p, addr);
				a = lb_emit_deep_field_gep(p, a, sel);
				return lb_addr(a);
			}
		} else {
			GB_PANIC("Unsupported selector expression");
		}
	case_end;

	case_ast_node(se, SelectorCallExpr, expr);
		GB_ASSERT(se->modified_call);
		TypeAndValue tav = type_and_value_of_expr(expr);
		GB_ASSERT(tav.mode != Addressing_Invalid);
		lbValue e = lb_build_expr(p, expr);
		return lb_addr(lb_address_from_load_or_generate_local(p, e));
	case_end;

	case_ast_node(ta, TypeAssertion, expr);
		TokenPos pos = ast_token(expr).pos;
		lbValue e = lb_build_expr(p, ta->expr);
		Type *t = type_deref(e.type);
		if (is_type_union(t)) {
			Type *type = type_of_expr(expr);
			lbAddr v = lb_add_local_generated(p, type, false);
			lb_addr_store(p, v, lb_emit_union_cast(p, lb_build_expr(p, ta->expr), type, pos));
			return v;
		} else if (is_type_any(t)) {
			Type *type = type_of_expr(expr);
			return lb_emit_any_cast_addr(p, lb_build_expr(p, ta->expr), type, pos);
		} else {
			GB_PANIC("TODO(bill): type assertion %s", type_to_string(e.type));
		}
	case_end;

	case_ast_node(ue, UnaryExpr, expr);
		switch (ue->op.kind) {
		case Token_And:
			return lb_build_addr(p, ue->expr);
		default:
			GB_PANIC("Invalid unary expression for lb_build_addr");
		}
	case_end;
	case_ast_node(be, BinaryExpr, expr);
		lbValue v = lb_build_expr(p, expr);
		Type *t = v.type;
		if (is_type_pointer(t)) {
			return lb_addr(v);
		}
		return lb_addr(lb_address_from_load_or_generate_local(p, v));
	case_end;

	case_ast_node(ie, IndexExpr, expr);
		Type *t = base_type(type_of_expr(ie->expr));

		bool deref = is_type_pointer(t);
		t = base_type(type_deref(t));
		if (is_type_soa_struct(t)) {
			// SOA STRUCTURES!!!!
			lbValue val = lb_build_addr_ptr(p, ie->expr);
			if (deref) {
				val = lb_emit_load(p, val);
			}

			lbValue index = lb_build_expr(p, ie->index);
			return lb_addr_soa_variable(val, index, ie->index);
		}

		if (ie->expr->tav.mode == Addressing_SoaVariable) {
			// SOA Structures for slices/dynamic arrays
			GB_ASSERT(is_type_pointer(type_of_expr(ie->expr)));

			lbValue field = lb_build_expr(p, ie->expr);
			lbValue index = lb_build_expr(p, ie->index);


			if (!build_context.no_bounds_check) {
				// TODO HACK(bill): Clean up this hack to get the length for bounds checking
				// GB_ASSERT(LLVMIsALoadInst(field.value));

				// lbValue a = {};
				// a.value = LLVMGetOperand(field.value, 0);
				// a.type = alloc_type_pointer(field.type);

				// irInstr *b = &a->Instr;
				// GB_ASSERT(b->kind == irInstr_StructElementPtr);
				// lbValue base_struct = b->StructElementPtr.address;

				// GB_ASSERT(is_type_soa_struct(type_deref(ir_type(base_struct))));
				// lbValue len = ir_soa_struct_len(p, base_struct);
				// lb_emit_bounds_check(p, ast_token(ie->index), index, len);
			}

			lbValue val = lb_emit_ptr_offset(p, field, index);
			return lb_addr(val);
		}

		GB_ASSERT_MSG(is_type_indexable(t), "%s %s", type_to_string(t), expr_to_string(expr));

		if (is_type_map(t)) {
			lbValue map_val = lb_build_addr_ptr(p, ie->expr);
			if (deref) {
				map_val = lb_emit_load(p, map_val);
			}

			lbValue key = lb_build_expr(p, ie->index);
			key = lb_emit_conv(p, key, t->Map.key);

			Type *result_type = type_of_expr(expr);
			return lb_addr_map(map_val, key, t, result_type);
		}

		switch (t->kind) {
		case Type_Array: {
			lbValue array = {};
			array = lb_build_addr_ptr(p, ie->expr);
			if (deref) {
				array = lb_emit_load(p, array);
			}
			lbValue index = lb_build_expr(p, ie->index);
			index = lb_emit_conv(p, index, t_int);
			lbValue elem = lb_emit_array_ep(p, array, index);

			auto index_tv = type_and_value_of_expr(ie->index);
			if (index_tv.mode != Addressing_Constant) {
				lbValue len = lb_const_int(p->module, t_int, t->Array.count);
				lb_emit_bounds_check(p, ast_token(ie->index), index, len);
			}
			return lb_addr(elem);
		}

		case Type_EnumeratedArray: {
			lbValue array = {};
			array = lb_build_addr_ptr(p, ie->expr);
			if (deref) {
				array = lb_emit_load(p, array);
			}

			Type *index_type = t->EnumeratedArray.index;

			auto index_tv = type_and_value_of_expr(ie->index);

			lbValue index = {};
			if (compare_exact_values(Token_NotEq, t->EnumeratedArray.min_value, exact_value_i64(0))) {
				if (index_tv.mode == Addressing_Constant) {
					ExactValue idx = exact_value_sub(index_tv.value, t->EnumeratedArray.min_value);
					index = lb_const_value(p->module, index_type, idx);
				} else {
					index = lb_emit_conv(p, lb_build_expr(p, ie->index), t_int);
					index = lb_emit_arith(p, Token_Sub, index, lb_const_value(p->module, index_type, t->EnumeratedArray.min_value), index_type);
				}
			} else {
				index = lb_emit_conv(p, lb_build_expr(p, ie->index), t_int);
			}

			lbValue elem = lb_emit_array_ep(p, array, index);

			if (index_tv.mode != Addressing_Constant) {
				lbValue len = lb_const_int(p->module, t_int, t->EnumeratedArray.count);
				lb_emit_bounds_check(p, ast_token(ie->index), index, len);
			}
			return lb_addr(elem);
		}

		case Type_Slice: {
			lbValue slice = {};
			slice = lb_build_expr(p, ie->expr);
			if (deref) {
				slice = lb_emit_load(p, slice);
			}
			lbValue elem = lb_slice_elem(p, slice);
			lbValue index = lb_emit_conv(p, lb_build_expr(p, ie->index), t_int);
			lbValue len = lb_slice_len(p, slice);
			lb_emit_bounds_check(p, ast_token(ie->index), index, len);
			lbValue v = lb_emit_ptr_offset(p, elem, index);
			return lb_addr(v);
		}

		case Type_RelativeSlice: {
			lbAddr slice_addr = {};
			if (deref) {
				slice_addr = lb_addr(lb_build_expr(p, ie->expr));
			} else {
				slice_addr = lb_build_addr(p, ie->expr);
			}
			lbValue slice = lb_addr_load(p, slice_addr);

			lbValue elem = lb_slice_elem(p, slice);
			lbValue index = lb_emit_conv(p, lb_build_expr(p, ie->index), t_int);
			lbValue len = lb_slice_len(p, slice);
			lb_emit_bounds_check(p, ast_token(ie->index), index, len);
			lbValue v = lb_emit_ptr_offset(p, elem, index);
			return lb_addr(v);
		}

		case Type_DynamicArray: {
			lbValue dynamic_array = {};
			dynamic_array = lb_build_expr(p, ie->expr);
			if (deref) {
				dynamic_array = lb_emit_load(p, dynamic_array);
			}
			lbValue elem = lb_dynamic_array_elem(p, dynamic_array);
			lbValue len = lb_dynamic_array_len(p, dynamic_array);
			lbValue index = lb_emit_conv(p, lb_build_expr(p, ie->index), t_int);
			lb_emit_bounds_check(p, ast_token(ie->index), index, len);
			lbValue v = lb_emit_ptr_offset(p, elem, index);
			return lb_addr(v);
		}


		case Type_Basic: { // Basic_string
			lbValue str;
			lbValue elem;
			lbValue len;
			lbValue index;

			str = lb_build_expr(p, ie->expr);
			if (deref) {
				str = lb_emit_load(p, str);
			}
			elem = lb_string_elem(p, str);
			len = lb_string_len(p, str);

			index = lb_emit_conv(p, lb_build_expr(p, ie->index), t_int);
			lb_emit_bounds_check(p, ast_token(ie->index), index, len);

			return lb_addr(lb_emit_ptr_offset(p, elem, index));
		}
		}
	case_end;

	case_ast_node(se, SliceExpr, expr);

		lbValue low  = lb_const_int(p->module, t_int, 0);
		lbValue high = {};

		if (se->low  != nullptr) low  = lb_build_expr(p, se->low);
		if (se->high != nullptr) high = lb_build_expr(p, se->high);

		bool no_indices = se->low == nullptr && se->high == nullptr;

		lbAddr addr = lb_build_addr(p, se->expr);
		lbValue base = lb_addr_load(p, addr);
		Type *type = base_type(base.type);

		if (is_type_pointer(type)) {
			type = base_type(type_deref(type));
			addr = lb_addr(base);
			base = lb_addr_load(p, addr);
		}

		switch (type->kind) {
		case Type_Slice: {
			Type *slice_type = type;
			lbValue len = lb_slice_len(p, base);
			if (high.value == nullptr) high = len;

			if (!no_indices) {
				lb_emit_slice_bounds_check(p, se->open, low, high, len, se->low != nullptr);
			}

			lbValue elem    = lb_emit_ptr_offset(p, lb_slice_elem(p, base), low);
			lbValue new_len = lb_emit_arith(p, Token_Sub, high, low, t_int);

			lbAddr slice = lb_add_local_generated(p, slice_type, false);
			lb_fill_slice(p, slice, elem, new_len);
			return slice;
		}

		case Type_RelativeSlice:
			GB_PANIC("TODO(bill): Type_RelativeSlice should be handled above already on the lb_addr_load");
			break;

		case Type_DynamicArray: {
			Type *elem_type = type->DynamicArray.elem;
			Type *slice_type = alloc_type_slice(elem_type);

			lbValue len = lb_dynamic_array_len(p, base);
			if (high.value == nullptr) high = len;

			if (!no_indices) {
				lb_emit_slice_bounds_check(p, se->open, low, high, len, se->low != nullptr);
			}

			lbValue elem    = lb_emit_ptr_offset(p, lb_dynamic_array_elem(p, base), low);
			lbValue new_len = lb_emit_arith(p, Token_Sub, high, low, t_int);

			lbAddr slice = lb_add_local_generated(p, slice_type, false);
			lb_fill_slice(p, slice, elem, new_len);
			return slice;
		}


		case Type_Array: {
			Type *slice_type = alloc_type_slice(type->Array.elem);
			lbValue len = lb_const_int(p->module, t_int, type->Array.count);

			if (high.value == nullptr) high = len;

			bool low_const  = type_and_value_of_expr(se->low).mode  == Addressing_Constant;
			bool high_const = type_and_value_of_expr(se->high).mode == Addressing_Constant;

			if (!low_const || !high_const) {
				if (!no_indices) {
					lb_emit_slice_bounds_check(p, se->open, low, high, len, se->low != nullptr);
				}
			}
			lbValue elem    = lb_emit_ptr_offset(p, lb_array_elem(p, lb_addr_get_ptr(p, addr)), low);
			lbValue new_len = lb_emit_arith(p, Token_Sub, high, low, t_int);

			lbAddr slice = lb_add_local_generated(p, slice_type, false);
			lb_fill_slice(p, slice, elem, new_len);
			return slice;
		}

		case Type_Basic: {
			GB_ASSERT(type == t_string);
			lbValue len = lb_string_len(p, base);
			if (high.value == nullptr) high = len;

			if (!no_indices) {
				lb_emit_slice_bounds_check(p, se->open, low, high, len, se->low != nullptr);
			}

			lbValue elem    = lb_emit_ptr_offset(p, lb_string_elem(p, base), low);
			lbValue new_len = lb_emit_arith(p, Token_Sub, high, low, t_int);

			lbAddr str = lb_add_local_generated(p, t_string, false);
			lb_fill_string(p, str, elem, new_len);
			return str;
		}


		case Type_Struct:
			if (is_type_soa_struct(type)) {
				lbValue len = lb_soa_struct_len(p, lb_addr_get_ptr(p, addr));
				if (high.value == nullptr) high = len;

				if (!no_indices) {
					lb_emit_slice_bounds_check(p, se->open, low, high, len, se->low != nullptr);
				}
				#if 1

				lbAddr dst = lb_add_local_generated(p, type_of_expr(expr), true);
				if (type->Struct.soa_kind == StructSoa_Fixed) {
					i32 field_count = cast(i32)type->Struct.fields.count;
					for (i32 i = 0; i < field_count; i++) {
						lbValue field_dst = lb_emit_struct_ep(p, dst.addr, i);
						lbValue field_src = lb_emit_struct_ep(p, lb_addr_get_ptr(p, addr), i);
						field_src = lb_emit_array_ep(p, field_src, low);
						lb_emit_store(p, field_dst, field_src);
					}

					lbValue len_dst = lb_emit_struct_ep(p, dst.addr, field_count);
					lbValue new_len = lb_emit_arith(p, Token_Sub, high, low, t_int);
					lb_emit_store(p, len_dst, new_len);
				} else if (type->Struct.soa_kind == StructSoa_Slice) {
					if (no_indices) {
						lb_addr_store(p, dst, base);
					} else {
						i32 field_count = cast(i32)type->Struct.fields.count - 1;
						for (i32 i = 0; i < field_count; i++) {
							lbValue field_dst = lb_emit_struct_ep(p, dst.addr, i);
							lbValue field_src = lb_emit_struct_ev(p, base, i);
							field_src = lb_emit_ptr_offset(p, field_src, low);
							lb_emit_store(p, field_dst, field_src);
						}


						lbValue len_dst = lb_emit_struct_ep(p, dst.addr, field_count);
						lbValue new_len = lb_emit_arith(p, Token_Sub, high, low, t_int);
						lb_emit_store(p, len_dst, new_len);
					}
				} else if (type->Struct.soa_kind == StructSoa_Dynamic) {
					i32 field_count = cast(i32)type->Struct.fields.count - 3;
					for (i32 i = 0; i < field_count; i++) {
						lbValue field_dst = lb_emit_struct_ep(p, dst.addr, i);
						lbValue field_src = lb_emit_struct_ev(p, base, i);
						field_src = lb_emit_ptr_offset(p, field_src, low);
						lb_emit_store(p, field_dst, field_src);
					}


					lbValue len_dst = lb_emit_struct_ep(p, dst.addr, field_count);
					lbValue new_len = lb_emit_arith(p, Token_Sub, high, low, t_int);
					lb_emit_store(p, len_dst, new_len);
				}

				return dst;
				#endif
			}
			break;

		}

		GB_PANIC("Unknown slicable type");
	case_end;

	case_ast_node(de, DerefExpr, expr);
		if (is_type_relative_pointer(type_of_expr(de->expr))) {
			lbAddr addr = lb_build_addr(p, de->expr);
			addr.relative.deref = true;
			return addr;\
		}
		lbValue addr = lb_build_expr(p, de->expr);
		return lb_addr(addr);
	case_end;

	case_ast_node(ce, CallExpr, expr);
		// NOTE(bill): This is make sure you never need to have an 'array_ev'
		lbValue e = lb_build_expr(p, expr);
	#if 1
		return lb_addr(lb_address_from_load_or_generate_local(p, e));
	#else
		lbAddr v = lb_add_local_generated(p, e.type, false);
		lb_addr_store(p, v, e);
		return v;
	#endif
	case_end;

	case_ast_node(cl, CompoundLit, expr);
		Type *type = type_of_expr(expr);
		Type *bt = base_type(type);

		lbAddr v = lb_add_local_generated(p, type, true);

		Type *et = nullptr;
		switch (bt->kind) {
		case Type_Array:           et = bt->Array.elem;           break;
		case Type_EnumeratedArray: et = bt->EnumeratedArray.elem; break;
		case Type_Slice:           et = bt->Slice.elem;           break;
		case Type_BitSet:          et = bt->BitSet.elem;          break;
		case Type_SimdVector:      et = bt->SimdVector.elem;      break;
		}

		String proc_name = {};
		if (p->entity) {
			proc_name = p->entity->token.string;
		}
		TokenPos pos = ast_token(expr).pos;

		switch (bt->kind) {
		default: GB_PANIC("Unknown CompoundLit type: %s", type_to_string(type)); break;

		case Type_Struct: {
			// TODO(bill): "constant" '#raw_union's are not initialized constantly at the moment.
			// NOTE(bill): This is due to the layout of the unions when printed to LLVM-IR
			bool is_raw_union = is_type_raw_union(bt);
			GB_ASSERT(is_type_struct(bt) || is_raw_union);
			TypeStruct *st = &bt->Struct;
			if (cl->elems.count > 0) {
				lb_addr_store(p, v, lb_const_value(p->module, type, exact_value_compound(expr)));
				lbValue comp_lit_ptr = lb_addr_get_ptr(p, v);

				for_array(field_index, cl->elems) {
					Ast *elem = cl->elems[field_index];

					lbValue field_expr = {};
					Entity *field = nullptr;
					isize index = field_index;

					if (elem->kind == Ast_FieldValue) {
						ast_node(fv, FieldValue, elem);
						String name = fv->field->Ident.token.string;
						Selection sel = lookup_field(bt, name, false);
						index = sel.index[0];
						elem = fv->value;
						TypeAndValue tav = type_and_value_of_expr(elem);
					} else {
						TypeAndValue tav = type_and_value_of_expr(elem);
						Selection sel = lookup_field_from_index(bt, st->fields[field_index]->Variable.field_src_index);
						index = sel.index[0];
					}

					field = st->fields[index];
					Type *ft = field->type;
					if (!is_raw_union && !is_type_typeid(ft) && lb_is_elem_const(elem, ft)) {
						continue;
					}

					field_expr = lb_build_expr(p, elem);

					lbValue gep = {};
					if (is_raw_union) {
						gep = lb_emit_conv(p, comp_lit_ptr, alloc_type_pointer(ft));
					} else {
						gep = lb_emit_struct_ep(p, comp_lit_ptr, cast(i32)index);
					}

					Type *fet = field_expr.type;
					GB_ASSERT(fet->kind != Type_Tuple);

					// HACK TODO(bill): THIS IS A MASSIVE HACK!!!!
					if (is_type_union(ft) && !are_types_identical(fet, ft) && !is_type_untyped(fet)) {
						GB_ASSERT_MSG(union_variant_index(ft, fet) > 0, "%s", type_to_string(fet));

						lb_emit_store_union_variant(p, gep, field_expr, fet);
					} else {
						lbValue fv = lb_emit_conv(p, field_expr, ft);
						lb_emit_store(p, gep, fv);
					}
				}
			}
			break;
		}

		case Type_Map: {
			if (cl->elems.count == 0) {
				break;
			}
			{
				auto args = array_make<lbValue>(permanent_allocator(), 3);
				args[0] = lb_gen_map_header(p, v.addr, type);
				args[1] = lb_const_int(p->module, t_int, 2*cl->elems.count);
				args[2] = lb_emit_source_code_location(p, proc_name, pos);
				lb_emit_runtime_call(p, "__dynamic_map_reserve", args);
			}
			for_array(field_index, cl->elems) {
				Ast *elem = cl->elems[field_index];
				ast_node(fv, FieldValue, elem);

				lbValue key   = lb_build_expr(p, fv->field);
				lbValue value = lb_build_expr(p, fv->value);
				lb_insert_dynamic_map_key_and_value(p, v, type, key, value, elem);
			}
			break;
		}

		case Type_Array: {
			if (cl->elems.count > 0) {
				lb_addr_store(p, v, lb_const_value(p->module, type, exact_value_compound(expr)));

				auto temp_data = array_make<lbCompoundLitElemTempData>(temporary_allocator(), 0, cl->elems.count);

				// NOTE(bill): Separate value, gep, store into their own chunks
				for_array(i, cl->elems) {
					Ast *elem = cl->elems[i];
					if (elem->kind == Ast_FieldValue) {
						ast_node(fv, FieldValue, elem);
						if (lb_is_elem_const(fv->value, et)) {
							continue;
						}
						if (is_ast_range(fv->field)) {
							ast_node(ie, BinaryExpr, fv->field);
							TypeAndValue lo_tav = ie->left->tav;
							TypeAndValue hi_tav = ie->right->tav;
							GB_ASSERT(lo_tav.mode == Addressing_Constant);
							GB_ASSERT(hi_tav.mode == Addressing_Constant);

							TokenKind op = ie->op.kind;
							i64 lo = exact_value_to_i64(lo_tav.value);
							i64 hi = exact_value_to_i64(hi_tav.value);
							if (op != Token_RangeHalf) {
								hi += 1;
							}

							lbValue value = lb_build_expr(p, fv->value);

							for (i64 k = lo; k < hi; k++) {
								lbCompoundLitElemTempData data = {};
								data.value = value;
								data.elem_index = cast(i32)k;
								array_add(&temp_data, data);
							}

						} else {
							auto tav = fv->field->tav;
							GB_ASSERT(tav.mode == Addressing_Constant);
							i64 index = exact_value_to_i64(tav.value);

							lbValue value = lb_build_expr(p, fv->value);
							lbCompoundLitElemTempData data = {};
							data.value = lb_emit_conv(p, value, et);
							data.expr = fv->value;
							data.elem_index = cast(i32)index;
							array_add(&temp_data, data);
						}

					} else {
						if (lb_is_elem_const(elem, et)) {
							continue;
						}
						lbCompoundLitElemTempData data = {};
						data.expr = elem;
						data.elem_index = cast(i32)i;
						array_add(&temp_data, data);
					}
				}

				for_array(i, temp_data) {
					temp_data[i].gep = lb_emit_array_epi(p, lb_addr_get_ptr(p, v), temp_data[i].elem_index);
				}

				for_array(i, temp_data) {
					lbValue field_expr = temp_data[i].value;
					Ast *expr = temp_data[i].expr;

					auto prev_hint = lb_set_copy_elision_hint(p, lb_addr(temp_data[i].gep), expr);

					if (field_expr.value == nullptr) {
						field_expr = lb_build_expr(p, expr);
					}
					Type *t = field_expr.type;
					GB_ASSERT(t->kind != Type_Tuple);
					lbValue ev = lb_emit_conv(p, field_expr, et);

					if (!p->copy_elision_hint.used) {
						temp_data[i].value = ev;
					}

					lb_reset_copy_elision_hint(p, prev_hint);
				}

				for_array(i, temp_data) {
					if (temp_data[i].value.value != nullptr) {
						lb_emit_store(p, temp_data[i].gep, temp_data[i].value);
					}
				}
			}
			break;
		}
		case Type_EnumeratedArray: {
			if (cl->elems.count > 0) {
				lb_addr_store(p, v, lb_const_value(p->module, type, exact_value_compound(expr)));

				auto temp_data = array_make<lbCompoundLitElemTempData>(temporary_allocator(), 0, cl->elems.count);

				// NOTE(bill): Separate value, gep, store into their own chunks
				for_array(i, cl->elems) {
					Ast *elem = cl->elems[i];
					if (elem->kind == Ast_FieldValue) {
						ast_node(fv, FieldValue, elem);
						if (lb_is_elem_const(fv->value, et)) {
							continue;
						}
						if (is_ast_range(fv->field)) {
							ast_node(ie, BinaryExpr, fv->field);
							TypeAndValue lo_tav = ie->left->tav;
							TypeAndValue hi_tav = ie->right->tav;
							GB_ASSERT(lo_tav.mode == Addressing_Constant);
							GB_ASSERT(hi_tav.mode == Addressing_Constant);

							TokenKind op = ie->op.kind;
							i64 lo = exact_value_to_i64(lo_tav.value);
							i64 hi = exact_value_to_i64(hi_tav.value);
							if (op != Token_RangeHalf) {
								hi += 1;
							}

							lbValue value = lb_build_expr(p, fv->value);

							for (i64 k = lo; k < hi; k++) {
								lbCompoundLitElemTempData data = {};
								data.value = value;
								data.elem_index = cast(i32)k;
								array_add(&temp_data, data);
							}

						} else {
							auto tav = fv->field->tav;
							GB_ASSERT(tav.mode == Addressing_Constant);
							i64 index = exact_value_to_i64(tav.value);

							lbValue value = lb_build_expr(p, fv->value);
							lbCompoundLitElemTempData data = {};
							data.value = lb_emit_conv(p, value, et);
							data.expr = fv->value;
							data.elem_index = cast(i32)index;
							array_add(&temp_data, data);
						}

					} else {
						if (lb_is_elem_const(elem, et)) {
							continue;
						}
						lbCompoundLitElemTempData data = {};
						data.expr = elem;
						data.elem_index = cast(i32)i;
						array_add(&temp_data, data);
					}
				}


				i32 index_offset = cast(i32)exact_value_to_i64(bt->EnumeratedArray.min_value);

				for_array(i, temp_data) {
					i32 index = temp_data[i].elem_index - index_offset;
					temp_data[i].gep = lb_emit_array_epi(p, lb_addr_get_ptr(p, v), index);
				}

				for_array(i, temp_data) {
					lbValue field_expr = temp_data[i].value;
					Ast *expr = temp_data[i].expr;

					auto prev_hint = lb_set_copy_elision_hint(p, lb_addr(temp_data[i].gep), expr);

					if (field_expr.value == nullptr) {
						field_expr = lb_build_expr(p, expr);
					}
					Type *t = field_expr.type;
					GB_ASSERT(t->kind != Type_Tuple);
					lbValue ev = lb_emit_conv(p, field_expr, et);

					if (!p->copy_elision_hint.used) {
						temp_data[i].value = ev;
					}

					lb_reset_copy_elision_hint(p, prev_hint);
				}

				for_array(i, temp_data) {
					if (temp_data[i].value.value != nullptr) {
						lb_emit_store(p, temp_data[i].gep, temp_data[i].value);
					}
				}
			}
			break;
		}
		case Type_Slice: {
			if (cl->elems.count > 0) {
				Type *elem_type = bt->Slice.elem;
				Type *elem_ptr_type = alloc_type_pointer(elem_type);
				Type *elem_ptr_ptr_type = alloc_type_pointer(elem_ptr_type);
				lbValue slice = lb_const_value(p->module, type, exact_value_compound(expr));

				lbValue data = lb_slice_elem(p, slice);

				auto temp_data = array_make<lbCompoundLitElemTempData>(temporary_allocator(), 0, cl->elems.count);

				for_array(i, cl->elems) {
					Ast *elem = cl->elems[i];
					if (elem->kind == Ast_FieldValue) {
						ast_node(fv, FieldValue, elem);

						if (lb_is_elem_const(fv->value, et)) {
							continue;
						}

						if (is_ast_range(fv->field)) {
							ast_node(ie, BinaryExpr, fv->field);
							TypeAndValue lo_tav = ie->left->tav;
							TypeAndValue hi_tav = ie->right->tav;
							GB_ASSERT(lo_tav.mode == Addressing_Constant);
							GB_ASSERT(hi_tav.mode == Addressing_Constant);

							TokenKind op = ie->op.kind;
							i64 lo = exact_value_to_i64(lo_tav.value);
							i64 hi = exact_value_to_i64(hi_tav.value);
							if (op != Token_RangeHalf) {
								hi += 1;
							}

							lbValue value = lb_emit_conv(p, lb_build_expr(p, fv->value), et);

							for (i64 k = lo; k < hi; k++) {
								lbCompoundLitElemTempData data = {};
								data.value = value;
								data.elem_index = cast(i32)k;
								array_add(&temp_data, data);
							}

						} else {
							GB_ASSERT(fv->field->tav.mode == Addressing_Constant);
							i64 index = exact_value_to_i64(fv->field->tav.value);

							lbValue field_expr = lb_build_expr(p, fv->value);
							GB_ASSERT(!is_type_tuple(field_expr.type));

							lbValue ev = lb_emit_conv(p, field_expr, et);

							lbCompoundLitElemTempData data = {};
							data.value = ev;
							data.elem_index = cast(i32)index;
							array_add(&temp_data, data);
						}
					} else {
						if (lb_is_elem_const(elem, et)) {
							continue;
						}
						lbValue field_expr = lb_build_expr(p, elem);
						GB_ASSERT(!is_type_tuple(field_expr.type));

						lbValue ev = lb_emit_conv(p, field_expr, et);

						lbCompoundLitElemTempData data = {};
						data.value = ev;
						data.elem_index = cast(i32)i;
						array_add(&temp_data, data);
					}
				}

				for_array(i, temp_data) {
					temp_data[i].gep = lb_emit_ptr_offset(p, data, lb_const_int(p->module, t_int, temp_data[i].elem_index));
				}

				for_array(i, temp_data) {
					lb_emit_store(p, temp_data[i].gep, temp_data[i].value);
				}

				{
					lbValue count = {};
					count.type = t_int;

					if (lb_is_const(slice)) {
						unsigned indices[1] = {1};
						count.value = LLVMConstExtractValue(slice.value, indices, gb_count_of(indices));
					} else {
						count.value = LLVMBuildExtractValue(p->builder, slice.value, 1, "");
					}
					lb_fill_slice(p, v, data, count);
				}
			}
			break;
		}

		case Type_DynamicArray: {
			if (cl->elems.count == 0) {
				break;
			}
			Type *et = bt->DynamicArray.elem;
			lbValue size  = lb_const_int(p->module, t_int, type_size_of(et));
			lbValue align = lb_const_int(p->module, t_int, type_align_of(et));

			i64 item_count = gb_max(cl->max_count, cl->elems.count);
			{

				auto args = array_make<lbValue>(permanent_allocator(), 5);
				args[0] = lb_emit_conv(p, lb_addr_get_ptr(p, v), t_rawptr);
				args[1] = size;
				args[2] = align;
				args[3] = lb_const_int(p->module, t_int, 2*item_count); // TODO(bill): Is this too much waste?
				args[4] = lb_emit_source_code_location(p, proc_name, pos);
				lb_emit_runtime_call(p, "__dynamic_array_reserve", args);
			}

			lbValue items = lb_generate_local_array(p, et, item_count);
			// lbValue items = lb_generate_global_array(p->module, et, item_count, str_lit("dacl$"), cast(i64)cast(intptr)expr);

			for_array(i, cl->elems) {
				Ast *elem = cl->elems[i];
				if (elem->kind == Ast_FieldValue) {
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
						if (op != Token_RangeHalf) {
							hi += 1;
						}

						lbValue value = lb_emit_conv(p, lb_build_expr(p, fv->value), et);

						for (i64 k = lo; k < hi; k++) {
							lbValue ep = lb_emit_array_epi(p, items, cast(i32)k);
							lb_emit_store(p, ep, value);
						}
					} else {
						GB_ASSERT(fv->field->tav.mode == Addressing_Constant);

						i64 field_index = exact_value_to_i64(fv->field->tav.value);

						lbValue ev = lb_build_expr(p, fv->value);
						lbValue value = lb_emit_conv(p, ev, et);
						lbValue ep = lb_emit_array_epi(p, items, cast(i32)field_index);
						lb_emit_store(p, ep, value);
					}
				} else {
					lbValue value = lb_emit_conv(p, lb_build_expr(p, elem), et);
					lbValue ep = lb_emit_array_epi(p, items, cast(i32)i);
					lb_emit_store(p, ep, value);
				}
			}

			{
				auto args = array_make<lbValue>(permanent_allocator(), 6);
				args[0] = lb_emit_conv(p, v.addr, t_rawptr);
				args[1] = size;
				args[2] = align;
				args[3] = lb_emit_conv(p, items, t_rawptr);
				args[4] = lb_const_int(p->module, t_int, item_count);
				args[5] = lb_emit_source_code_location(p, proc_name, pos);
				lb_emit_runtime_call(p, "__dynamic_array_append", args);
			}
			break;
		}

		case Type_Basic: {
			GB_ASSERT(is_type_any(bt));
			if (cl->elems.count > 0) {
				lb_addr_store(p, v, lb_const_value(p->module, type, exact_value_compound(expr)));
				String field_names[2] = {
					str_lit("data"),
					str_lit("id"),
				};
				Type *field_types[2] = {
					t_rawptr,
					t_typeid,
				};

				for_array(field_index, cl->elems) {
					Ast *elem = cl->elems[field_index];

					lbValue field_expr = {};
					isize index = field_index;

					if (elem->kind == Ast_FieldValue) {
						ast_node(fv, FieldValue, elem);
						Selection sel = lookup_field(bt, fv->field->Ident.token.string, false);
						index = sel.index[0];
						elem = fv->value;
					} else {
						TypeAndValue tav = type_and_value_of_expr(elem);
						Selection sel = lookup_field(bt, field_names[field_index], false);
						index = sel.index[0];
					}

					field_expr = lb_build_expr(p, elem);

					GB_ASSERT(field_expr.type->kind != Type_Tuple);

					Type *ft = field_types[index];
					lbValue fv = lb_emit_conv(p, field_expr, ft);
					lbValue gep = lb_emit_struct_ep(p, lb_addr_get_ptr(p, v), cast(i32)index);
					lb_emit_store(p, gep, fv);
				}
			}

			break;
		}

		case Type_BitSet: {
			i64 sz = type_size_of(type);
			if (cl->elems.count > 0 && sz > 0) {
				lb_addr_store(p, v, lb_const_value(p->module, type, exact_value_compound(expr)));

				lbValue lower = lb_const_value(p->module, t_int, exact_value_i64(bt->BitSet.lower));
				for_array(i, cl->elems) {
					Ast *elem = cl->elems[i];
					GB_ASSERT(elem->kind != Ast_FieldValue);

					if (lb_is_elem_const(elem, et)) {
						continue;
					}

					lbValue expr = lb_build_expr(p, elem);
					GB_ASSERT(expr.type->kind != Type_Tuple);

					Type *it = bit_set_to_int(bt);
					lbValue one = lb_const_value(p->module, it, exact_value_i64(1));
					lbValue e = lb_emit_conv(p, expr, it);
					e = lb_emit_arith(p, Token_Sub, e, lower, it);
					e = lb_emit_arith(p, Token_Shl, one, e, it);

					lbValue old_value = lb_emit_transmute(p, lb_addr_load(p, v), it);
					lbValue new_value = lb_emit_arith(p, Token_Or, old_value, e, it);
					new_value = lb_emit_transmute(p, new_value, type);
					lb_addr_store(p, v, new_value);
				}
			}
			break;
		}

		}

		return v;
	case_end;

	case_ast_node(tc, TypeCast, expr);
		Type *type = type_of_expr(expr);
		lbValue x = lb_build_expr(p, tc->expr);
		lbValue e = {};
		switch (tc->token.kind) {
		case Token_cast:
			e = lb_emit_conv(p, x, type);
			break;
		case Token_transmute:
			e = lb_emit_transmute(p, x, type);
			break;
		default:
			GB_PANIC("Invalid AST TypeCast");
		}
		lbAddr v = lb_add_local_generated(p, type, false);
		lb_addr_store(p, v, e);
		return v;
	case_end;

	case_ast_node(ac, AutoCast, expr);
		return lb_build_addr(p, ac->expr);
	case_end;
	}

	TokenPos token_pos = ast_token(expr).pos;
	GB_PANIC("Unexpected address expression\n"
	         "\tAst: %.*s @ "
	         "%s\n",
	         LIT(ast_strings[expr->kind]),
	         token_pos_to_string(token_pos));


	return {};
}

void lb_init_module(lbModule *m, Checker *c) {
	m->info = &c->info;

	gbString module_name = gb_string_make(heap_allocator(), "odin_package");
	if (m->pkg) {
		module_name = gb_string_appendc(module_name, "-");
		module_name = gb_string_append_length(module_name, m->pkg->name.text, m->pkg->name.len);
	} else if (USE_SEPARTE_MODULES) {
		module_name = gb_string_appendc(module_name, "-builtin");
	}

	m->ctx = LLVMContextCreate();
	m->mod = LLVMModuleCreateWithNameInContext(module_name ? module_name : "odin_package", m->ctx);
	// m->debug_builder = nullptr;
	if (build_context.ODIN_DEBUG) {
		enum {DEBUG_METADATA_VERSION = 3};

		LLVMMetadataRef debug_ref = LLVMValueAsMetadata(LLVMConstInt(LLVMInt32TypeInContext(m->ctx), DEBUG_METADATA_VERSION, true));
		LLVMAddModuleFlag(m->mod, LLVMModuleFlagBehaviorWarning, "Debug Info Version", 18, debug_ref);

		switch (build_context.metrics.os) {
		case TargetOs_windows:
			LLVMAddModuleFlag(m->mod,
				LLVMModuleFlagBehaviorWarning,
				"CodeView", 8,
				LLVMValueAsMetadata(LLVMConstInt(LLVMInt32TypeInContext(m->ctx), 1, true)));
			break;

		case TargetOs_darwin:
			// NOTE(bill): Darwin only supports DWARF2 (that I know of)
			LLVMAddModuleFlag(m->mod,
				LLVMModuleFlagBehaviorWarning,
				"Dwarf Version", 13,
				LLVMValueAsMetadata(LLVMConstInt(LLVMInt32TypeInContext(m->ctx), 2, true)));
			break;
		}
		m->debug_builder = LLVMCreateDIBuilder(m->mod);
	}

	gbAllocator a = heap_allocator();
	map_init(&m->types, a);
	map_init(&m->llvm_types, a);
	map_init(&m->values, a);
	map_init(&m->soa_values, a);
	string_map_init(&m->members, a);
	map_init(&m->procedure_values, a);
	string_map_init(&m->procedures, a);
	string_map_init(&m->const_strings, a);
	map_init(&m->anonymous_proc_lits, a);
	map_init(&m->function_type_map, a);
	map_init(&m->equal_procs, a);
	map_init(&m->hasher_procs, a);
	array_init(&m->procedures_to_generate, a, 0, 1024);
	array_init(&m->foreign_library_paths,  a, 0, 1024);

	map_init(&m->debug_values, a);
	array_init(&m->debug_incomplete_types, a, 0, 1024);

}


bool lb_init_generator(lbGenerator *gen, Checker *c) {
	if (global_error_collector.count != 0) {
		return false;
	}

	isize tc = c->parser->total_token_count;
	if (tc < 2) {
		return false;
	}


	String init_fullpath = c->parser->init_fullpath;

	if (build_context.out_filepath.len == 0) {
		gen->output_name = remove_directory_from_path(init_fullpath);
		gen->output_name = remove_extension_from_path(gen->output_name);
		gen->output_name = string_trim_whitespace(gen->output_name);
		if (gen->output_name.len == 0) {
			gen->output_name = c->info.init_scope->pkg->name;
		}
		gen->output_base = gen->output_name;
	} else {
		gen->output_name = build_context.out_filepath;
		gen->output_name = string_trim_whitespace(gen->output_name);
		if (gen->output_name.len == 0) {
			gen->output_name = c->info.init_scope->pkg->name;
		}
		isize pos = string_extension_position(gen->output_name);
		if (pos < 0) {
			gen->output_base = gen->output_name;
		} else {
			gen->output_base = substring(gen->output_name, 0, pos);
		}
	}
	gbAllocator ha = heap_allocator();
	array_init(&gen->output_object_paths, ha);
	array_init(&gen->output_temp_paths, ha);

	gen->output_base = path_to_full_path(ha, gen->output_base);

	gbString output_file_path = gb_string_make_length(ha, gen->output_base.text, gen->output_base.len);
	output_file_path = gb_string_appendc(output_file_path, ".obj");
	defer (gb_string_free(output_file_path));

	gen->info = &c->info;

	map_init(&gen->modules, permanent_allocator(), gen->info->packages.entries.count*2);
	map_init(&gen->modules_through_ctx, permanent_allocator(), gen->info->packages.entries.count*2);
	map_init(&gen->anonymous_proc_lits, heap_allocator(), 1024);

	gb_mutex_init(&gen->mutex);

	if (USE_SEPARTE_MODULES) {
		for_array(i, gen->info->packages.entries) {
			AstPackage *pkg = gen->info->packages.entries[i].value;

			auto m = gb_alloc_item(permanent_allocator(), lbModule);
			m->pkg = pkg;
			m->gen = gen;
			map_set(&gen->modules, hash_pointer(pkg), m);
			lb_init_module(m, c);
		}
	}

	gen->default_module.gen = gen;
	map_set(&gen->modules, hash_pointer(nullptr), &gen->default_module);
	lb_init_module(&gen->default_module, c);


	for_array(i, gen->modules.entries) {
		lbModule *m = gen->modules.entries[i].value;
		LLVMContextRef ctx = LLVMGetModuleContext(m->mod);
		map_set(&gen->modules_through_ctx, hash_pointer(ctx), m);
	}

	return true;
}

lbAddr lb_add_global_generated(lbModule *m, Type *type, lbValue value) {
	GB_ASSERT(type != nullptr);
	type = default_type(type);

	isize max_len = 7+8+1;
	u8 *str = cast(u8 *)gb_alloc_array(permanent_allocator(), u8, max_len);

	u32 id = cast(u32)gb_atomic32_fetch_add(&m->gen->global_generated_index, 1);

	isize len = gb_snprintf(cast(char *)str, max_len, "ggv$%x", id);
	String name = make_string(str, len-1);

	Scope *scope = nullptr;
	Entity *e = alloc_entity_variable(scope, make_token_ident(name), type);
	lbValue g = {};
	g.type = alloc_type_pointer(type);
	g.value = LLVMAddGlobal(m->mod, lb_type(m, type), cast(char const *)str);
	if (value.value != nullptr) {
		GB_ASSERT_MSG(LLVMIsConstant(value.value), LLVMPrintValueToString(value.value));
		LLVMSetInitializer(g.value, value.value);
	} else {
		LLVMSetInitializer(g.value, LLVMConstNull(lb_type(m, type)));
	}

	lb_add_entity(m, e, g);
	lb_add_member(m, name, g);
	return lb_addr(g);
}

lbValue lb_find_runtime_value(lbModule *m, String const &name) {
	AstPackage *p = m->info->runtime_package;
	Entity *e = scope_lookup_current(p->scope, name);
	return lb_find_value_from_entity(m, e);
}
lbValue lb_find_package_value(lbModule *m, String const &pkg, String const &name) {
	Entity *e = find_entity_in_pkg(m->info, pkg, name);
	return lb_find_value_from_entity(m, e);
}

lbValue lb_get_type_info_ptr(lbModule *m, Type *type) {
	i32 index = cast(i32)lb_type_info_index(m->info, type);
	GB_ASSERT(index >= 0);
	// gb_printf_err("%d %s\n", index, type_to_string(type));

	LLVMValueRef indices[2] = {
		LLVMConstInt(lb_type(m, t_int), 0, false),
		LLVMConstInt(lb_type(m, t_int), index, false),
	};

	lbValue res = {};
	res.type = t_type_info_ptr;
	res.value = LLVMConstGEP(lb_global_type_info_data_ptr(m).value, indices, cast(unsigned)gb_count_of(indices));
	return res;
}


lbValue lb_type_info_member_types_offset(lbProcedure *p, isize count) {
	GB_ASSERT(p->module == &p->module->gen->default_module);
	lbValue offset = lb_emit_array_epi(p, lb_global_type_info_member_types.addr, lb_global_type_info_member_types_index);
	lb_global_type_info_member_types_index += cast(i32)count;
	return offset;
}
lbValue lb_type_info_member_names_offset(lbProcedure *p, isize count) {
	GB_ASSERT(p->module == &p->module->gen->default_module);
	lbValue offset = lb_emit_array_epi(p, lb_global_type_info_member_names.addr, lb_global_type_info_member_names_index);
	lb_global_type_info_member_names_index += cast(i32)count;
	return offset;
}
lbValue lb_type_info_member_offsets_offset(lbProcedure *p, isize count) {
	GB_ASSERT(p->module == &p->module->gen->default_module);
	lbValue offset = lb_emit_array_epi(p, lb_global_type_info_member_offsets.addr, lb_global_type_info_member_offsets_index);
	lb_global_type_info_member_offsets_index += cast(i32)count;
	return offset;
}
lbValue lb_type_info_member_usings_offset(lbProcedure *p, isize count) {
	GB_ASSERT(p->module == &p->module->gen->default_module);
	lbValue offset = lb_emit_array_epi(p, lb_global_type_info_member_usings.addr, lb_global_type_info_member_usings_index);
	lb_global_type_info_member_usings_index += cast(i32)count;
	return offset;
}
lbValue lb_type_info_member_tags_offset(lbProcedure *p, isize count) {
	GB_ASSERT(p->module == &p->module->gen->default_module);
	lbValue offset = lb_emit_array_epi(p, lb_global_type_info_member_tags.addr, lb_global_type_info_member_tags_index);
	lb_global_type_info_member_tags_index += cast(i32)count;
	return offset;
}

lbValue lb_generate_local_array(lbProcedure *p, Type *elem_type, i64 count, bool zero_init) {
	lbAddr addr = lb_add_local_generated(p, alloc_type_array(elem_type, count), zero_init);
	return lb_addr_get_ptr(p, addr);
}

lbValue lb_generate_global_array(lbModule *m, Type *elem_type, i64 count, String prefix, i64 id) {
	Token token = {Token_Ident};
	isize name_len = prefix.len + 1 + 20;

	auto suffix_id = cast(unsigned long long)id;
	char *text = gb_alloc_array(permanent_allocator(), char, name_len+1);
	gb_snprintf(text, name_len,
	            "%.*s-%llu", LIT(prefix), suffix_id);
	text[name_len] = 0;

	String s = make_string_c(text);

	Type *t = alloc_type_array(elem_type, count);
	lbValue g = {};
	g.value = LLVMAddGlobal(m->mod, lb_type(m, t), text);
	g.type = alloc_type_pointer(t);
	LLVMSetInitializer(g.value, LLVMConstNull(lb_type(m, t)));
	LLVMSetLinkage(g.value, LLVMInternalLinkage);
	string_map_set(&m->members, s, g);
	return g;
}


void lb_setup_type_info_data(lbProcedure *p) { // NOTE(bill): Setup type_info data
	lbModule *m = p->module;
	LLVMContextRef ctx = m->ctx;
	CheckerInfo *info = m->info;

	{
		// NOTE(bill): Set the type_table slice with the global backing array
		lbValue global_type_table = lb_find_runtime_value(m, str_lit("type_table"));
		Type *type = base_type(lb_global_type_info_data_entity->type);
		GB_ASSERT(is_type_array(type));

		LLVMValueRef indices[2] = {llvm_zero(m), llvm_zero(m)};
		LLVMValueRef values[2] = {
			LLVMConstInBoundsGEP(lb_global_type_info_data_ptr(m).value, indices, gb_count_of(indices)),
			LLVMConstInt(lb_type(m, t_int), type->Array.count, true),
		};
		LLVMValueRef slice = llvm_const_named_struct(llvm_addr_type(global_type_table), values, gb_count_of(values));

		LLVMSetInitializer(global_type_table.value, slice);
	}


	// Useful types
	Type *t_i64_slice_ptr    = alloc_type_pointer(alloc_type_slice(t_i64));
	Type *t_string_slice_ptr = alloc_type_pointer(alloc_type_slice(t_string));
	Entity *type_info_flags_entity = find_core_entity(info->checker, str_lit("Type_Info_Flags"));
	Type *t_type_info_flags = type_info_flags_entity->type;


	i32 type_info_member_types_index = 0;
	i32 type_info_member_names_index = 0;
	i32 type_info_member_offsets_index = 0;

	for_array(type_info_type_index, info->type_info_types) {
		Type *t = info->type_info_types[type_info_type_index];
		if (t == nullptr || t == t_invalid) {
			continue;
		}

		isize entry_index = lb_type_info_index(info, t, false);
		if (entry_index <= 0) {
			continue;
		}

		lbValue tag = {};
		lbValue ti_ptr = lb_emit_array_epi(p, lb_global_type_info_data_ptr(m), cast(i32)entry_index);
		lbValue variant_ptr = lb_emit_struct_ep(p, ti_ptr, 4);

		lbValue type_info_flags = lb_const_int(p->module, t_type_info_flags, type_info_flags_of_type(t));

		lb_emit_store(p, lb_emit_struct_ep(p, ti_ptr, 0), lb_const_int(m, t_int, type_size_of(t)));
		lb_emit_store(p, lb_emit_struct_ep(p, ti_ptr, 1), lb_const_int(m, t_int, type_align_of(t)));
		lb_emit_store(p, lb_emit_struct_ep(p, ti_ptr, 2), type_info_flags);
		lb_emit_store(p, lb_emit_struct_ep(p, ti_ptr, 3), lb_typeid(m, t));


		switch (t->kind) {
		case Type_Named: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_named_ptr);

			LLVMValueRef pkg_name = nullptr;
			if (t->Named.type_name->pkg) {
				pkg_name = lb_const_string(m, t->Named.type_name->pkg->name).value;
			} else {
				pkg_name = LLVMConstNull(lb_type(m, t_string));
			}

			String proc_name = {};
			if (t->Named.type_name->parent_proc_decl) {
				DeclInfo *decl = t->Named.type_name->parent_proc_decl;
				if (decl->entity && decl->entity->kind == Entity_Procedure) {
					proc_name = decl->entity->token.string;
				}
			}
			TokenPos pos = t->Named.type_name->token.pos;

			lbValue loc = lb_emit_source_code_location(p, proc_name, pos);

			LLVMValueRef vals[4] = {
				lb_const_string(p->module, t->Named.type_name->token.string).value,
				lb_get_type_info_ptr(m, t->Named.base).value,
				pkg_name,
				loc.value
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);
			break;
		}

		case Type_Basic:
			switch (t->Basic.kind) {
			case Basic_bool:
			case Basic_b8:
			case Basic_b16:
			case Basic_b32:
			case Basic_b64:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_boolean_ptr);
				break;

			case Basic_i8:
			case Basic_u8:
			case Basic_i16:
			case Basic_u16:
			case Basic_i32:
			case Basic_u32:
			case Basic_i64:
			case Basic_u64:
			case Basic_i128:
			case Basic_u128:

			case Basic_i16le:
			case Basic_u16le:
			case Basic_i32le:
			case Basic_u32le:
			case Basic_i64le:
			case Basic_u64le:
			case Basic_i128le:
			case Basic_u128le:
			case Basic_i16be:
			case Basic_u16be:
			case Basic_i32be:
			case Basic_u32be:
			case Basic_i64be:
			case Basic_u64be:
			case Basic_i128be:
			case Basic_u128be:

			case Basic_int:
			case Basic_uint:
			case Basic_uintptr: {
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_integer_ptr);

				lbValue is_signed = lb_const_bool(m, t_bool, (t->Basic.flags & BasicFlag_Unsigned) == 0);
				// NOTE(bill): This is matches the runtime layout
				u8 endianness_value = 0;
				if (t->Basic.flags & BasicFlag_EndianLittle) {
					endianness_value = 1;
				} else if (t->Basic.flags & BasicFlag_EndianBig) {
					endianness_value = 2;
				}
				lbValue endianness = lb_const_int(m, t_u8, endianness_value);

				LLVMValueRef vals[2] = {
					is_signed.value,
					endianness.value,
				};

				lbValue res = {};
				res.type = type_deref(tag.type);
				res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
				lb_emit_store(p, tag, res);
				break;
			}

			case Basic_rune:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_rune_ptr);
				break;

			case Basic_f16:
			case Basic_f32:
			case Basic_f64:
			case Basic_f16le:
			case Basic_f32le:
			case Basic_f64le:
			case Basic_f16be:
			case Basic_f32be:
			case Basic_f64be:
				{
					tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_float_ptr);

					// NOTE(bill): This is matches the runtime layout
					u8 endianness_value = 0;
					if (t->Basic.flags & BasicFlag_EndianLittle) {
						endianness_value = 1;
					} else if (t->Basic.flags & BasicFlag_EndianBig) {
						endianness_value = 2;
					}
					lbValue endianness = lb_const_int(m, t_u8, endianness_value);

					LLVMValueRef vals[1] = {
						endianness.value,
					};

					lbValue res = {};
					res.type = type_deref(tag.type);
					res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
					lb_emit_store(p, tag, res);
				}
				break;

			case Basic_complex32:
			case Basic_complex64:
			case Basic_complex128:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_complex_ptr);
				break;

			case Basic_quaternion64:
			case Basic_quaternion128:
			case Basic_quaternion256:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_quaternion_ptr);
				break;

			case Basic_rawptr:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_pointer_ptr);
				break;

			case Basic_string:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_string_ptr);
				break;

			case Basic_cstring:
				{
					tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_string_ptr);
					LLVMValueRef vals[1] = {
						lb_const_bool(m, t_bool, true).value,
					};

					lbValue res = {};
					res.type = type_deref(tag.type);
					res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
					lb_emit_store(p, tag, res);
				}
				break;

			case Basic_any:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_any_ptr);
				break;

			case Basic_typeid:
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_typeid_ptr);
				break;
			}
			break;

		case Type_Pointer: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_pointer_ptr);
			lbValue gep = lb_get_type_info_ptr(m, t->Pointer.elem);

			LLVMValueRef vals[1] = {
				gep.value,
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);
			break;
		}
		case Type_Array: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_array_ptr);
			i64 ez = type_size_of(t->Array.elem);

			LLVMValueRef vals[3] = {
				lb_get_type_info_ptr(m, t->Array.elem).value,
				lb_const_int(m, t_int, ez).value,
				lb_const_int(m, t_int, t->Array.count).value,
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);
			break;
		}
		case Type_EnumeratedArray: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_enumerated_array_ptr);

			LLVMValueRef vals[6] = {
				lb_get_type_info_ptr(m, t->EnumeratedArray.elem).value,
				lb_get_type_info_ptr(m, t->EnumeratedArray.index).value,
				lb_const_int(m, t_int, type_size_of(t->EnumeratedArray.elem)).value,
				lb_const_int(m, t_int, t->EnumeratedArray.count).value,

				// Unions
				LLVMConstNull(lb_type(m, t_type_info_enum_value)),
				LLVMConstNull(lb_type(m, t_type_info_enum_value)),
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);

			// NOTE(bill): Union assignment
			lbValue min_value = lb_emit_struct_ep(p, tag, 4);
			lbValue max_value = lb_emit_struct_ep(p, tag, 5);

			lbValue min_v = lb_const_value(m, t_i64, t->EnumeratedArray.min_value);
			lbValue max_v = lb_const_value(m, t_i64, t->EnumeratedArray.max_value);

			lb_emit_store(p, min_value, min_v);
			lb_emit_store(p, max_value, max_v);
			break;
		}
		case Type_DynamicArray: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_dynamic_array_ptr);

			LLVMValueRef vals[2] = {
				lb_get_type_info_ptr(m, t->DynamicArray.elem).value,
				lb_const_int(m, t_int, type_size_of(t->DynamicArray.elem)).value,
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);
			break;
		}
		case Type_Slice: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_slice_ptr);

			LLVMValueRef vals[2] = {
				lb_get_type_info_ptr(m, t->Slice.elem).value,
				lb_const_int(m, t_int, type_size_of(t->Slice.elem)).value,
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);
			break;
		}
		case Type_Proc: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_procedure_ptr);

			LLVMValueRef params = LLVMConstNull(lb_type(m, t_type_info_ptr));
			LLVMValueRef results = LLVMConstNull(lb_type(m, t_type_info_ptr));
			if (t->Proc.params != nullptr) {
				params = lb_get_type_info_ptr(m, t->Proc.params).value;
			}
			if (t->Proc.results != nullptr) {
				results = lb_get_type_info_ptr(m, t->Proc.results).value;
			}

			LLVMValueRef vals[4] = {
				params,
				results,
				lb_const_bool(m, t_bool, t->Proc.variadic).value,
				lb_const_int(m, t_u8, t->Proc.calling_convention).value,
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);
			break;
		}
		case Type_Tuple: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_tuple_ptr);


			lbValue memory_types = lb_type_info_member_types_offset(p, t->Tuple.variables.count);
			lbValue memory_names = lb_type_info_member_names_offset(p, t->Tuple.variables.count);


			for_array(i, t->Tuple.variables) {
				// NOTE(bill): offset is not used for tuples
				Entity *f = t->Tuple.variables[i];

				lbValue index     = lb_const_int(m, t_int, i);
				lbValue type_info = lb_emit_ptr_offset(p, memory_types, index);

				// TODO(bill): Make this constant if possible, 'lb_const_store' does not work
				lb_emit_store(p, type_info, lb_type_info(m, f->type));
				if (f->token.string.len > 0) {
					lbValue name = lb_emit_ptr_offset(p, memory_names, index);
					lb_emit_store(p, name, lb_const_string(m, f->token.string));
				}
			}

			lbValue count = lb_const_int(m, t_int, t->Tuple.variables.count);

			LLVMValueRef types_slice = llvm_const_slice(m, memory_types, count);
			LLVMValueRef names_slice = llvm_const_slice(m, memory_names, count);

			LLVMValueRef vals[2] = {
				types_slice,
				names_slice,
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);

			break;
		}

		case Type_Enum:
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_enum_ptr);

			{
				GB_ASSERT(t->Enum.base_type != nullptr);
				// GB_ASSERT_MSG(type_size_of(t_type_info_enum_value) == 16, "%lld == 16", cast(long long)type_size_of(t_type_info_enum_value));


				LLVMValueRef vals[3] = {};
				vals[0] = lb_type_info(m, t->Enum.base_type).value;
				if (t->Enum.fields.count > 0) {
					auto fields = t->Enum.fields;
					lbValue name_array  = lb_generate_global_array(m, t_string, fields.count,
					                                        str_lit("$enum_names"), cast(i64)entry_index);
					lbValue value_array = lb_generate_global_array(m, t_type_info_enum_value, fields.count,
					                                        str_lit("$enum_values"), cast(i64)entry_index);


					LLVMValueRef *name_values = gb_alloc_array(temporary_allocator(), LLVMValueRef, fields.count);
					LLVMValueRef *value_values = gb_alloc_array(temporary_allocator(), LLVMValueRef, fields.count);

					GB_ASSERT(is_type_integer(t->Enum.base_type));

					LLVMTypeRef align_type = lb_alignment_prefix_type_hack(m, type_align_of(t));
					LLVMTypeRef array_type = LLVMArrayType(lb_type(m, t_u8), 8);

					for_array(i, fields) {
						name_values[i] = lb_const_string(m, fields[i]->token.string).value;
						value_values[i] = lb_const_value(m, t_i64, fields[i]->Constant.value).value;
					}

					LLVMValueRef name_init  = llvm_const_array(lb_type(m, t_string),               name_values,  cast(unsigned)fields.count);
					LLVMValueRef value_init = llvm_const_array(lb_type(m, t_type_info_enum_value), value_values, cast(unsigned)fields.count);
					LLVMSetInitializer(name_array.value,  name_init);
					LLVMSetInitializer(value_array.value, value_init);

					lbValue v_count = lb_const_int(m, t_int, fields.count);

					vals[1] = llvm_const_slice(m, lb_array_elem(p, name_array), v_count);
					vals[2] = llvm_const_slice(m, lb_array_elem(p, value_array), v_count);
				} else {
					vals[1] = LLVMConstNull(lb_type(m, base_type(t_type_info_enum)->Struct.fields[1]->type));
					vals[2] = LLVMConstNull(lb_type(m, base_type(t_type_info_enum)->Struct.fields[2]->type));
				}


				lbValue res = {};
				res.type = type_deref(tag.type);
				res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
				lb_emit_store(p, tag, res);
			}
			break;

		case Type_Union: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_union_ptr);

			{
				LLVMValueRef vals[7] = {};

				isize variant_count = gb_max(0, t->Union.variants.count);
				lbValue memory_types = lb_type_info_member_types_offset(p, variant_count);

				// NOTE(bill): Zeroth is nil so ignore it
				for (isize variant_index = 0; variant_index < variant_count; variant_index++) {
					Type *vt = t->Union.variants[variant_index];
					lbValue tip = lb_get_type_info_ptr(m, vt);

					lbValue index     = lb_const_int(m, t_int, variant_index);
					lbValue type_info = lb_emit_ptr_offset(p, memory_types, index);
					lb_emit_store(p, type_info, lb_type_info(m, vt));
				}

				lbValue count = lb_const_int(m, t_int, variant_count);
				vals[0] = llvm_const_slice(m, memory_types, count);

				i64 tag_size   = union_tag_size(t);
				i64 tag_offset = align_formula(t->Union.variant_block_size, tag_size);

				if (tag_size > 0) {
					vals[1] = lb_const_int(m, t_uintptr, tag_offset).value;
					vals[2] = lb_type_info(m, union_tag_type(t)).value;
				} else {
					vals[1] = lb_const_int(m, t_uintptr, 0).value;
					vals[2] = LLVMConstNull(lb_type(m, t_type_info_ptr));
				}

				if (is_type_comparable(t) && !is_type_simple_compare(t)) {
					vals[3] = lb_get_equal_proc_for_type(m, t).value;
				}

				vals[4] = lb_const_bool(m, t_bool, t->Union.custom_align != 0).value;
				vals[5] = lb_const_bool(m, t_bool, t->Union.no_nil).value;
				vals[6] = lb_const_bool(m, t_bool, t->Union.maybe).value;

				for (isize i = 0; i < gb_count_of(vals); i++) {
					if (vals[i] == nullptr) {
						vals[i]  = LLVMConstNull(lb_type(m, get_struct_field_type(tag.type, i)));
					}
				}

				lbValue res = {};
				res.type = type_deref(tag.type);
				res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
				lb_emit_store(p, tag, res);
			}

			break;
		}

		case Type_Struct: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_struct_ptr);

			LLVMValueRef vals[12] = {};


			{
				lbValue is_packed       = lb_const_bool(m, t_bool, t->Struct.is_packed);
				lbValue is_raw_union    = lb_const_bool(m, t_bool, t->Struct.is_raw_union);
				lbValue is_custom_align = lb_const_bool(m, t_bool, t->Struct.custom_align != 0);
				vals[5] = is_packed.value;
				vals[6] = is_raw_union.value;
				vals[7] = is_custom_align.value;
				if (is_type_comparable(t) && !is_type_simple_compare(t)) {
					vals[8] = lb_get_equal_proc_for_type(m, t).value;
				}


				if (t->Struct.soa_kind != StructSoa_None) {
					lbValue kind = lb_emit_struct_ep(p, tag, 9);
					Type *kind_type = type_deref(kind.type);

					lbValue soa_kind = lb_const_value(m, kind_type, exact_value_i64(t->Struct.soa_kind));
					lbValue soa_type = lb_type_info(m, t->Struct.soa_elem);
					lbValue soa_len = lb_const_int(m, t_int, t->Struct.soa_count);

					vals[9]  = soa_kind.value;
					vals[10] = soa_type.value;
					vals[11] = soa_len.value;
				}
			}

			isize count = t->Struct.fields.count;
			if (count > 0) {
				lbValue memory_types   = lb_type_info_member_types_offset  (p, count);
				lbValue memory_names   = lb_type_info_member_names_offset  (p, count);
				lbValue memory_offsets = lb_type_info_member_offsets_offset(p, count);
				lbValue memory_usings  = lb_type_info_member_usings_offset (p, count);
				lbValue memory_tags    = lb_type_info_member_tags_offset   (p, count);

				type_set_offsets(t); // NOTE(bill): Just incase the offsets have not been set yet
				for (isize source_index = 0; source_index < count; source_index++) {
					// TODO(bill): Order fields in source order not layout order
					Entity *f = t->Struct.fields[source_index];
					lbValue tip = lb_get_type_info_ptr(m, f->type);
					i64 foffset = 0;
					if (!t->Struct.is_raw_union) {
						foffset = t->Struct.offsets[f->Variable.field_index];
					}
					GB_ASSERT(f->kind == Entity_Variable && f->flags & EntityFlag_Field);

					lbValue index     = lb_const_int(m, t_int, source_index);
					lbValue type_info = lb_emit_ptr_offset(p, memory_types,   index);
					lbValue offset    = lb_emit_ptr_offset(p, memory_offsets, index);
					lbValue is_using  = lb_emit_ptr_offset(p, memory_usings,  index);

					lb_emit_store(p, type_info, lb_type_info(m, f->type));
					if (f->token.string.len > 0) {
						lbValue name = lb_emit_ptr_offset(p, memory_names,   index);
						lb_emit_store(p, name, lb_const_string(m, f->token.string));
					}
					lb_emit_store(p, offset, lb_const_int(m, t_uintptr, foffset));
					lb_emit_store(p, is_using, lb_const_bool(m, t_bool, (f->flags&EntityFlag_Using) != 0));

					if (t->Struct.tags.count > 0) {
						String tag_string = t->Struct.tags[source_index];
						if (tag_string.len > 0) {
							lbValue tag_ptr = lb_emit_ptr_offset(p, memory_tags, index);
							lb_emit_store(p, tag_ptr, lb_const_string(m, tag_string));
						}
					}

				}

				lbValue cv = lb_const_int(m, t_int, count);
				vals[0] = llvm_const_slice(m, memory_types,   cv);
				vals[1] = llvm_const_slice(m, memory_names,   cv);
				vals[2] = llvm_const_slice(m, memory_offsets, cv);
				vals[3] = llvm_const_slice(m, memory_usings,  cv);
				vals[4] = llvm_const_slice(m, memory_tags,    cv);
			}
			for (isize i = 0; i < gb_count_of(vals); i++) {
				if (vals[i] == nullptr) {
					vals[i]  = LLVMConstNull(lb_type(m, get_struct_field_type(tag.type, i)));
				}
			}


			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);

			break;
		}

		case Type_Map: {
			tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_map_ptr);
			init_map_internal_types(t);

			LLVMValueRef vals[5] = {
				lb_get_type_info_ptr(m, t->Map.key).value,
				lb_get_type_info_ptr(m, t->Map.value).value,
				lb_get_type_info_ptr(m, t->Map.generated_struct_type).value,
				lb_get_equal_proc_for_type(m, t->Map.key).value,
				lb_get_hasher_proc_for_type(m, t->Map.key).value
			};

			lbValue res = {};
			res.type = type_deref(tag.type);
			res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
			lb_emit_store(p, tag, res);
			break;
		}

		case Type_BitSet:
			{
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_bit_set_ptr);

				GB_ASSERT(is_type_typed(t->BitSet.elem));


				LLVMValueRef vals[4] = {
					lb_get_type_info_ptr(m, t->BitSet.elem).value,
					LLVMConstNull(lb_type(m, t_type_info_ptr)),
					lb_const_int(m, t_i64, t->BitSet.lower).value,
					lb_const_int(m, t_i64, t->BitSet.upper).value,
				};
				if (t->BitSet.underlying != nullptr) {
					vals[1] =lb_get_type_info_ptr(m, t->BitSet.underlying).value;
				}

				lbValue res = {};
				res.type = type_deref(tag.type);
				res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
				lb_emit_store(p, tag, res);
			}
			break;

		case Type_SimdVector:
			{
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_simd_vector_ptr);

				LLVMValueRef vals[3] = {};

				vals[0] = lb_get_type_info_ptr(m, t->SimdVector.elem).value;
				vals[1] = lb_const_int(m, t_int, type_size_of(t->SimdVector.elem)).value;
				vals[2] = lb_const_int(m, t_int, t->SimdVector.count).value;

				lbValue res = {};
				res.type = type_deref(tag.type);
				res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
				lb_emit_store(p, tag, res);
			}
			break;

		case Type_RelativePointer:
			{
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_relative_pointer_ptr);
				LLVMValueRef vals[2] = {
					lb_get_type_info_ptr(m, t->RelativePointer.pointer_type).value,
					lb_get_type_info_ptr(m, t->RelativePointer.base_integer).value,
				};

				lbValue res = {};
				res.type = type_deref(tag.type);
				res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
				lb_emit_store(p, tag, res);
			}
			break;
		case Type_RelativeSlice:
			{
				tag = lb_const_ptr_cast(m, variant_ptr, t_type_info_relative_slice_ptr);
				LLVMValueRef vals[2] = {
					lb_get_type_info_ptr(m, t->RelativeSlice.slice_type).value,
					lb_get_type_info_ptr(m, t->RelativeSlice.base_integer).value,
				};

				lbValue res = {};
				res.type = type_deref(tag.type);
				res.value = llvm_const_named_struct(lb_type(m, res.type), vals, gb_count_of(vals));
				lb_emit_store(p, tag, res);
			}
			break;

		}


		if (tag.value != nullptr) {
			Type *tag_type = type_deref(tag.type);
			GB_ASSERT(is_type_named(tag_type));
			// lb_emit_store_union_variant(p, variant_ptr, lb_emit_load(p, tag), tag_type);
			lb_emit_store_union_variant_tag(p, variant_ptr, tag_type);
		} else {
			if (t != t_llvm_bool) {
				GB_PANIC("Unhandled Type_Info variant: %s", type_to_string(t));
			}
		}
	}
}

struct lbGlobalVariable {
	lbValue var;
	lbValue init;
	DeclInfo *decl;
	bool is_initialized;
};

lbProcedure *lb_create_startup_type_info(lbModule *m) {
	LLVMPassManagerRef default_function_pass_manager = LLVMCreateFunctionPassManagerForModule(m->mod);
	lb_populate_function_pass_manager(m, default_function_pass_manager, false, build_context.optimization_level);
	LLVMFinalizeFunctionPassManager(default_function_pass_manager);

	Type *params  = alloc_type_tuple();
	Type *results = alloc_type_tuple();

	Type *proc_type = alloc_type_proc(nullptr, nullptr, 0, nullptr, 0, false, ProcCC_CDecl);

	lbProcedure *p = lb_create_dummy_procedure(m, str_lit(LB_STARTUP_TYPE_INFO_PROC_NAME), proc_type);
	p->is_startup = true;
	LLVMSetLinkage(p->value, LLVMInternalLinkage);

	lb_begin_procedure_body(p);

	lb_setup_type_info_data(p);

	lb_end_procedure_body(p);

	if (!m->debug_builder && LLVMVerifyFunction(p->value, LLVMReturnStatusAction)) {
		gb_printf_err("LLVM CODE GEN FAILED FOR PROCEDURE: %s\n", "main");
		LLVMDumpValue(p->value);
		gb_printf_err("\n\n\n\n");
		LLVMVerifyFunction(p->value, LLVMAbortProcessAction);
	}

	lb_run_function_pass_manager(default_function_pass_manager, p);

	return p;
}

lbProcedure *lb_create_startup_runtime(lbModule *main_module, lbProcedure *startup_type_info, Array<lbGlobalVariable> &global_variables) { // Startup Runtime
	LLVMPassManagerRef default_function_pass_manager = LLVMCreateFunctionPassManagerForModule(main_module->mod);
	lb_populate_function_pass_manager(main_module, default_function_pass_manager, false, build_context.optimization_level);
	LLVMFinalizeFunctionPassManager(default_function_pass_manager);

	Type *params  = alloc_type_tuple();
	Type *results = alloc_type_tuple();

	Type *proc_type = alloc_type_proc(nullptr, nullptr, 0, nullptr, 0, false, ProcCC_CDecl);

	lbProcedure *p = lb_create_dummy_procedure(main_module, str_lit(LB_STARTUP_RUNTIME_PROC_NAME), proc_type);
	p->is_startup = true;

	lb_begin_procedure_body(p);

	LLVMBuildCall2(p->builder, LLVMGetElementType(lb_type(main_module, startup_type_info->type)), startup_type_info->value, nullptr, 0, "");

	for_array(i, global_variables) {
		auto *var = &global_variables[i];
		if (var->is_initialized) {
			continue;
		}

		lbModule *entity_module = main_module;

		Entity *e = var->decl->entity;
		GB_ASSERT(e->kind == Entity_Variable);
		e->code_gen_module = entity_module;

		Ast *init_expr = var->decl->init_expr;
		if (init_expr != nullptr)  {
			lbValue init = lb_build_expr(p, init_expr);
			if (init.value == nullptr) {
				LLVMTypeRef global_type = LLVMGetElementType(LLVMTypeOf(var->var.value));
				if (is_type_untyped_undef(init.type)) {
					LLVMSetInitializer(var->var.value, LLVMGetUndef(global_type));
					var->is_initialized = true;
					continue;
				} else if (is_type_untyped_nil(init.type)) {
					LLVMSetInitializer(var->var.value, LLVMConstNull(global_type));
					var->is_initialized = true;
					continue;
				}
				GB_PANIC("Invalid init value, got %s", expr_to_string(init_expr));
			}

			LLVMValueKind value_kind = LLVMGetValueKind(init.value);
			// gb_printf_err("%s %d\n", LLVMPrintValueToString(init.value));

			if (is_type_any(e->type) || is_type_union(e->type)) {
				var->init = init;
			} else if (lb_is_const_or_global(init)) {
				if (!var->is_initialized) {
					LLVMSetInitializer(var->var.value, init.value);
					var->is_initialized = true;
					continue;
				}
			} else {
				var->init = init;
			}
		}

		if (var->init.value != nullptr) {
			GB_ASSERT(!var->is_initialized);
			Type *t = type_deref(var->var.type);

			if (is_type_any(t)) {
				// NOTE(bill): Edge case for 'any' type
				Type *var_type = default_type(var->init.type);
				lbAddr g = lb_add_global_generated(main_module, var_type, var->init);
				lb_addr_store(p, g, var->init);
				lbValue gp = lb_addr_get_ptr(p, g);

				lbValue data = lb_emit_struct_ep(p, var->var, 0);
				lbValue ti   = lb_emit_struct_ep(p, var->var, 1);
				lb_emit_store(p, data, lb_emit_conv(p, gp, t_rawptr));
				lb_emit_store(p, ti,   lb_type_info(main_module, var_type));
			} else {
				LLVMTypeRef pvt = LLVMTypeOf(var->var.value);
				LLVMTypeRef vt = LLVMGetElementType(pvt);
				lbValue src0 = lb_emit_conv(p, var->init, t);
				LLVMValueRef src = OdinLLVMBuildTransmute(p, src0.value, vt);
				LLVMValueRef dst = var->var.value;
				LLVMBuildStore(p->builder, src, dst);
			}

			var->is_initialized = true;
		}
	}


	lb_end_procedure_body(p);

	if (!main_module->debug_builder && LLVMVerifyFunction(p->value, LLVMReturnStatusAction)) {
		gb_printf_err("LLVM CODE GEN FAILED FOR PROCEDURE: %s\n", "main");
		LLVMDumpValue(p->value);
		gb_printf_err("\n\n\n\n");
		LLVMVerifyFunction(p->value, LLVMAbortProcessAction);
	}

	lb_run_function_pass_manager(default_function_pass_manager, p);

	return p;
}


lbProcedure *lb_create_main_procedure(lbModule *m, lbProcedure *startup_runtime) {
	LLVMPassManagerRef default_function_pass_manager = LLVMCreateFunctionPassManagerForModule(m->mod);
	lb_populate_function_pass_manager(m, default_function_pass_manager, false, build_context.optimization_level);
	LLVMFinalizeFunctionPassManager(default_function_pass_manager);

	Type *params  = alloc_type_tuple();
	Type *results = alloc_type_tuple();

	Type *t_ptr_cstring = alloc_type_pointer(t_cstring);

	String name = str_lit("main");
	if (build_context.metrics.os == TargetOs_windows && build_context.metrics.arch == TargetArch_386) {
		name = str_lit("mainCRTStartup");
	} else {
		array_init(&params->Tuple.variables, permanent_allocator(), 2);
		params->Tuple.variables[0] = alloc_entity_param(nullptr, make_token_ident("argc"), t_i32, false, true);
		params->Tuple.variables[1] = alloc_entity_param(nullptr, make_token_ident("argv"), t_ptr_cstring, false, true);
	}

	array_init(&results->Tuple.variables, permanent_allocator(), 1);
	results->Tuple.variables[0] = alloc_entity_param(nullptr, blank_token, t_i32, false, true);

	Type *proc_type = alloc_type_proc(nullptr,
		params, params->Tuple.variables.count,
		results, results->Tuple.variables.count, false, ProcCC_CDecl);


	lbProcedure *p = lb_create_dummy_procedure(m, name, proc_type);
	p->is_startup = true;

	lb_begin_procedure_body(p);

	{ // initialize `runtime.args__`
		lbValue argc = {LLVMGetParam(p->value, 0), t_i32};
		lbValue argv = {LLVMGetParam(p->value, 1), t_ptr_cstring};
		LLVMSetValueName2(argc.value, "argc", 4);
		LLVMSetValueName2(argv.value, "argv", 4);
		argc = lb_emit_conv(p, argc, t_int);
		lbAddr args = lb_addr(lb_find_runtime_value(p->module, str_lit("args__")));
		lb_fill_slice(p, args, argv, argc);
	}

	LLVMBuildCall2(p->builder, LLVMGetElementType(lb_type(m, startup_runtime->type)), startup_runtime->value, nullptr, 0, "");

	if (build_context.command_kind == Command_test) {
		Type *t_Internal_Test = find_type_in_pkg(m->info, str_lit("testing"), str_lit("Internal_Test"));
		Type *array_type = alloc_type_array(t_Internal_Test, m->info->testing_procedures.count);
		Type *slice_type = alloc_type_slice(t_Internal_Test);
		lbAddr all_tests_array_addr = lb_add_global_generated(p->module, array_type, {});
		lbValue all_tests_array = lb_addr_get_ptr(p, all_tests_array_addr);

		LLVMTypeRef lbt_Internal_Test = lb_type(m, t_Internal_Test);

		LLVMValueRef indices[2] = {};
		indices[0] = LLVMConstInt(lb_type(m, t_i32), 0, false);

		for_array(i, m->info->testing_procedures) {
			Entity *testing_proc = m->info->testing_procedures[i];
			String name = testing_proc->token.string;

			String pkg_name = {};
			if (testing_proc->pkg != nullptr) {
				pkg_name = testing_proc->pkg->name;
			}
			lbValue v_pkg  = lb_find_or_add_entity_string(m, pkg_name);
			lbValue v_name = lb_find_or_add_entity_string(m, name);
			lbValue v_proc = lb_find_procedure_value_from_entity(m, testing_proc);

			indices[1] = LLVMConstInt(lb_type(m, t_int), i, false);

			LLVMValueRef vals[3] = {};
			vals[0] = v_pkg.value;
			vals[1] = v_name.value;
			vals[2] = v_proc.value;
			GB_ASSERT(LLVMIsConstant(vals[0]));
			GB_ASSERT(LLVMIsConstant(vals[1]));
			GB_ASSERT(LLVMIsConstant(vals[2]));

			LLVMValueRef dst = LLVMConstInBoundsGEP(all_tests_array.value, indices, gb_count_of(indices));
			LLVMValueRef src = llvm_const_named_struct(lbt_Internal_Test, vals, gb_count_of(vals));

			LLVMBuildStore(p->builder, src, dst);
		}

		lbAddr all_tests_slice = lb_add_local_generated(p, slice_type, true);
		lb_fill_slice(p, all_tests_slice,
		              lb_array_elem(p, all_tests_array),
		              lb_const_int(m, t_int, m->info->testing_procedures.count));


		lbValue runner = lb_find_package_value(m, str_lit("testing"), str_lit("runner"));

		auto args = array_make<lbValue>(heap_allocator(), 1);
		args[0] = lb_addr_load(p, all_tests_slice);
		lb_emit_call(p, runner, args);
	} else {
		if (m->info->entry_point != nullptr) {
			lbValue entry_point = lb_find_procedure_value_from_entity(m, m->info->entry_point);
			lb_emit_call(p, entry_point, {});
		}
	}

	LLVMBuildRet(p->builder, LLVMConstInt(lb_type(m, t_i32), 0, false));

	lb_end_procedure_body(p);

	if (!m->debug_builder && LLVMVerifyFunction(p->value, LLVMReturnStatusAction)) {
		gb_printf_err("LLVM CODE GEN FAILED FOR PROCEDURE: %s\n", "main");
		LLVMDumpValue(p->value);
		gb_printf_err("\n\n\n\n");
		LLVMVerifyFunction(p->value, LLVMAbortProcessAction);
	}

	lb_run_function_pass_manager(default_function_pass_manager, p);
	return p;
}

String lb_filepath_ll_for_module(lbModule *m) {
	String path = m->gen->output_base;
	if (m->pkg) {
		path = concatenate3_strings(permanent_allocator(), path, STR_LIT("-"), m->pkg->name);
	} else if (USE_SEPARTE_MODULES) {
		path = concatenate_strings(permanent_allocator(), path, STR_LIT("-builtin"));
	}
	path = concatenate_strings(permanent_allocator(), path, STR_LIT(".ll"));

	return path;
}
String lb_filepath_obj_for_module(lbModule *m) {
	String path = m->gen->output_base;
	if (m->pkg) {
		path = concatenate3_strings(permanent_allocator(), path, STR_LIT("-"), m->pkg->name);
	}

	String ext = {};

	if (build_context.build_mode == BuildMode_Assembly) {
		ext = STR_LIT(".S");
	} else {
		if (is_arch_wasm()) {
			ext = STR_LIT(".wasm.o");
		} else {
			switch (build_context.metrics.os) {
			case TargetOs_windows:
				ext = STR_LIT(".obj");
				break;
			default:
			case TargetOs_darwin:
			case TargetOs_linux:
			case TargetOs_essence:
				ext = STR_LIT(".o");
				break;
			}
		}
	}

	return concatenate_strings(permanent_allocator(), path, ext);
}


bool lb_is_module_empty(lbModule *m) {
	if (LLVMGetFirstFunction(m->mod) == nullptr &&
	    LLVMGetFirstGlobal(m->mod) == nullptr) {
		return true;
	}
	for (auto fn = LLVMGetFirstFunction(m->mod); fn != nullptr; fn = LLVMGetNextFunction(fn)) {
		if (LLVMGetFirstBasicBlock(fn) != nullptr) {
			return false;
		}
	}

	for (auto g = LLVMGetFirstGlobal(m->mod); g != nullptr; g = LLVMGetNextGlobal(g)) {
		if (LLVMGetLinkage(g) == LLVMExternalLinkage) {
			continue;
		}
		if (!LLVMIsExternallyInitialized(g)) {
			return false;
		}
	}
	return true;
}

struct lbLLVMEmitWorker {
	LLVMTargetMachineRef target_machine;
	LLVMCodeGenFileType code_gen_file_type;
	String filepath_obj;
	lbModule *m;
};

WORKER_TASK_PROC(lb_llvm_emit_worker_proc) {
	GB_ASSERT(MULTITHREAD_OBJECT_GENERATION);

	char *llvm_error = nullptr;

	auto wd = cast(lbLLVMEmitWorker *)data;

	if (LLVMTargetMachineEmitToFile(wd->target_machine, wd->m->mod, cast(char *)wd->filepath_obj.text, wd->code_gen_file_type, &llvm_error)) {
		gb_printf_err("LLVM Error: %s\n", llvm_error);
		gb_exit(1);
	}

	return 0;
}

WORKER_TASK_PROC(lb_llvm_function_pass_worker_proc) {
	GB_ASSERT(MULTITHREAD_OBJECT_GENERATION);

	auto m = cast(lbModule *)data;

	LLVMPassManagerRef default_function_pass_manager = LLVMCreateFunctionPassManagerForModule(m->mod);
	LLVMPassManagerRef function_pass_manager_minimal = LLVMCreateFunctionPassManagerForModule(m->mod);
	LLVMPassManagerRef function_pass_manager_size = LLVMCreateFunctionPassManagerForModule(m->mod);
	LLVMPassManagerRef function_pass_manager_speed = LLVMCreateFunctionPassManagerForModule(m->mod);

	LLVMInitializeFunctionPassManager(default_function_pass_manager);
	LLVMInitializeFunctionPassManager(function_pass_manager_minimal);
	LLVMInitializeFunctionPassManager(function_pass_manager_size);
	LLVMInitializeFunctionPassManager(function_pass_manager_speed);

	lb_populate_function_pass_manager(m, default_function_pass_manager, false, build_context.optimization_level);
	lb_populate_function_pass_manager_specific(m, function_pass_manager_minimal, 0);
	lb_populate_function_pass_manager_specific(m, function_pass_manager_size,    1);
	lb_populate_function_pass_manager_specific(m, function_pass_manager_speed,   2);

	LLVMFinalizeFunctionPassManager(default_function_pass_manager);
	LLVMFinalizeFunctionPassManager(function_pass_manager_minimal);
	LLVMFinalizeFunctionPassManager(function_pass_manager_size);
	LLVMFinalizeFunctionPassManager(function_pass_manager_speed);


	LLVMPassManagerRef default_function_pass_manager_without_memcpy = LLVMCreateFunctionPassManagerForModule(m->mod);
	LLVMInitializeFunctionPassManager(default_function_pass_manager_without_memcpy);
	lb_populate_function_pass_manager(m, default_function_pass_manager_without_memcpy, true, build_context.optimization_level);
	LLVMFinalizeFunctionPassManager(default_function_pass_manager_without_memcpy);


	for_array(i, m->procedures_to_generate) {
		lbProcedure *p = m->procedures_to_generate[i];
		if (p->body != nullptr) { // Build Procedure
			if (p->flags & lbProcedureFlag_WithoutMemcpyPass) {
				lb_run_function_pass_manager(default_function_pass_manager_without_memcpy, p);
			} else {
				if (p->entity && p->entity->kind == Entity_Procedure) {
					switch (p->entity->Procedure.optimization_mode) {
					case ProcedureOptimizationMode_None:
					case ProcedureOptimizationMode_Minimal:
						lb_run_function_pass_manager(function_pass_manager_minimal, p);
						break;
					case ProcedureOptimizationMode_Size:
						lb_run_function_pass_manager(function_pass_manager_size, p);
						break;
					case ProcedureOptimizationMode_Speed:
						lb_run_function_pass_manager(function_pass_manager_speed, p);
						break;
					default:
						lb_run_function_pass_manager(default_function_pass_manager, p);
						break;
					}
				} else {
					lb_run_function_pass_manager(default_function_pass_manager, p);
				}
			}
		}
	}

	for_array(i, m->equal_procs.entries) {
		lbProcedure *p = m->equal_procs.entries[i].value;
		lb_run_function_pass_manager(default_function_pass_manager, p);
	}
	for_array(i, m->hasher_procs.entries) {
		lbProcedure *p = m->hasher_procs.entries[i].value;
		lb_run_function_pass_manager(default_function_pass_manager, p);
	}

	return 0;
}


struct lbLLVMModulePassWorkerData {
	lbModule *m;
	LLVMTargetMachineRef target_machine;
};

WORKER_TASK_PROC(lb_llvm_module_pass_worker_proc) {
	GB_ASSERT(MULTITHREAD_OBJECT_GENERATION);

	auto wd = cast(lbLLVMModulePassWorkerData *)data;

	LLVMPassManagerRef module_pass_manager = LLVMCreatePassManager();
	lb_populate_module_pass_manager(wd->target_machine, module_pass_manager, build_context.optimization_level);
	LLVMRunPassManager(module_pass_manager, wd->m->mod);

	return 0;
}


void lb_generate_code(lbGenerator *gen) {
	#define TIME_SECTION(str) do { if (build_context.show_more_timings) timings_start_section(&global_timings, str_lit(str)); } while (0)
	#define TIME_SECTION_WITH_LEN(str, len) do { if (build_context.show_more_timings) timings_start_section(&global_timings, make_string((u8 *)str, len)); } while (0)

	TIME_SECTION("LLVM Initializtion");

	isize thread_count = gb_max(build_context.thread_count, 1);
	isize worker_count = thread_count-1;

	LLVMBool do_threading = (LLVMIsMultithreaded() && USE_SEPARTE_MODULES && MULTITHREAD_OBJECT_GENERATION && worker_count > 0);

	thread_pool_init(&lb_thread_pool, heap_allocator(), worker_count, "LLVMBackend");
	defer (thread_pool_destroy(&lb_thread_pool));

	lbModule *default_module = &gen->default_module;
	CheckerInfo *info = gen->info;

	auto *min_dep_set = &info->minimum_dependency_set;

	LLVMInitializeAllTargetInfos();
	LLVMInitializeAllTargets();
	LLVMInitializeAllTargetMCs();
	LLVMInitializeAllAsmPrinters();
	LLVMInitializeAllAsmParsers();
	LLVMInitializeAllDisassemblers();
	LLVMInitializeNativeTarget();

	char const *target_triple = alloc_cstring(permanent_allocator(), build_context.metrics.target_triplet);
	char const *target_data_layout = alloc_cstring(permanent_allocator(), build_context.metrics.target_data_layout);
	for_array(i, gen->modules.entries) {
		LLVMSetTarget(gen->modules.entries[i].value->mod, target_triple);
	}

	LLVMTargetRef target = {};
	char *llvm_error = nullptr;
	LLVMGetTargetFromTriple(target_triple, &target, &llvm_error);
	GB_ASSERT(target != nullptr);



	TIME_SECTION("LLVM Create Target Machine");

	LLVMCodeModel code_mode = LLVMCodeModelDefault;
	if (is_arch_wasm()) {
		code_mode = LLVMCodeModelJITDefault;
	}

	char const *host_cpu_name = LLVMGetHostCPUName();
	char const *llvm_cpu = "generic";
	char const *llvm_features = "";
	if (build_context.microarch.len != 0) {
		if (build_context.microarch == "native") {
			llvm_cpu = host_cpu_name;
		} else {
			llvm_cpu = alloc_cstring(permanent_allocator(), build_context.microarch);
		}
		if (gb_strcmp(llvm_cpu, host_cpu_name) == 0) {
			llvm_features = LLVMGetHostCPUFeatures();
		}
	}

	// GB_ASSERT_MSG(LLVMTargetHasAsmBackend(target));

	LLVMCodeGenOptLevel code_gen_level = LLVMCodeGenLevelNone;
	switch (build_context.optimization_level) {
	case 0: code_gen_level = LLVMCodeGenLevelNone;    break;
	case 1: code_gen_level = LLVMCodeGenLevelLess;    break;
	case 2: code_gen_level = LLVMCodeGenLevelDefault; break;
	case 3: code_gen_level = LLVMCodeGenLevelDefault; break; // NOTE(bill): force -opt:3 to be the same as -opt:2
	// case 3: code_gen_level = LLVMCodeGenLevelAggressive; break;
	}

	// NOTE(bill): Target Machine Creation
	// NOTE(bill, 2021-05-04): Target machines must be unique to each module because they are not thread safe
	auto target_machines = array_make<LLVMTargetMachineRef>(permanent_allocator(), gen->modules.entries.count);

	for_array(i, gen->modules.entries) {
		target_machines[i] = LLVMCreateTargetMachine(
			target, target_triple, llvm_cpu,
			llvm_features,
			code_gen_level,
			LLVMRelocDefault,
			code_mode);
		LLVMSetModuleDataLayout(gen->modules.entries[i].value->mod, LLVMCreateTargetDataLayout(target_machines[i]));
	}

	for_array(i, gen->modules.entries) {
		lbModule *m = gen->modules.entries[i].value;
		if (m->debug_builder) { // Debug Info
			for_array(i, info->files.entries) {
				AstFile *f = info->files.entries[i].value;
				String fullpath = f->fullpath;
				String filename = remove_directory_from_path(fullpath);
				String directory = directory_from_path(fullpath);
				LLVMMetadataRef res = LLVMDIBuilderCreateFile(m->debug_builder,
					cast(char const *)filename.text, filename.len,
					cast(char const *)directory.text, directory.len);
				lb_set_llvm_metadata(m, f, res);
			}

			gbString producer = gb_string_make(heap_allocator(), "odin");
			// producer = gb_string_append_fmt(producer, " version %.*s", LIT(ODIN_VERSION));
			// #ifdef NIGHTLY
			// producer = gb_string_appendc(producer, "-nightly");
			// #endif
			// #ifdef GIT_SHA
			// producer = gb_string_append_fmt(producer, "-%s", GIT_SHA);
			// #endif

			gbString split_name = gb_string_make(heap_allocator(), "");

			LLVMBool is_optimized = build_context.optimization_level > 0;
			AstFile *init_file = m->info->init_package->files[0];
			if (m->info->entry_point && m->info->entry_point->identifier && m->info->entry_point->identifier->file) {
				init_file = m->info->entry_point->identifier->file;
			}

			LLVMBool split_debug_inlining = false;
			LLVMBool debug_info_for_profiling = false;

			m->debug_compile_unit = LLVMDIBuilderCreateCompileUnit(m->debug_builder, LLVMDWARFSourceLanguageC99,
				lb_get_llvm_metadata(m, init_file),
				producer, gb_string_length(producer),
				is_optimized, "", 0,
				1, split_name, gb_string_length(split_name),
				LLVMDWARFEmissionFull,
				0, split_debug_inlining,
				debug_info_for_profiling,
				"", 0, // sys_root
				"", 0  // SDK
			);
			GB_ASSERT(m->debug_compile_unit != nullptr);
		}
	}

	TIME_SECTION("LLVM Global Variables");

	{
		lbModule *m = default_module;

		{ // Add type info data
			isize max_type_info_count = info->minimum_dependency_type_info_set.entries.count+1;
			// gb_printf_err("max_type_info_count: %td\n", max_type_info_count);
			Type *t = alloc_type_array(t_type_info, max_type_info_count);
			LLVMValueRef g = LLVMAddGlobal(m->mod, lb_type(m, t), LB_TYPE_INFO_DATA_NAME);
			LLVMSetInitializer(g, LLVMConstNull(lb_type(m, t)));
			if (!USE_SEPARTE_MODULES) {
				LLVMSetLinkage(g, LLVMInternalLinkage);
			}

			lbValue value = {};
			value.value = g;
			value.type = alloc_type_pointer(t);

			lb_global_type_info_data_entity = alloc_entity_variable(nullptr, make_token_ident(LB_TYPE_INFO_DATA_NAME), t, EntityState_Resolved);
			lb_add_entity(m, lb_global_type_info_data_entity, value);
		}
		{ // Type info member buffer
			// NOTE(bill): Removes need for heap allocation by making it global memory
			isize count = 0;

			for_array(entry_index, m->info->type_info_types) {
				Type *t = m->info->type_info_types[entry_index];

				isize index = lb_type_info_index(m->info, t, false);
				if (index < 0) {
					continue;
				}

				switch (t->kind) {
				case Type_Union:
					count += t->Union.variants.count;
					break;
				case Type_Struct:
					count += t->Struct.fields.count;
					break;
				case Type_Tuple:
					count += t->Tuple.variables.count;
					break;
				}
			}

			if (count > 0) {
				{
					char const *name = LB_TYPE_INFO_TYPES_NAME;
					Type *t = alloc_type_array(t_type_info_ptr, count);
					LLVMValueRef g = LLVMAddGlobal(m->mod, lb_type(m, t), name);
					LLVMSetInitializer(g, LLVMConstNull(lb_type(m, t)));
					LLVMSetLinkage(g, LLVMInternalLinkage);
					lb_global_type_info_member_types = lb_addr({g, alloc_type_pointer(t)});

				}
				{
					char const *name = LB_TYPE_INFO_NAMES_NAME;
					Type *t = alloc_type_array(t_string, count);
					LLVMValueRef g = LLVMAddGlobal(m->mod, lb_type(m, t), name);
					LLVMSetInitializer(g, LLVMConstNull(lb_type(m, t)));
					LLVMSetLinkage(g, LLVMInternalLinkage);
					lb_global_type_info_member_names = lb_addr({g, alloc_type_pointer(t)});
				}
				{
					char const *name = LB_TYPE_INFO_OFFSETS_NAME;
					Type *t = alloc_type_array(t_uintptr, count);
					LLVMValueRef g = LLVMAddGlobal(m->mod, lb_type(m, t), name);
					LLVMSetInitializer(g, LLVMConstNull(lb_type(m, t)));
					LLVMSetLinkage(g, LLVMInternalLinkage);
					lb_global_type_info_member_offsets = lb_addr({g, alloc_type_pointer(t)});
				}

				{
					char const *name = LB_TYPE_INFO_USINGS_NAME;
					Type *t = alloc_type_array(t_bool, count);
					LLVMValueRef g = LLVMAddGlobal(m->mod, lb_type(m, t), name);
					LLVMSetInitializer(g, LLVMConstNull(lb_type(m, t)));
					LLVMSetLinkage(g, LLVMInternalLinkage);
					lb_global_type_info_member_usings = lb_addr({g, alloc_type_pointer(t)});
				}

				{
					char const *name = LB_TYPE_INFO_TAGS_NAME;
					Type *t = alloc_type_array(t_string, count);
					LLVMValueRef g = LLVMAddGlobal(m->mod, lb_type(m, t), name);
					LLVMSetInitializer(g, LLVMConstNull(lb_type(m, t)));
					LLVMSetLinkage(g, LLVMInternalLinkage);
					lb_global_type_info_member_tags = lb_addr({g, alloc_type_pointer(t)});
				}
			}
		}
	}


	isize global_variable_max_count = 0;
	Entity *entry_point = info->entry_point;
	bool has_dll_main = false;
	bool has_win_main = false;

	for_array(i, info->entities) {
		Entity *e = info->entities[i];
		String name = e->token.string;

		bool is_global = e->pkg != nullptr;

		if (e->kind == Entity_Variable) {
			global_variable_max_count++;
		} else if (e->kind == Entity_Procedure && !is_global) {
			if ((e->scope->flags&ScopeFlag_Init) && name == "main") {
				GB_ASSERT(e == entry_point);
				// entry_point = e;
			}
			if (e->Procedure.is_export ||
			    (e->Procedure.link_name.len > 0) ||
			    ((e->scope->flags&ScopeFlag_File) && e->Procedure.link_name.len > 0)) {
				if (!has_dll_main && name == "DllMain") {
					has_dll_main = true;
				} else if (!has_win_main && name == "WinMain") {
					has_win_main = true;
				}
			}
		}
	}


	auto global_variables = array_make<lbGlobalVariable>(permanent_allocator(), 0, global_variable_max_count);

	for_array(i, info->variable_init_order) {
		DeclInfo *d = info->variable_init_order[i];

		Entity *e = d->entity;

		if ((e->scope->flags & ScopeFlag_File) == 0) {
			continue;
		}

		if (!ptr_set_exists(min_dep_set, e)) {
			continue;
		}
		DeclInfo *decl = decl_info_of_entity(e);
		if (decl == nullptr) {
			continue;
		}
		GB_ASSERT(e->kind == Entity_Variable);

		bool is_foreign = e->Variable.is_foreign;
		bool is_export  = e->Variable.is_export;


		lbModule *m = &gen->default_module;
		String name = lb_get_entity_name(m, e);

		lbValue g = {};
		g.value = LLVMAddGlobal(m->mod, lb_type(m, e->type), alloc_cstring(permanent_allocator(), name));
		g.type = alloc_type_pointer(e->type);
		if (e->Variable.thread_local_model != "") {
			LLVMSetThreadLocal(g.value, true);

			String m = e->Variable.thread_local_model;
			LLVMThreadLocalMode mode = LLVMGeneralDynamicTLSModel;
			if (m == "default") {
				mode = LLVMGeneralDynamicTLSModel;
			} else if (m == "localdynamic") {
				mode = LLVMLocalDynamicTLSModel;
			} else if (m == "initialexec") {
				mode = LLVMInitialExecTLSModel;
			} else if (m == "localexec") {
				mode = LLVMLocalExecTLSModel;
			} else {
				GB_PANIC("Unhandled thread local mode %.*s", LIT(m));
			}
			LLVMSetThreadLocalMode(g.value, mode);
		}
		if (is_foreign) {
			LLVMSetLinkage(g.value, LLVMExternalLinkage);
			LLVMSetExternallyInitialized(g.value, true);
			lb_add_foreign_library_path(m, e->Variable.foreign_library);
		} else {
			LLVMSetInitializer(g.value, LLVMConstNull(lb_type(m, e->type)));
		}
		if (is_export) {
			LLVMSetLinkage(g.value, LLVMDLLExportLinkage);
			LLVMSetDLLStorageClass(g.value, LLVMDLLExportStorageClass);
		} else if (!is_foreign) {
			if (USE_SEPARTE_MODULES) {
				LLVMSetLinkage(g.value, LLVMExternalLinkage);
			} else {
				LLVMSetLinkage(g.value, LLVMInternalLinkage);
			}
		}
		if (e->Variable.link_section.len > 0) {
			LLVMSetSection(g.value, alloc_cstring(permanent_allocator(), e->Variable.link_section));
		}

		lbGlobalVariable var = {};
		var.var = g;
		var.decl = decl;

		if (decl->init_expr != nullptr) {
			TypeAndValue tav = type_and_value_of_expr(decl->init_expr);
			if (!is_type_any(e->type) && !is_type_union(e->type)) {
				if (tav.mode != Addressing_Invalid) {
					if (tav.value.kind != ExactValue_Invalid) {
						ExactValue v = tav.value;
						lbValue init = lb_const_value(m, tav.type, v);
						LLVMSetInitializer(g.value, init.value);
						var.is_initialized = true;
					}
				}
			}
			if (!var.is_initialized &&
			    (is_type_untyped_nil(tav.type) || is_type_untyped_undef(tav.type))) {
				var.is_initialized = true;
			}
		}

		array_add(&global_variables, var);

		lb_add_entity(m, e, g);
		lb_add_member(m, name, g);


		if (m->debug_builder) {
			String global_name = e->token.string;
			if (global_name.len != 0 && global_name != "_") {
				LLVMMetadataRef llvm_file = lb_get_llvm_metadata(m, e->file);
				LLVMMetadataRef llvm_scope = llvm_file;

				LLVMBool local_to_unit = LLVMGetLinkage(g.value) == LLVMInternalLinkage;

				LLVMMetadataRef llvm_expr = LLVMDIBuilderCreateExpression(m->debug_builder, nullptr, 0);
				LLVMMetadataRef llvm_decl = nullptr;

				u32 align_in_bits = cast(u32)(8*type_align_of(e->type));

				LLVMMetadataRef global_variable_metadata = LLVMDIBuilderCreateGlobalVariableExpression(
					m->debug_builder, llvm_scope,
					cast(char const *)global_name.text, global_name.len,
					"", 0, // linkage
					llvm_file, e->token.pos.line,
					lb_debug_type(m, e->type),
					local_to_unit,
					llvm_expr,
					llvm_decl,
					align_in_bits
				);
				lb_set_llvm_metadata(m, g.value, global_variable_metadata);
				LLVMGlobalSetMetadata(g.value, 0, global_variable_metadata);
			}
		}
	}


	TIME_SECTION("LLVM Global Procedures and Types");
	for_array(i, info->entities) {
		Entity *e = info->entities[i];
		String    name  = e->token.string;
		DeclInfo *decl  = e->decl_info;
		Scope *   scope = e->scope;

		if ((scope->flags & ScopeFlag_File) == 0) {
			continue;
		}

		Scope *package_scope = scope->parent;
		GB_ASSERT(package_scope->flags & ScopeFlag_Pkg);

		switch (e->kind) {
		case Entity_Variable:
			// NOTE(bill): Handled above as it requires a specific load order
			continue;
		case Entity_ProcGroup:
			continue;

		case Entity_TypeName:
		case Entity_Procedure:
			break;
		}

		bool polymorphic_struct = false;
		if (e->type != nullptr && e->kind == Entity_TypeName) {
			Type *bt = base_type(e->type);
			if (bt->kind == Type_Struct) {
				polymorphic_struct = is_type_polymorphic(bt);
			}
		}

		if (!polymorphic_struct && !ptr_set_exists(min_dep_set, e)) {
			// NOTE(bill): Nothing depends upon it so doesn't need to be built
			continue;
		}

		lbModule *m = &gen->default_module;
		if (USE_SEPARTE_MODULES) {
			m = lb_pkg_module(gen, e->pkg);
		}

		String mangled_name = lb_get_entity_name(m, e);

		switch (e->kind) {
		case Entity_TypeName:
			lb_type(m, e->type);
			break;
		case Entity_Procedure:
			{
				lbProcedure *p = lb_create_procedure(m, e);
				array_add(&m->procedures_to_generate, p);
			}
			break;
		}
	}


	TIME_SECTION("LLVM Runtime Type Information Creation");
	lbProcedure *startup_type_info = lb_create_startup_type_info(default_module);

	TIME_SECTION("LLVM Runtime Startup Creation (Global Variables)");
	lbProcedure *startup_runtime = lb_create_startup_runtime(default_module, startup_type_info, global_variables);


	TIME_SECTION("LLVM Procedure Generation");
	for_array(j, gen->modules.entries) {
		lbModule *m = gen->modules.entries[j].value;
		for_array(i, m->procedures_to_generate) {
			lbProcedure *p = m->procedures_to_generate[i];
			if (p->is_done) {
				continue;
			}
			if (p->body != nullptr) { // Build Procedure
				m->curr_procedure = p;
				lb_begin_procedure_body(p);
				lb_build_stmt(p, p->body);
				lb_end_procedure_body(p);
				p->is_done = true;
				m->curr_procedure = nullptr;
			}
			lb_end_procedure(p);

			// Add Flags
			if (p->body != nullptr) {
				if (p->name == "memcpy" || p->name == "memmove" ||
				    p->name == "runtime.mem_copy" || p->name == "mem_copy_non_overlapping" ||
				    string_starts_with(p->name, str_lit("llvm.memcpy")) ||
				    string_starts_with(p->name, str_lit("llvm.memmove"))) {
					p->flags |= lbProcedureFlag_WithoutMemcpyPass;
				}
			}

			if (!m->debug_builder && LLVMVerifyFunction(p->value, LLVMReturnStatusAction)) {
				gb_printf_err("LLVM CODE GEN FAILED FOR PROCEDURE: %.*s\n", LIT(p->name));
				LLVMDumpValue(p->value);
				gb_printf_err("\n\n\n\n");
				String filepath_ll = lb_filepath_ll_for_module(m);
				if (LLVMPrintModuleToFile(m->mod, cast(char const *)filepath_ll.text, &llvm_error)) {
					gb_printf_err("LLVM Error: %s\n", llvm_error);
				}
				LLVMVerifyFunction(p->value, LLVMPrintMessageAction);
				gb_exit(1);
			}
		}
	}


	if (!(build_context.build_mode == BuildMode_DynamicLibrary && !has_dll_main)) {
		TIME_SECTION("LLVM main");
		lb_create_main_procedure(default_module, startup_runtime);
	}

	if (build_context.ODIN_DEBUG) {
		TIME_SECTION("LLVM Debug Info Complete Types and Finalize");
		for_array(j, gen->modules.entries) {
			lbModule *m = gen->modules.entries[j].value;
			if (m->debug_builder != nullptr) {
				lb_debug_complete_types(m);
				LLVMDIBuilderFinalize(m->debug_builder);
			}
		}
	}


	TIME_SECTION("LLVM Function Pass");
	for_array(i, gen->modules.entries) {
		lbModule *m = gen->modules.entries[i].value;

		lb_llvm_function_pass_worker_proc(m);
	}

	TIME_SECTION("LLVM Module Pass");

	for_array(i, gen->modules.entries) {
		lbModule *m = gen->modules.entries[i].value;

		auto wd = gb_alloc_item(permanent_allocator(), lbLLVMModulePassWorkerData);
		wd->m = m;
		wd->target_machine = target_machines[i];

		lb_llvm_module_pass_worker_proc(wd);
	}


	llvm_error = nullptr;
	defer (LLVMDisposeMessage(llvm_error));

	LLVMCodeGenFileType code_gen_file_type = LLVMObjectFile;
	if (build_context.build_mode == BuildMode_Assembly) {
		code_gen_file_type = LLVMAssemblyFile;
	}

	for_array(j, gen->modules.entries) {
		lbModule *m = gen->modules.entries[j].value;
		if (LLVMVerifyModule(m->mod, LLVMReturnStatusAction, &llvm_error)) {
			gb_printf_err("LLVM Error:\n%s\n", llvm_error);
			if (build_context.keep_temp_files) {
				TIME_SECTION("LLVM Print Module to File");
				String filepath_ll = lb_filepath_ll_for_module(m);
				if (LLVMPrintModuleToFile(m->mod, cast(char const *)filepath_ll.text, &llvm_error)) {
					gb_printf_err("LLVM Error: %s\n", llvm_error);
					gb_exit(1);
					return;
				}
			}
			gb_exit(1);
			return;
		}
	}
	llvm_error = nullptr;
	if (build_context.keep_temp_files ||
	    build_context.build_mode == BuildMode_LLVM_IR) {
		TIME_SECTION("LLVM Print Module to File");

		for_array(j, gen->modules.entries) {
			lbModule *m = gen->modules.entries[j].value;

			if (lb_is_module_empty(m)) {
				continue;
			}

			String filepath_ll = lb_filepath_ll_for_module(m);
			if (LLVMPrintModuleToFile(m->mod, cast(char const *)filepath_ll.text, &llvm_error)) {
				gb_printf_err("LLVM Error: %s\n", llvm_error);
				gb_exit(1);
				return;
			}
			array_add(&gen->output_temp_paths, filepath_ll);

		}
		if (build_context.build_mode == BuildMode_LLVM_IR) {
			gb_exit(0);
			return;
		}
	}


	TIME_SECTION("LLVM Add Foreign Library Paths");

	for_array(j, gen->modules.entries) {
		lbModule *m = gen->modules.entries[j].value;
		for_array(i, m->info->required_foreign_imports_through_force) {
			Entity *e = m->info->required_foreign_imports_through_force[i];
			lb_add_foreign_library_path(m, e);
		}

		if (lb_is_module_empty(m)) {
			continue;
		}
	}

	TIME_SECTION("LLVM Object Generation");

	if (do_threading) {
		for_array(j, gen->modules.entries) {
			lbModule *m = gen->modules.entries[j].value;
			if (lb_is_module_empty(m)) {
				continue;
			}

			String filepath_ll = lb_filepath_ll_for_module(m);
			String filepath_obj = lb_filepath_obj_for_module(m);
			array_add(&gen->output_object_paths, filepath_obj);
			array_add(&gen->output_temp_paths, filepath_ll);

			auto *wd = gb_alloc_item(heap_allocator(), lbLLVMEmitWorker);
			wd->target_machine = target_machines[j];
			wd->code_gen_file_type = code_gen_file_type;
			wd->filepath_obj = filepath_obj;
			wd->m = m;
			thread_pool_add_task(&lb_thread_pool, lb_llvm_emit_worker_proc, wd);
		}

		thread_pool_start(&lb_thread_pool);
		thread_pool_wait_to_process(&lb_thread_pool);
	} else {
		for_array(j, gen->modules.entries) {
			lbModule *m = gen->modules.entries[j].value;
			if (lb_is_module_empty(m)) {
				continue;
			}

			String filepath_obj = lb_filepath_obj_for_module(m);
			array_add(&gen->output_object_paths, filepath_obj);

			String short_name = remove_directory_from_path(filepath_obj);
			gbString section_name = gb_string_make(heap_allocator(), "LLVM Generate Object: ");
			section_name = gb_string_append_length(section_name, short_name.text, short_name.len);

			TIME_SECTION_WITH_LEN(section_name, gb_string_length(section_name));

			if (LLVMTargetMachineEmitToFile(target_machines[j], m->mod, cast(char *)filepath_obj.text, code_gen_file_type, &llvm_error)) {
				gb_printf_err("LLVM Error: %s\n", llvm_error);
				gb_exit(1);
				return;
			}
		}
	}



#undef TIME_SECTION
}
