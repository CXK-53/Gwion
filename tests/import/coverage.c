#include "complex.h"
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "vm.h"
#include "env.h"
#include "type.h"
#include "object.h"
#include "instr.h"
#include "gwion.h"
#include "operator.h"
#include "import.h"
#include "gwi.h"

SFUN(coverage_int)     { *(m_uint*)RETURN    = 0; }
SFUN(coverage_float)   { *(m_float*)RETURN   = 0; }
SFUN(coverage_complex) { *(m_complex*)RETURN = 0; }
SFUN(coverage_vec3)    { m_vec3 v = {0,0,0};   *(m_vec3*)RETURN = v; }
SFUN(coverage_vec4)    { m_vec4 v = {0,0,0,0}; *(m_vec4*)RETURN = v; }

GWION_IMPORT(coverage) {
  Type t_coverage;
  CHECK_OB((t_coverage = gwi_mk_type(gwi, "Coverage", SZ_INT, "Object")))
  CHECK_BB(gwi_class_ini(gwi, t_coverage, NULL, NULL))
  CHECK_BB(gwi_func_ini(gwi, "int", "i", coverage_int))
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))
  CHECK_BB(gwi_func_ini(gwi, "float", "f", coverage_float))
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))
  CHECK_BB(gwi_func_ini(gwi, "complex", "c", coverage_complex))
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))
  CHECK_BB(gwi_func_ini(gwi, "Vec3", "v", coverage_vec3))
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))
  CHECK_BB(gwi_func_ini(gwi, "Vec4", "w", coverage_vec4))
  CHECK_BB(gwi_func_end(gwi, ae_flag_static))

  ALLOC_PTR(gwi->gwion->mp, i, m_uint, 5);
  CHECK_BB(gwi_item_ini(gwi,"int", "s_i"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static, i))

  ALLOC_PTR(gwi->gwion->mp, f, m_float, 2.1);
  CHECK_BB(gwi_item_ini(gwi,"int", "s_f"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static, (void*)f))

  const m_complex _c = 2.1 + 2.2*I;
  ALLOC_PTR(gwi->gwion->mp, c, m_complex, _c);
  CHECK_BB(gwi_item_ini(gwi,"complex", "s_c"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static, (void*)c))

  const m_vec3 _v = { 2.1, 2.2, 2.3 };
  ALLOC_PTR(gwi->gwion->mp, v, m_vec3, _v);
  CHECK_BB(gwi_item_ini(gwi,"Vec3", "s_v"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static, (void*)v))

  const m_vec4 _w = { 2.1, 2.2, 2.3, 2.4 };
  ALLOC_PTR(gwi->gwion->mp, w, m_vec4, _w);
  CHECK_BB(gwi_item_ini(gwi,"Vec4", "s_w"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static, (void*)w))

  ALLOC_PTR(gwi->gwion->mp, ci, m_uint, 5);
  CHECK_BB(gwi_item_ini(gwi,"int", "sc_i"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static | ae_flag_const, ci))

  ALLOC_PTR(gwi->gwion->mp, cf, m_float, 2.1);
  CHECK_BB(gwi_item_ini(gwi,"float", "sc_f"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static | ae_flag_const, (void*)cf))

  const m_complex _cc = 2.1 + 2.2*I;
  ALLOC_PTR(gwi->gwion->mp, cc, m_complex, _cc);
  CHECK_BB(gwi_item_ini(gwi,"complex", "sc_c"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static | ae_flag_const, (void*)cc))

  const m_vec3 _cv = { 2.1, 2.2, 2.3 };
  ALLOC_PTR(gwi->gwion->mp, cv, m_vec3, _cv);
  CHECK_BB(gwi_item_ini(gwi,"Vec3", "sc_v"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static | ae_flag_const, (void*)cv))

  const m_vec4 _cw = { 2.1, 2.2, 2.3, 2.4 };
  ALLOC_PTR(gwi->gwion->mp, cw, m_vec4, _cw);
  CHECK_BB(gwi_item_ini(gwi,"Vec4", "sc_w"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_static | ae_flag_const, (void*)cw))

  CHECK_BB(gwi_class_end(gwi))
  return GW_OK;
}
