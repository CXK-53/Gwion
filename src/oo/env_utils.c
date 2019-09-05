#include <stdlib.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "env.h"
#include "value.h"
#include "traverse.h"
#include "type.h"
#include "context.h"
#include "nspc.h"
#include "vm.h"
#include "parse.h"

ANN Map env_label(const Env env) {
  return &env->context->lbls;
}

ANN Nspc env_nspc(const Env env) {
  return env->context->nspc;
}

#define GET(a,b) ((a) & (b)) == (b)
ANN m_bool env_access(const Env env, const ae_flag flag, const loc_t pos) {
  if(env->scope->depth) {
   if(GET(flag, ae_flag_global))
      ERR_B(pos, _("'global' can only be used at %s scope."),
          GET(flag, ae_flag_global) && !env->class_def ?
           "file" : "class")
  }
  if((GET(flag, ae_flag_static) || GET(flag, ae_flag_private) ||
      GET(flag, ae_flag_protect)) && (!env->class_def || env->scope->depth))
      ERR_B(pos, _("static/private/protect can only be used at class scope."))
  return GW_OK;
}

ANN m_bool env_storage(const Env env, ae_flag flag, const loc_t pos) {
  CHECK_BB(env_access(env, flag, pos))
  return !(env->class_def && GET(flag, ae_flag_global)) ? GW_OK :GW_ERROR;
}

ANN Type find_type(const Env env, ID_List path) {
  Type type = nspc_lookup_type1(env->curr, path->xid);
  CHECK_OO(type)
  Nspc nspc = type->nspc;
  path = path->next;
  while(path) {
    const Symbol xid = path->xid;
    if(nspc) {
      Type t = nspc_lookup_type1(nspc, xid);
      while(!t && type && type->e->parent) {
        if(type->e->parent->nspc) // should we break sooner ?
          t = nspc_lookup_type1(type->e->parent->nspc, xid); // was lookup2
        type = type->e->parent;
      }
      if(!t)
        ERR_O(path->pos, _("...(cannot find class '%s' in nspc '%s')"), s_name(xid), nspc->name)
      type = t;
    }
    nspc = type->nspc;
    path = path->next;
  }
  return type;
}

ANN m_bool already_defined(const Env env, const Symbol s, const loc_t pos) {
  const Value v = nspc_lookup_value0(env->curr, s);
  if(!v || isa(v->type, t_class) > 0)
    return GW_OK;
  env_err(env, pos,
      _("'%s' already declared as variable of type '%s'."), s_name(s), v->type->name);
  return GW_ERROR;
}

