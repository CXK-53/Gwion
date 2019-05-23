#include <stdlib.h>
#include <string.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "vm.h"
#include "env.h"
#include "gwion.h"
#include "type.h"
#include "instr.h"
#include "object.h"
#include "array.h"
#include "emit.h"
#include "import.h"
#include "operator.h"
#include "traverse.h"
#include "parse.h"

struct M_Vector_ {
  m_bit* ptr;
};
#define ARRAY_OFFSET SZ_INT * 4
#define ARRAY_PTR(array) (array->ptr + ARRAY_OFFSET)
#define ARRAY_LEN(array) *(m_uint*)(array->ptr)
#define ARRAY_SIZE(array) *(m_uint*)(array->ptr + SZ_INT)
#define ARRAY_CAP(array) *(m_uint*)(array->ptr + SZ_INT*2)

ANN m_uint m_vector_size(const M_Vector v) {
  return ARRAY_LEN(v);
}

M_Vector new_m_vector(MemPool p, const m_uint size) {
  const M_Vector array = mp_calloc(p, M_Vector);
  const size_t sz = (ARRAY_OFFSET*SZ_INT) + (2*size);
  array->ptr   = (m_bit*)xcalloc(1, sz);
  ARRAY_CAP(array)   = 2;
  ARRAY_SIZE(array)  = size;
  return array;
}

M_Vector new_m_vector2(MemPool p, const m_uint size, const m_uint len) {
  const M_Vector array = mp_calloc(p, M_Vector);
  const size_t sz = (ARRAY_OFFSET*SZ_INT) + (len*size);
  array->ptr   = (m_bit*)xcalloc(1, sz);
  m_uint cap = 1;
  while(cap < len)
    cap *= 2;
  ARRAY_CAP(array)   = cap;
  ARRAY_SIZE(array)  = size;
  ARRAY_LEN(array) = len;
  return array;
}

void free_m_vector(MemPool p, M_Vector a) {
  xfree(a->ptr);
  mp_free(p, M_Vector, a);
}

static DTOR(array_dtor) {
  const Type t = o->type_ref;
  const Type base = array_base(t);
  struct M_Vector_* a = ARRAY(o);
  if(t->array_depth > 1 || isa(base, t_object) > 0)
    for(m_uint i = 0; i < ARRAY_LEN(a); ++i)
      release(*(M_Object*)(ARRAY_PTR(a) + i * SZ_INT), shred);
  free_m_vector(shred->info->mp, a);
  REM_REF(t, shred->info->vm->gwion)
}

ANN M_Object new_array(MemPool p, const Type t, const m_uint length) {
  const M_Object a = new_object(p, NULL, t);
  const m_uint depth = t->array_depth;
  const m_uint size = depth > 1 ? SZ_INT : array_base(t)->size;
  ARRAY(a) = new_m_vector2(p, size,length);
  ADD_REF(t);
  return a;
}

ANN void m_vector_get(const M_Vector v, const m_uint i, void* c) {
  const m_uint size = ARRAY_SIZE(v);
  memcpy(c, ARRAY_PTR(v) + i * size, size);
}

ANN void m_vector_add(const M_Vector v, const void* data) {
  const m_uint size = ARRAY_SIZE(v);
  if(++ARRAY_LEN(v) >= ARRAY_CAP(v)) {
    const m_uint cap = ARRAY_CAP(v) *=2;
    v->ptr = (m_bit*)xrealloc(v->ptr, ARRAY_OFFSET + cap * size);
  }
  memcpy(ARRAY_PTR(v) + (ARRAY_LEN(v) - 1) * size, data, size);
}

ANN void m_vector_set(const M_Vector v, const m_uint i, const void* data) {
  const m_uint size = ARRAY_SIZE(v);
  memcpy(ARRAY_PTR(v) + i * size, data, size);
}

ANN void m_vector_rem(const M_Vector v, m_uint index) {
  const m_uint size = ARRAY_SIZE(v);
  char c[--ARRAY_LEN(v) * size];
  if(index)
    memcpy(c, ARRAY_PTR(v), index * size);
  ++index;
  memcpy(c + (index - 1) * size, ARRAY_PTR(v) + index * size, (ARRAY_CAP(v) - index) * size);
  if(ARRAY_LEN(v) < ARRAY_CAP(v) / 2) {
    const m_uint cap = ARRAY_CAP(v) /= 2;
    v->ptr = (m_bit*)xrealloc(v->ptr, ARRAY_OFFSET + cap * size);
  }
  memcpy(ARRAY_PTR(v), c, ARRAY_CAP(v) * size);
}

static MFUN(vm_vector_rem) {
  const m_int index = *(m_int*)(shred->reg + SZ_INT);
  const M_Vector v = ARRAY(o);
  if(index < 0 || (m_uint)index >= ARRAY_LEN(v))
    return;
  if(isa(o->type_ref, t_object) > 0) {
    M_Object obj;
    m_vector_get(v, (vtype)index, &obj);
    release(obj,shred);
  }
  m_vector_rem(v, (vtype)index);
}

ANN m_bit* m_vector_addr(const M_Vector v, const m_uint i) {
  return &*(m_bit*)(ARRAY_PTR(v) + i * ARRAY_SIZE(v));
}

static MFUN(vm_vector_size) {
  *(m_uint*)RETURN = ARRAY_LEN(ARRAY(o));
}

static MFUN(vm_vector_depth) {
  *(m_uint*)RETURN = o->type_ref->array_depth;
}

static MFUN(vm_vector_cap) {
  *(m_uint*)RETURN = ARRAY_CAP(ARRAY(o));
}

ANN static Type get_array_type(Type t) {
  while(t->e->d.base_type)
    t = t->e->d.base_type;
  return t;
}

#define ARRAY_OPCK                                        \
  const Exp_Binary* bin = (Exp_Binary*)data;              \
  const Type l = get_array_type(bin->lhs->type);          \
  const Type r = get_array_type(bin->rhs->type);          \
  if(isa(l, r) < 0)                                       \
    ERR_N(exp_self(bin)->pos, "array types do not match.")

static OP_CHECK(opck_array_at) {
  ARRAY_OPCK
  if(opck_const_rhs(env, data) == t_null)
    return t_null;
  if(bin->lhs->type->array_depth != bin->rhs->type->array_depth)
    ERR_N(exp_self(bin)->pos, "array depths do not match.")
  if(bin->rhs->exp_type == ae_exp_decl) {
    if(bin->rhs->d.exp_decl.list->self->array)
      ERR_N(exp_self(bin)->pos, "do not provide array for 'xxx @=> declaration'.")
  }
  bin->rhs->emit_var = 1;
  return bin->rhs->type;
}

static OP_CHECK(opck_array_shift) {
  ARRAY_OPCK
  if(bin->lhs->type->array_depth != bin->rhs->type->array_depth + 1)
    return t_null;
  return bin->lhs->type;
}

static OP_EMIT(opem_array_shift) {
  const Exp_Binary* bin = (Exp_Binary*)data;
  const Type type = bin->rhs->type;
  const Instr pop = emit_add_instr(emit, RegPop);
  pop->m_val = type->size;
  emit_add_instr(emit, GWOP_EXCEPT);
  emit_add_instr(emit, ArrayAppend);
  return GW_OK;
}

// check me. use common ancestor maybe
static OP_CHECK(opck_array_cast) {
  const Exp_Cast* cast = (Exp_Cast*)data;
  Type l = cast->exp->type;
  Type r = exp_self(cast)->type;
  while(!l->e->d.base_type)
    l = l->e->parent;
  while(!r->e->d.base_type)
    r = r->e->parent;
  if(l->array_depth == r->array_depth || isa(l->e->d.base_type, r->e->d.base_type) > 0)
    return l;
  return t_null;
}

static FREEARG(freearg_array) {
  ArrayInfo* info = (ArrayInfo*)instr->m_val;
  REM_REF((Type)vector_back(&info->type), gwion);
  vector_release(&info->type);
  mp_free(((Gwion)gwion)->mp, ArrayInfo, info);
}

GWION_IMPORT(array) {
  t_array  = gwi_mk_type(gwi, "Array", SZ_INT, t_object);
  SET_FLAG((t_array), abstract);
  CHECK_BB(gwi_class_ini(gwi,  t_array, NULL, array_dtor))

  CHECK_BB(gwi_item_ini(gwi, "int", "@array"))
  CHECK_BB(gwi_item_end(gwi, 0, NULL))

  CHECK_BB(gwi_func_ini(gwi, "int", "size", vm_vector_size))
  CHECK_BB(gwi_func_end(gwi, 0))
  CHECK_BB(gwi_func_ini(gwi, "int", "depth", vm_vector_depth))
  CHECK_BB(gwi_func_end(gwi, 0))

  CHECK_BB(gwi_func_ini(gwi, "int", "cap", vm_vector_cap))
  CHECK_BB(gwi_func_end(gwi, 0))

  CHECK_BB(gwi_func_ini(gwi, "int", "remove", vm_vector_rem))
  CHECK_BB(gwi_func_arg(gwi, "int", "index"))
  CHECK_BB(gwi_func_end(gwi, 0))

  CHECK_BB(gwi_class_end(gwi))
  CHECK_BB(gwi_oper_ini(gwi, "Array", (m_str)OP_ANY_TYPE, NULL))
  CHECK_BB(gwi_oper_add(gwi, opck_array_at))
  CHECK_BB(gwi_oper_end(gwi, op_ref, ObjectAssign))
  CHECK_BB(gwi_oper_add(gwi, opck_array_shift))
  CHECK_BB(gwi_oper_emi(gwi, opem_array_shift))
  CHECK_BB(gwi_oper_end(gwi, op_shl, NULL))
  CHECK_BB(gwi_oper_ini(gwi, "Array", "Array", NULL))
  CHECK_BB(gwi_oper_add(gwi, opck_array_cast))
  CHECK_BB(gwi_oper_emi(gwi, opem_basic_cast))
  CHECK_BB(gwi_oper_end(gwi, op_cast, NULL))
  register_freearg(gwi, ArrayAlloc, freearg_array);
  return GW_OK;
}

INSTR(ArrayBottom) {
  *(M_Object*)(*(m_uint**)REG(-SZ_INT * 4))[(*(m_int*)REG(-SZ_INT * 3))++] = *(M_Object*)REG(-SZ_INT);
}

INSTR(ArrayPost) {
  xfree(*(m_uint**)REG(0));
}

INSTR(ArrayInit) {// for litteral array
  const Type t = (Type)instr->m_val;
  const m_uint sz = *(m_uint*)REG(0);
  const m_uint off = instr->m_val2 * sz;
  POP_REG(shred, off - SZ_INT);
  const M_Object obj = new_array(shred->info->mp, t, sz);
  memcpy(ARRAY(obj)->ptr + ARRAY_OFFSET, REG(-SZ_INT), off);
  *(M_Object*)REG(-SZ_INT) = obj;
}

#define TOP -1

ANN static inline M_Object do_alloc_array_object(MemPool p, const ArrayInfo* info, const m_int cap) {
  struct Vector_ v = info->type;
  const Type t = (Type)vector_at(&v, (vtype)(-info->depth - 1));
  return new_array(p, t, (m_uint)cap);
}

ANN static inline M_Object do_alloc_array_init(ArrayInfo* info, const m_uint cap,
    const M_Object base) {
  for(m_uint i = 0; i < cap; ++i)
    info->data[(*info->d.idx)++] = (M_Object)m_vector_addr(ARRAY(base), i);
  return base;
}

ANN static M_Object do_alloc_array(const VM_Shred shred, ArrayInfo* info);
ANN static M_Object do_alloc_array_loop(const VM_Shred shred, ArrayInfo* info,
    const m_uint cap, const M_Object base) {
  for(m_uint i = 0; i < cap; ++i) {
    struct ArrayInfo_ aai = { info->depth + 1, info->type,
      info->base, info->data, { info->d.idx } , 0, info->is_obj };
    const M_Object next = do_alloc_array(shred, &aai);
    if(!next) {
      _release(base, shred);
      return NULL;
    }
    m_vector_set(ARRAY(base), i, &next);
  }
  return base;
}

ANN static M_Object do_alloc_array(const VM_Shred shred, ArrayInfo* info) {
  const m_int cap = *(m_int*)REG(info->depth * SZ_INT);
  if(cap < 0) {
    gw_err("[gwion](VM): NegativeArraySize: while allocating arrays...\n");
    return NULL;
  }
  const M_Object base = do_alloc_array_object(shred->info->mp, info, cap);
  return info->depth < TOP ? do_alloc_array_loop(shred, info, (m_uint)cap, base) :
    info->data ? do_alloc_array_init(info, (m_uint)cap, base) : base;
}

ANN static M_Object* init_array(const VM_Shred shred, const ArrayInfo* info, m_uint* num_obj) {
  m_int curr = -info->depth;
  while(curr <= TOP) {
    *num_obj *= *(m_uint*)REG(SZ_INT * curr);
    ++curr;
  }
  return *num_obj > 0 ? (M_Object*)xcalloc(*num_obj, SZ_INT) : NULL;
}

INSTR(ArrayAlloc) {
  const ArrayInfo* info = (ArrayInfo*)instr->m_val;
  m_uint num_obj = 1;
  m_int idx = 0;
  const m_bool is_obj = info->is_obj && !info->is_ref;
  struct ArrayInfo_ aai = { -info->depth, info->type, info->base,
         NULL, { &idx }, 0, info->is_obj};
  if(is_obj)
    aai.data = init_array(shred, info, &num_obj);
  const M_Object ref = do_alloc_array(shred, &aai);
  if(!ref) {
    gw_err("[Gwion](VM): (note: in shred[id=%" UINT_F ":%s])\n", shred->tick->xid, shred->info->name);
    vm_shred_exit(shred);
    return; // TODO make exception vararg
  }
  if(!is_obj) {
    POP_REG(shred, SZ_INT * (info->depth - 1));
    *(M_Object*)REG(-SZ_INT) = ref;
  } else {
    POP_REG(shred, SZ_INT * (info->depth - 4));
    *(M_Object*)REG(-SZ_INT*4) = ref;
    *(M_Object**)REG(-SZ_INT*3) = aai.data;
    *(m_uint*) REG(-SZ_INT*2) = 0;
    *(m_uint*) REG(-SZ_INT) = num_obj;
  }
}
