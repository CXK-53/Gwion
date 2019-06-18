#include <stdlib.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "vm.h"
#include "env.h"
#include "type.h"
#include "nspc.h"
#include "value.h"
#include "func.h"
#include "object.h"
#include "gwion.h"
#include "operator.h"

ANN void nspc_commit(const Nspc nspc) {
  scope_commit(nspc->info->value);
  scope_commit(nspc->info->func);
  scope_commit(nspc->info->type);
}

ANN static inline void nspc_release_object(const Nspc a, Value value, Gwion gwion) {
  if((GET_FLAG(value, static) && a->info->class_data) ||
    (value->d.ptr && GET_FLAG(value, builtin))) {
    const M_Object obj = value->d.ptr ? (M_Object)value->d.ptr :
        *(M_Object*)(a->info->class_data + value->offset);
       release(obj, gwion->vm->cleaner_shred);
  }
}

ANN static void free_nspc_value(const Nspc a, Gwion gwion) {
  struct scope_iter iter = { a->info->value, 0, 0 };
  Value v;
  if(!a->is_union) {
    while(scope_iter(&iter, &v) > 0) {
const Type  t = (GET_FLAG(v, builtin) && v->type->array_depth) ?
  v->type : NULL;
      if(isa(v->type, t_object) > 0)
        nspc_release_object(a, v, gwion);
      REM_REF(v, gwion);
if(t)
    REM_REF(t, gwion);
    }
  }
  free_scope(gwion->mp, a->info->value);
}

#define describe_nspc_free(A, b) \
ANN static void nspc_free_##b(Nspc n, Gwion gwion) {\
  struct scope_iter iter = { n->info->b, 0, 0 };\
  A a;\
  while(scope_iter(&iter, &a) > 0) \
    REM_REF(a, gwion);\
  free_scope(gwion->mp, n->info->b);\
}

describe_nspc_free(Func, func)
describe_nspc_free(Type, type)

ANN static void free_nspc(Nspc a, Gwion gwion) {
  free_nspc_value(a, gwion);
  nspc_free_func(a, gwion);
  if(a->info->op_map.ptr)
    free_op_map(&a->info->op_map, gwion);
  nspc_free_type(a, gwion);

  if(a->info->class_data)
    mp_free2(gwion->mp, a->info->class_data_size, a->info->class_data);
  if(a->info->vtable.ptr)
    vector_release(&a->info->vtable);
  mp_free(gwion->mp, NspcInfo, a->info);
  if(a->pre_ctor)
    REM_REF(a->pre_ctor, gwion);
  if(a->dtor)
    REM_REF(a->dtor, gwion);
  mp_free(gwion->mp, Nspc, a);
}

ANN Nspc new_nspc(MemPool p, const m_str name) {
  const Nspc a = mp_calloc(p, Nspc);
  a->name = name;
  a->info = mp_calloc(p, NspcInfo);
  a->info->value = new_scope(p);
  a->info->type = new_scope(p);
  a->info->func = new_scope(p);
  a->ref = new_refcount(p, free_nspc);
  return a;
}
