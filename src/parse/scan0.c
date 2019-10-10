#include <string.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "env.h"
#include "type.h"
#include "value.h"
#include "func.h"
#include "nspc.h"
#include "vm.h"
#include "traverse.h"
#include "template.h"
#include "parser.h"
#include "parse.h"
#include "cpy_ast.h"
#include "object.h"
#include "instr.h"
#include "operator.h"
#include "import.h"

#include "context.h"

static inline void add_type(const Env env, const Nspc nspc, const Type t) {
  nspc_add_type_front(nspc, insert_symbol(t->name), t);
}

static inline void context_global(const Env env) {
  if(env->context)
    env->context->global = 1;
}

static inline Type scan0_type(const Env env, const m_uint xid,
    const m_str name, const Type t) {
  const Type type = new_type(env->gwion->mp, xid, name, t);
  type->e->ctx = env->context;
  return type;
}

ANN static Value mk_class(const Env env, const Type base) {
  const Symbol sym = insert_symbol(base->name);
  const Type t = type_copy(env->gwion->mp, env->gwion->type[et_class]);
t->e->ctx = env->context;
  const Value v = new_value(env->gwion->mp, t, s_name(sym));
  t->e->d.base_type = base;
// set from
  v->from->owner = base->e->owner;
  SET_FLAG(v, const | ae_flag_checked);
  nspc_add_value_front(base->e->owner, sym, v);
  return v;
}

ANN static inline m_bool scan0_defined(const Env env, const Symbol s, const loc_t pos) {
  if(nspc_lookup_type0(env->curr, s))
    ERR_B(pos, _("type '%s' already defined"), s_name(s));
  return already_defined(env, s, pos);
}

ANN static void fptr_assign(const Env env, const Fptr_Def fptr) {
  const Func_Def def = fptr->type->e->d.func->def;
  if(GET_FLAG(fptr->base->td, global)) {
    context_global(env);
    SET_FLAG(fptr->value, global);
    SET_FLAG(fptr->base->func, global);
    SET_FLAG(def, global);
  } else if(!GET_FLAG(fptr->base->td, static)) {
    SET_FLAG(fptr->value, member);
    SET_FLAG(fptr->base->func, member);
    SET_FLAG(def, member);
    def->stack_depth += SZ_INT;
  } else {
    SET_FLAG(fptr->value, static);
    SET_FLAG(fptr->base->func, static);
    SET_FLAG(def, static);
  }
  if(GET_FLAG(def, variadic))
    def->stack_depth += SZ_INT;
  fptr->value->from->owner_class = env->class_def;
}

static void fptr_def(const Env env, const Fptr_Def fptr) {
  const Func_Def def = new_func_def(env->gwion->mp, new_func_base(env->gwion->mp, fptr->base->td, fptr->base->xid, fptr->base->args),
    NULL,fptr->base->td->flag, loc_cpy(env->gwion->mp, td_pos(fptr->base->td)));
  fptr->base->func = new_func(env->gwion->mp, s_name(fptr->base->xid), def);
  fptr->value->d.func_ref = fptr->base->func;
  fptr->base->func->value_ref = fptr->value;
  fptr->type->e->d.func = fptr->base->func;
  def->base->tmpl = fptr->base->tmpl;
  def->base->func = fptr->base->func;
}

ANN m_bool scan0_fptr_def(const Env env, const Fptr_Def fptr) {
  CHECK_BB(env_access(env, fptr->base->td->flag, td_pos(fptr->base->td)))
  CHECK_OB(known_type(env, fptr->base->td))
  CHECK_BB(scan0_defined(env, fptr->base->xid, td_pos(fptr->base->td)));
  const m_str name = s_name(fptr->base->xid);
  const Type t = scan0_type(env, env->gwion->type[et_fptr]->xid, name, env->gwion->type[et_fptr]);
  t->e->owner = !(!env->class_def && GET_FLAG(fptr->base->td, global)) ?
    env->curr : env->global_nspc;
  if(GET_FLAG(fptr->base->td, global))
    context_global(env);
  t->nspc = new_nspc(env->gwion->mp, name);
  t->flag = fptr->base->td->flag;
  fptr->type = t;
  fptr->value = mk_class(env, t);
// set owner ?
  fptr->value->from->owner = env->curr;
  fptr->value->from->owner_class = env->class_def;
  fptr_def(env, fptr);
  if(env->class_def)
    fptr_assign(env, fptr);
  SET_FLAG(fptr->value, func);
  add_type(env, t->e->owner, t);
  return GW_OK;
}

static OP_CHECK(opck_implicit_similar) {
  const Exp_Binary *bin = (Exp_Binary*)data;
  return bin->rhs->type;
}

static OP_CHECK(opck_cast_similar) {
  const Exp_Cast *cast = (Exp_Cast*)data;
  return exp_self(cast)->type;
}

ANN static void scan0_implicit_similar(const Env env, const Type lhs, const Type rhs) {
  struct Op_Import opi = { .op=insert_symbol("@implicit"), .lhs=lhs, .rhs=rhs, .ck=opck_implicit_similar };
  add_op(env->gwion, &opi);
  opi.op=insert_symbol("$");
  opi.ck = opck_cast_similar;
  add_op(env->gwion, &opi);
  opi.lhs=rhs;
  opi.rhs=lhs;
  add_op(env->gwion, &opi);
  opi.ck = opck_usr_implicit;
  opi.op=insert_symbol("@implicit");
  add_op(env->gwion, &opi);
}

ANN static void typedef_simple(const Env env, const Type_Def tdef, const Type base) {
  const Type t = scan0_type(env, ++env->scope->type_xid, s_name(tdef->xid), base);
  t->size = base->size;
  const Nspc nspc = (!env->class_def && GET_FLAG(tdef->ext, global)) ?
  env->global_nspc : env->curr;
  if(GET_FLAG(tdef->ext, global))
    context_global(env);
  add_type(env, nspc, t);
  t->e->owner = nspc;
  tdef->type = t;
  if(base->nspc)
    ADD_REF((t->nspc = base->nspc));
  t->flag = tdef->ext->flag | ae_flag_checked;
  scan0_implicit_similar(env, t, base);
  if(tdef->ext->array && !tdef->ext->array->exp)
    SET_FLAG(t, empty);
}

ANN static m_bool typedef_complex(const Env env, const Type_Def tdef, const Type base) {
  const ae_flag flag = base->e->def ? base->e->def->flag : 0;
  const Class_Def cdef = new_class_def(env->gwion->mp, flag, tdef->xid, tdef->ext, NULL,
    loc_cpy(env->gwion->mp, td_pos(tdef->ext)));
  CHECK_BB(scan0_class_def(env, cdef))
  tdef->type = cdef->base.type;
  cdef->base.tmpl = tdef->tmpl;
  return GW_OK;
}

ANN static void typedef_fptr(const Env env, const Type_Def tdef, const Type base) {
  tdef->type = type_copy(env->gwion->mp, base);
  ADD_REF(tdef->type->nspc)
  tdef->type->name = s_name(tdef->xid);
  tdef->type->e->parent = base;
  add_type(env, env->curr, tdef->type);
  mk_class(env, tdef->type);
  if(base->e->d.func->def->base->tmpl)
    SET_FLAG(tdef->type, func);
}

ANN m_bool scan0_type_def(const Env env, const Type_Def tdef) {
  CHECK_BB(env_access(env, tdef->ext->flag, td_pos(tdef->ext)))
  DECL_OB(const Type, base, = tdef->tmpl ? find_type(env, tdef->ext->xid) : known_type(env, tdef->ext))
  CHECK_BB(scan0_defined(env, tdef->xid, td_pos(tdef->ext)))
  if(isa(base, env->gwion->type[et_function]) < 0) {
    if(!tdef->ext->types && (!tdef->ext->array || !tdef->ext->array->exp))
      typedef_simple(env, tdef, base);
    else
      CHECK_BB(typedef_complex(env, tdef, base))
  } else
    typedef_fptr(env, tdef, base);
  SET_FLAG(tdef->type, typedef);
  return GW_OK;
}

ANN m_bool scan0_enum_def(const Env env, const Enum_Def edef) {
  CHECK_BB(env_storage(env, edef->flag, edef->pos))
  if(edef->xid) {
    const Value v = nspc_lookup_value1(env->curr, edef->xid);
    if(v)
      ERR_B(edef->pos, _("'%s' already declared as variable of type '%s'."),
        s_name(edef->xid),  v->type->name)
    CHECK_BB(scan0_defined(env, edef->xid, edef->pos))
  }
  const Type t = type_copy(env->gwion->mp, env->gwion->type[et_int]);
  t->xid = ++env->scope->type_xid;
  t->name = edef->xid ? s_name(edef->xid) : "int";
  t->e->parent = env->gwion->type[et_int];
  const Nspc nspc = GET_FLAG(edef, global) ? env->global_nspc : env->curr;
  t->e->owner = nspc;
  if(GET_FLAG(edef, global))
    context_global(env);
  edef->t = t;
  if(edef->xid) {
    add_type(env, nspc, t);
    mk_class(env, t);
  }
  scan0_implicit_similar(env, t, env->gwion->type[et_int]);
  return GW_OK;
}

ANN static Type union_type(const Env env, const Nspc nspc, const Symbol s, const m_bool add) {
  const m_str name = s_name(s);
  const Type t = type_copy(env->gwion->mp, env->gwion->type[et_union]);
  t->xid = ++env->scope->type_xid;
  t->name = name;
  t->nspc = new_nspc(env->gwion->mp, name);
  t->nspc->parent = nspc;
  t->nspc->is_union = 1;
  t->e->owner = nspc;
  t->e->parent = env->gwion->type[et_union];
  if(add) {
    add_type(env, nspc, t);
    mk_class(env, t);
  }
  return t;
}

ANN m_bool scan0_union_def(const Env env, const Union_Def udef) {
  CHECK_BB(env_storage(env, udef->flag, udef->pos))
  const m_uint scope = !GET_FLAG(udef, global) ? env->scope->depth :
      env_push_global(env);
  if(GET_FLAG(udef, global))
    context_global(env);
  if(udef->xid) {
    CHECK_BB(scan0_defined(env, udef->xid, udef->pos))
    const Nspc nspc = !GET_FLAG(udef, global) ?
      env->curr : env->global_nspc;
    const Type t = union_type(env, nspc, udef->type_xid ?: udef->xid,
       !!udef->type_xid);
    udef->value = new_value(env->gwion->mp, t, s_name(udef->xid));
    valuefrom(env, udef->value->from);
    nspc_add_value(nspc, udef->xid, udef->value);
    add_type(env, nspc, t);
    SET_FLAG(t, scan1 | ae_flag_union);
    SET_FLAG(udef->value, checked | udef->flag);
    if(env->class_def && !GET_FLAG(udef, static)) {
      SET_FLAG(udef->value, member);
      SET_FLAG(udef, member);
    }
  } else if(udef->type_xid) {
    CHECK_BB(scan0_defined(env, udef->type_xid, udef->pos))
    const Nspc nspc = !GET_FLAG(udef, global) ?
      env->curr : env->global_nspc;
    udef->type = union_type(env, nspc, udef->type_xid, 1);
    SET_FLAG(udef->type, checked);
    SET_FLAG(udef->type, scan1 | ae_flag_union);
  } else {
    const Nspc nspc = !GET_FLAG(udef, global) ?
      env->curr : env->global_nspc;
    const size_t line_len = num_digit(udef->pos->first_line);
    const size_t col_len = num_digit(udef->pos->first_column);
    char name[strlen(env->name) + line_len + col_len + 10]; // add pos
    sprintf(name, "@union:%s:%u:%u", env->name, udef->pos->first_line, udef->pos->first_column);
    const Symbol sym = insert_symbol(name);
    const Type t = union_type(env, nspc, sym, 1);
    udef->value = new_value(env->gwion->mp, t, s_name(sym));
    valuefrom(env, udef->value->from);
    nspc_add_value(nspc, sym, udef->value);
    add_type(env, nspc, t);
    SET_FLAG(udef->value, checked | ae_flag_pure | udef->flag);
    SET_FLAG(t, scan1 | ae_flag_union);
  }
  if(udef->tmpl) {
    if(tmpl_base(udef->tmpl)) {
      assert(udef->type_xid);
      const Class_Def cdef = new_class_def(env->gwion->mp, udef->flag, udef->type_xid,
          NULL, (Class_Body)udef->l, loc_cpy(env->gwion->mp, udef->pos));
      udef->type->e->def = cdef;
      cdef->base.tmpl = udef->tmpl;
      cdef->base.type = udef->type;
      cdef->list = cpy_decl_list(env->gwion->mp, udef->l);
      SET_FLAG(cdef, union);
      SET_FLAG(udef->type, pure);
      SET_FLAG(udef, template);
      SET_FLAG(udef->type, template);
    }
    SET_FLAG(udef->type, union);
  }
  if(GET_FLAG(udef, global))
    env_pop(env, scope);
  return GW_OK;
}

ANN static m_bool scan0_class_def_pre(const Env env, const Class_Def cdef) {
  CHECK_BB(env_storage(env, cdef->flag, cdef->pos))
  CHECK_BB(isres(env, cdef->base.xid, cdef->pos))
  return GW_OK;
}

ANN static void set_template(const Type t, const Class_Def cdef) {
  SET_FLAG(t, template);
  SET_FLAG(cdef, template);
}


ANN static void inherit_tmpl(const Env env, const Class_Def cdef) {
  const ID_List list = env->class_def->e->def->base.tmpl->list;
  const ID_List prev_list = cpy_id_list(env->gwion->mp, list);
  ID_List il = prev_list;
  while(il->next && (il = il->next));
  il->next = cdef->base.tmpl->list;
  cdef->base.tmpl->list = prev_list;
}

ANN static Type scan0_class_def_init(const Env env, const Class_Def cdef) {
  CHECK_BO(scan0_defined(env, cdef->base.xid, cdef->pos))
  const Type t = scan0_type(env, ++env->scope->type_xid, s_name(cdef->base.xid), env->gwion->type[et_object]);
  t->e->owner = env->curr;
  t->nspc = new_nspc(env->gwion->mp, t->name);
  t->nspc->parent = env->curr;
  t->e->def = cdef;
  t->flag = cdef->flag;
  add_type(env, t->e->owner, t);
  if(cdef->base.tmpl) {
    if(SAFE_FLAG(env->class_def, template) && env->class_def->e->def->base.tmpl->call == (Type_List)1)
      inherit_tmpl(env, cdef);
    set_template(t, cdef);
  } else if(SAFE_FLAG(env->class_def, template)) {
    cdef->base.tmpl = new_tmpl(env->gwion->mp, env->class_def->e->def->base.tmpl->list, -1);
    set_template(t, cdef);
  }
  if(cdef->base.ext && cdef->base.ext->array)
    SET_FLAG(t, typedef);
  return t;
}

ANN static m_bool scan0_section(const Env env, const Section* section) {
  if(section->section_type == ae_section_class)
    return scan0_class_def(env, section->d.class_def);
  if(section->section_type == ae_section_enum)
    return scan0_enum_def(env, section->d.enum_def);
  if(section->section_type == ae_section_union)
    return scan0_union_def(env, section->d.union_def);
  if(section->section_type == ae_section_fptr)
    return scan0_fptr_def(env, section->d.fptr_def);
  if(section->section_type == ae_section_type)
    return scan0_type_def(env, section->d.type_def);
  return GW_OK;
}

ANN static m_bool scan0_class_def_inner(const Env env, const Class_Def cdef) {
  CHECK_OB((cdef->base.type = scan0_class_def_init(env, cdef)))
  SET_FLAG(cdef->base.type, scan0);
  if(cdef->body) {
    int call = cdef->base.tmpl && !cdef->base.tmpl->call;
    if(call)cdef->base.tmpl->call = (Type_List)1;
      CHECK_BB(env_body(env, cdef, scan0_section))
    if(call)cdef->base.tmpl->call = NULL;
  }
  (void)mk_class(env, cdef->base.type);
  return GW_OK;
}

ANN m_bool scan0_class_def(const Env env, const Class_Def cdef) {
  if(GET_FLAG(cdef, global)) {
    vector_add(&env->scope->nspc_stack, (vtype)env->curr);
    env->curr = env->global_nspc;
    context_global(env);
  }
  const m_bool ret = scan0_class_def_pre(env, cdef) > 0 ?
    scan0_class_def_inner(env, cdef) : GW_ERROR;
  if(GET_FLAG(cdef, global))
    env->curr = (Nspc)vector_pop(&env->scope->nspc_stack);
  return ret;
}

ANN m_bool scan0_ast(const Env env, Ast ast) {
  do CHECK_BB(scan0_section(env, ast->section))
  while((ast = ast->next));
  return GW_OK;
}
