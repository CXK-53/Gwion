#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "env.h"
#include "type.h"
#include "nspc.h"
#include "value.h"
#include "func.h"
#include "vm.h"
#include "traverse.h"
#include "template.h"
#include "parse.h"
#include "context.h"

ANN static m_bool scan1_stmt_list(const Env env, Stmt_List list);
ANN static m_bool scan1_stmt(const Env env, Stmt stmt);

ANN static inline void type_contains(const Type base, const Type t) {
  const Vector v = &base->e->contains;
  if(!v->ptr)
    vector_init(v);
  if(vector_find(v, (vtype)t) == GW_ERROR) {
    vector_add(v, (vtype)t);
    if(base != t)
      ADD_REF(t);
  }
}

ANN static m_bool type_recursive(const Env env, const Type_Decl *td, const Type t) {
  if(env->class_def && !env->scope->depth) {
    type_contains(env->class_def, t);
    if(t->e->contains.ptr) {
      for(m_uint i = 0; i < vector_size(&t->e->contains); ++i) {
        if(env->class_def == (Type)vector_at(&t->e->contains, i) && !GET_FLAG(td, ref))
          ERR_B(td_pos(td), _("%s declared inside %s\n. (make it a ref ?)"),
              t->name, t == env->class_def ? "itself" : env->class_def->name);
      }
    }
  }
  return GW_OK;
}

ANN static Type void_type(const Env env, const Type_Decl* td) {
  DECL_OO(const Type, type, = known_type(env, td))
{
  const Type t = get_type(type);
  if(isa(t, env->gwion->type[et_object]) > 0)
    CHECK_BO(type_recursive(env, td, t))
}
  if(type->size)
    return type;
  ERR_O(td_pos(td), _("cannot declare variables of size '0' (i.e. 'void')..."))
}

ANN static Type scan1_exp_decl_type(const Env env, Exp_Decl* decl) {
  DECL_OO(const Type ,t, = void_type(env, decl->td))
  if(decl->td->xid && decl->td->xid->xid == insert_symbol("auto") && decl->type)
    return decl->type;
  if(!env->scope->depth && env->class_def && !GET_FLAG(decl->td, static))
    SET_FLAG(decl->td, member);
  if(GET_FLAG(t, private) && t->e->owner != env->curr)
    ERR_O(exp_self(decl)->pos, _("can't use private type %s"), t->name)
  if(GET_FLAG(t, protect) && (!env->class_def || isa(t, env->class_def) < 0))
    ERR_O(exp_self(decl)->pos, _("can't use protected type %s"), t->name)
  decl->base = t->e->def;
  return decl->type = t;
}

ANN m_bool scan1_exp_decl(const Env env, const Exp_Decl* decl) {
  CHECK_BB(env_storage(env, decl->td->flag, exp_self(decl)->pos))
  Var_Decl_List list = decl->list;
  ((Exp_Decl*)decl)->type = scan1_exp_decl_type(env, (Exp_Decl*)decl);
  CHECK_OB(decl->type)
  const m_bool global = GET_FLAG(decl->td, global);
  if(env->context)
    env->context->global = 1;
  const m_uint scope = !global ? env->scope->depth : env_push_global(env);
  const Nspc nspc = !global ? env->curr : env->global_nspc;
  do {
    const Var_Decl var = list->self;
    CHECK_BB(isres(env, var->xid, exp_self(decl)->pos))
    Type t = decl->type;
    const Value former = nspc_lookup_value0(env->curr, var->xid);
    if(former && t != env->gwion->type[et_auto])
      ERR_B(var->pos, _("variable %s has already been defined in the same scope..."),
              s_name(var->xid))
    if(var->array) {
      if(var->array->exp) {
        if(GET_FLAG(decl->td, ref))
          ERR_B(td_pos(decl->td), _("ref array must not have array expression.\n"
            "e.g: int @my_array[];\nnot: int @my_array[2];"))
        CHECK_BB(scan1_exp(env, var->array->exp))
      }
      t = array_type(env, decl->type, var->array->depth);
    } else if(GET_FLAG(t, abstract) && !GET_FLAG(decl->td, ref))
      ERR_B(exp_self(decl)->pos, _("Type '%s' is abstract, declare as ref. (use @)"), t->name)
    if(env->class_def)
      type_contains(env->class_def, t);
    const Value v = var->value = former ?: new_value(env->gwion->mp, t, s_name(var->xid));
    nspc_add_value(nspc, var->xid, v);
    v->flag = decl->td->flag;
    v->type = t;
    if(var->array && !var->array->exp)
      SET_FLAG(v, ref);
    if(!env->scope->depth && !env->class_def)
      SET_FLAG(v, global);
    v->d.ptr = var->addr;
    if(!env->scope->depth)
      valuefrom(env, v->from);
  } while((list = list->next));
  ((Exp_Decl*)decl)->type = decl->list->self->value->type;
  if(global)
    env_pop(env, scope);
  return GW_OK;
}

ANN static inline m_bool scan1_exp_binary(const Env env, const Exp_Binary* bin) {
  CHECK_BB(scan1_exp(env, bin->lhs))
  return scan1_exp(env, bin->rhs);
}

ANN static inline m_bool scan1_exp_primary(const Env env, const Exp_Primary* prim) {
  if(prim->primary_type == ae_primary_hack)
    return scan1_exp(env, prim->d.exp);
  if(prim->primary_type == ae_primary_array && prim->d.array->exp)
    return scan1_exp(env, prim->d.array->exp);
  if(prim->primary_type == ae_primary_tuple)
    return scan1_exp(env, prim->d.tuple.exp);
//  if(prim->primary_type == ae_primary_unpack)
//    return scan1_exp(env, prim->d.tuple.exp);
  return GW_OK;
}

ANN static inline m_bool scan1_exp_array(const Env env, const Exp_Array* array) {
  CHECK_BB(scan1_exp(env, array->base))
  return scan1_exp(env, array->array->exp);
}

ANN static inline m_bool scan1_exp_cast(const Env env, const Exp_Cast* cast) {
  return scan1_exp(env, cast->exp);
}

ANN static m_bool scan1_exp_post(const Env env, const Exp_Postfix* post) {
  CHECK_BB(scan1_exp(env, post->exp))
  if(post->exp->meta == ae_meta_var)
    return GW_OK;
  ERR_B(post->exp->pos, _("post operator '%s' cannot be used"
      " on non-mutable data-type..."), s_name(post->op));
}

ANN static m_bool scan1_exp_call(const Env env, const Exp_Call* exp_call) {
  if(exp_call->tmpl)
    return GW_OK;
  CHECK_BB(scan1_exp(env, exp_call->func))
  const Exp args = exp_call->args;
  return args ? scan1_exp(env, args) : GW_OK;
}

ANN static inline m_bool scan1_exp_dot(const Env env, const Exp_Dot* member) {
  if(member->base->next)
    ERR_B(member->base->pos, _("can't use multiple expression"
      " in dot member base expression"))
  return scan1_exp(env, member->base);
}

ANN static m_bool scan1_exp_if(const Env env, const Exp_If* exp_if) {
  CHECK_BB(scan1_exp(env, exp_if->cond))
  CHECK_BB(scan1_exp(env, exp_if->if_exp ?: exp_if->cond))
  return scan1_exp(env, exp_if->else_exp);
}

ANN static inline m_bool scan1_exp_unary(const restrict Env env, const Exp_Unary *unary) {
  if((unary->op == insert_symbol("spork") || unary->op == insert_symbol("fork")) && unary->code)
    { RET_NSPC(scan1_stmt(env, unary->code)) }
  return unary->exp ? scan1_exp(env, unary->exp) : GW_OK;
}

ANN static inline m_bool scan1_exp_typeof(const restrict Env env, const Exp_Typeof *exp) {
  return scan1_exp(env, exp->exp);
}

#define scan1_exp_lambda dummy_func
HANDLE_EXP_FUNC(scan1, m_bool, 1)

ANN static inline m_bool _scan1_stmt_match_case(const restrict Env env, const Stmt_Match stmt) {
  CHECK_BB(scan1_exp(env, stmt->cond))
  if(stmt->when)
    CHECK_BB(scan1_exp(env, stmt->when))
  return scan1_stmt_list(env, stmt->list);
}

ANN static inline m_bool scan1_stmt_match_case(const restrict Env env, const Stmt_Match stmt) {
  RET_NSPC(_scan1_stmt_match_case(env, stmt))
}

ANN static inline m_bool _scan1_stmt_match(const restrict Env env, const Stmt_Match stmt) {
  if(stmt->where)
    CHECK_BB(scan1_stmt(env, stmt->where))
  Stmt_List list = stmt->list;
  do CHECK_BB(scan1_stmt_match_case(env, &list->stmt->d.stmt_match))
  while((list = list->next));
  return GW_OK;
}

ANN static inline m_bool scan1_stmt_match(const restrict Env env, const Stmt_Match stmt) {
  CHECK_BB(scan1_exp(env, stmt->cond))
  RET_NSPC(_scan1_stmt_match(env, stmt))
}

#define describe_ret_nspc(name, type, prolog, exp) describe_stmt_func(scan1, name, type, prolog, exp)
describe_ret_nspc(flow, Stmt_Flow,, !(scan1_exp(env, stmt->cond) < 0 ||
    scan1_stmt(env, stmt->body) < 0) ? 1 : -1)
describe_ret_nspc(for, Stmt_For,, !(scan1_stmt(env, stmt->c1) < 0 ||
    scan1_stmt(env, stmt->c2) < 0 ||
    (stmt->c3 && scan1_exp(env, stmt->c3) < 0) ||
    scan1_stmt(env, stmt->body) < 0) ? 1 : -1)
describe_ret_nspc(auto, Stmt_Auto,, !(scan1_exp(env, stmt->exp) < 0 ||
    scan1_stmt(env, stmt->body) < 0) ? 1 : -1)
describe_ret_nspc(loop, Stmt_Loop,, !(scan1_exp(env, stmt->cond) < 0 ||
    scan1_stmt(env, stmt->body) < 0) ? 1 : -1)
describe_ret_nspc(if, Stmt_If,, !(scan1_exp(env, stmt->cond) < 0 ||
    scan1_stmt(env, stmt->if_body) < 0 ||
    (stmt->else_body && scan1_stmt(env, stmt->else_body) < 0)) ? 1 : -1)

ANN static inline m_bool scan1_stmt_code(const Env env, const Stmt_Code stmt) {
  if(stmt->stmt_list)
    { RET_NSPC(scan1_stmt_list(env, stmt->stmt_list)) }
  return GW_OK;
}

ANN static inline m_bool scan1_stmt_exp(const Env env, const Stmt_Exp stmt) {
  return stmt->val ? scan1_exp(env, stmt->val) : 1;
}

ANN m_bool scan1_enum_def(const Env env, const Enum_Def edef) {
  ID_List list = edef->list;
  do {
    CHECK_BB(already_defined(env, list->xid, edef->pos))
    const Value v = new_value(env->gwion->mp, edef->t, s_name(list->xid));
    valuefrom(env, v->from);
    if(env->class_def) {
      SET_FLAG(v, static);
      SET_ACCESS(edef, v)
    }
    SET_FLAG(v, const | ae_flag_enum | ae_flag_checked);
    nspc_add_value(edef->t->e->owner, list->xid, v);
    vector_add(&edef->values, (vtype)v);
  } while((list = list->next));
  return GW_OK;
}

ANN static m_bool scan1_args(const Env env, Arg_List list) {
  do {
    const Var_Decl var = list->var_decl;
    if(var->xid)
      CHECK_BB(isres(env, var->xid, var->pos))
    if(list->td)
      CHECK_OB((list->type = void_type(env, list->td)))
  } while((list = list->next));
  return GW_OK;
}

ANN m_bool scan1_fptr_def(const Env env, const Fptr_Def fptr) {
  if(tmpl_base(fptr->base->tmpl))
    return GW_OK;
  CHECK_OB((fptr->base->ret_type = known_type(env, fptr->base->td)))
  return fptr->base->args ? scan1_args(env, fptr->base->args) : GW_OK;
}

ANN m_bool scan1_type_def(const Env env, const Type_Def tdef) {
  if(!tdef->type->e->def)return GW_OK;
  return !is_fptr(env->gwion, tdef->type) ? scan1_cdef(env, tdef->type->e->def) : GW_OK;
}

ANN m_bool scan1_union_def_action(const Env env, const Union_Def udef,
    const Decl_List l) {
  const Exp_Decl decl = l->self->d.exp_decl;
  SET_FLAG(decl.td, checked | udef->flag);
  const m_bool global = GET_FLAG(udef, global);
  if(global)
    UNSET_FLAG(decl.td, global);
  if(GET_FLAG(udef, member))
    SET_FLAG(decl.td, member);
  else if(GET_FLAG(udef, static))
    SET_FLAG(decl.td, static);
  CHECK_BB(scan1_exp(env, l->self))
  if(global)
    SET_FLAG(decl.td, global);
  return GW_OK;
}

ANN m_bool scan1_union_def_inner(const Env env, const Union_Def udef) {
  Decl_List l = udef->l;
  do CHECK_BB(scan1_union_def_action(env, udef, l))
  while((l = l->next));
 return GW_OK;
}

ANN m_bool scan1_union_def(const Env env, const Union_Def udef) {
  if(tmpl_base(udef->tmpl))
    return GW_OK;
  const m_uint scope = union_push(env, udef);
  if(udef->xid || udef->type_xid) {
    UNSET_FLAG(udef, private);
    UNSET_FLAG(udef, protect);
  }
  const m_bool ret = scan1_union_def_inner(env, udef);
  union_pop(env, udef, scope);
  SET_FLAG(udef, scan1);
  return ret;
}

static const _exp_func stmt_func[] = {
  (_exp_func)scan1_stmt_exp,  (_exp_func)scan1_stmt_flow, (_exp_func)scan1_stmt_flow,
  (_exp_func)scan1_stmt_for,  (_exp_func)scan1_stmt_auto, (_exp_func)scan1_stmt_loop,
  (_exp_func)scan1_stmt_if,   (_exp_func)scan1_stmt_code, (_exp_func)dummy_func,
  (_exp_func)dummy_func,      (_exp_func)scan1_stmt_exp,  (_exp_func)scan1_stmt_match,
  (_exp_func)dummy_func
};

ANN static inline m_bool scan1_stmt(const Env env, const Stmt stmt) {
  return stmt_func[stmt->stmt_type](env, &stmt->d);
}

ANN static m_bool scan1_stmt_list(const Env env, Stmt_List l) {
  do {
    CHECK_BB(scan1_stmt(env, l->stmt))
    if(l->next) {
      if(l->stmt->stmt_type != ae_stmt_return) {
        if(l->next->stmt->stmt_type == ae_stmt_exp &&
          !l->next->stmt->d.stmt_exp.val) {
           Stmt_List next = l->next;
           l->next = l->next->next;
           next->next = NULL;
           free_stmt_list(env->gwion->mp, next);
        }
      } else {
        Stmt_List tmp = l->next;
        l->next = NULL;
        free_stmt_list(env->gwion->mp, tmp);
      }
    }
  } while((l = l->next));
  return GW_OK;
}

ANN m_bool scan1_fdef(const Env env, const Func_Def fdef) {
  if(fdef->base->td)
    CHECK_OB((fdef->base->ret_type = known_type(env, fdef->base->td)))
  if(fdef->base->args)
    CHECK_BB(scan1_args(env, fdef->base->args))
  if(!GET_FLAG(fdef, builtin) && fdef->d.code)
    CHECK_BB(scan1_stmt_code(env, &fdef->d.code->d.stmt_code))
  return GW_OK;
}

ANN m_bool scan1_func_def(const Env env, const Func_Def fdef) {
  if(fdef->base->td)
    CHECK_BB(env_storage(env, fdef->flag, td_pos(fdef->base->td)))
  if(tmpl_base(fdef->base->tmpl))
    return GW_OK;
  if(!env->class_def && (GET_FLAG(fdef, dtor) || fdef->base->xid == insert_symbol("@gack")))
    ERR_B(td_pos(fdef->base->td), _("'%s' must be in class def!!"), s_name(fdef->base->xid))
  if(GET_FLAG(fdef, op) && env->class_def)
    SET_FLAG(fdef, static);
  struct Func_ fake = { .name=s_name(fdef->base->xid) }, *const former = env->func;
  env->func = &fake;
  ++env->scope->depth;
  if(fdef->base->tmpl)
    CHECK_BB(template_push_types(env, fdef->base->tmpl))
  const m_bool ret = scan1_fdef(env, fdef);
  if(fdef->base->tmpl)
    nspc_pop_type(env->gwion->mp, env->curr);
  env->func = former;
  --env->scope->depth;
  return ret;
}

DECL_SECTION_FUNC(scan1)

ANN static m_bool scan1_parent(const Env env, const Class_Def cdef) {
  const loc_t pos = td_pos(cdef->base.ext);
  if(cdef->base.ext->array)
    CHECK_BB(scan1_exp(env, cdef->base.ext->array->exp))
  DECL_OB(const Type , parent,  = cdef->base.type->e->parent = known_type(env, cdef->base.ext))
  Type t = parent;
  do {
    if(cdef->base.type == t)
      ERR_B(pos, _("recursive (%s <= %s) class declaration."), cdef->base.type->name, t->name);
  } while((t = t->e->parent));
  if(isa(parent, env->gwion->type[et_object]) < 0)
    ERR_B(pos, _("cannot extend primitive type '%s'"), parent->name)
  if(parent->e->def && !GET_FLAG(parent, scan1))
    CHECK_BB(scanx_parent(parent, scan1_cdef, env))
  if(type_ref(parent))
    ERR_B(pos, _("can't use ref type in class extend"))
  if(GET_FLAG(parent, nonnull))
    ERR_B(pos, _("can't use nonnull type in class extend"))
  return GW_OK;
}

ANN m_bool scan1_class_def(const Env env, const Class_Def cdef) {
  if(tmpl_base(cdef->base.tmpl))
    return GW_OK;
  SET_FLAG(cdef->base.type, scan1);
  if(cdef->base.ext)
    CHECK_BB(scanx_parent(cdef->base.type, scan1_parent, env))
  if(cdef->body)
    CHECK_BB(env_body(env, cdef, scan1_section))
  SET_FLAG(cdef, scan1);
  return GW_OK;
}

ANN m_bool scan1_ast(const Env env, Ast ast) {
  do CHECK_BB(scan1_section(env, ast->section))
  while((ast = ast->next));
  return GW_OK;
}
