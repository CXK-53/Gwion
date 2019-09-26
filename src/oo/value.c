#include <stdlib.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "env.h"
#include "vm.h"
#include "gwion.h"
#include "value.h"
#include "type.h"

ANN static void free_value(Value a, Gwion gwion) {
  const Type t = a->type;
  if(!GET_FLAG(a, func) && a->d.ptr && !GET_FLAG(a, union) &&
      !(GET_FLAG(a, enum) && GET_FLAG(a, builtin) && a->from->owner_class)
      && isa(t, gwion->type[et_object]) < 0)
   _mp_free(gwion->mp, t->size, a->d.ptr);
  if(isa(t, gwion->type[et_class]) > 0/* || isa(a->type, t_function) > 0*/)
    REM_REF(t, gwion)
  mp_free(gwion->mp, ValueFrom, a->from);
  mp_free(gwion->mp, Value, a);
}

ANN Value new_value(MemPool p, const Type type, const m_str name) {
  const Value a = mp_calloc(p, Value);
  a->from = mp_calloc(p, ValueFrom);
  a->type       = type;
  a->name       = name;
  a->ref = new_refcount(p, free_value);
  return a;
}

ANN void valuefrom(const Env env, struct ValueFrom_ *from) {
  from->owner = env->curr;
  from->owner_class = env->class_def;
  from->ctx = env->context;
}
