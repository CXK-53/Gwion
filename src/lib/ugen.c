#include <stdlib.h>
#include "defs.h"
#include "map.h"
#include "absyn.h"
#include "err_msg.h"
#include "oo.h"
#include "vm.h"
#include "env.h"
#include "type.h"
#include "instr.h"
#include "object.h"
#include "import.h"
#include "ugen.h"
#include "mpool.h"

ANN static inline void ugop_add   (const UGen u, const m_float f) { u->in += f; }
ANN static inline void ugop_sub  (const UGen u, const m_float f) { u->in -= f; }
ANN static inline void ugop_mul  (const UGen u, const m_float f) { u->in *= f; }
ANN static inline void ugop_div (const UGen u, const m_float f) { u->in /= f; }

static TICK(dac_tick) {
  m_float* out = ((VM*)u->module.gen.data)->bbq->out;
  m_uint i = 0;
  do out[i] = UGEN(u->connect.multi->channel[i])->in;
  while(++i < u->connect.multi->n_out);
}

static TICK(adc_tick) {
  const m_float* in = ((VM*)u->module.gen.data)->bbq->in;
  m_uint i = 0;
  do UGEN(u->connect.multi->channel[i])->out = in[i];
  while(++i < u->connect.multi->n_out);
}

#define COMPUTE(a) if(!(a)->done)(a)->compute((a));

__attribute__((hot))
ANN void compute_mono(const UGen u) {
  u->done = 1;
  const m_uint size = u->connect.net.size;
  if(size) {
    const Vector vec = &u->connect.net.from;
    const UGen   v   = (UGen)vector_front(vec);
    COMPUTE(v)
    u->in = v->out;
    for(m_uint i = 1; i < size; ++i) {
      const UGen w = (UGen)vector_at(vec, i);
      COMPUTE(w)
      u->op(u, w->out);
    }
  }
}

__attribute__((hot))
ANN void compute_multi(const UGen u) {
  u->done = 1;
  m_uint i = 0;
  do compute_mono(UGEN(u->connect.multi->channel[i]));
  while(++i < u->connect.multi->n_chan);
}

__attribute__((hot))
ANN void compute_chan(const UGen u) {
  COMPUTE(u->module.ref);
}

#define describe_compute(func, opt, aux)         \
__attribute__((hot))                             \
ANN void gen_compute_##func##opt(const UGen u) { \
  compute_##func(u);                             \
  aux                                            \
  u->module.gen.tick(u);                                \
}
describe_compute(mono,,)
describe_compute(multi,,)
describe_compute(mono,  trig, {u->module.gen.trig->compute(u->module.gen.trig);})
describe_compute(multi, trig, {u->module.gen.trig->compute(u->module.gen.trig);})

ANEW static UGen new_UGen() {
  const UGen u = mp_alloc(UGen);
  u->op = ugop_add;
  u->compute = gen_compute_mono;
  return u;
}

ANEW /*static */M_Object new_M_UGen() {
  const M_Object o = new_object(NULL, t_ugen);
  UGEN(o) = new_UGen();
  return o;
}

ANN static void assign_channel(const UGen u) {
  SET_FLAG(u, UGEN_MULTI);
  u->compute = gen_compute_multi;
  u->connect.multi->channel = (M_Object*)xmalloc(u->connect.multi->n_chan * SZ_INT);
  for(m_uint i = u->connect.multi->n_chan + 1; --i;) {
    const m_uint j = i - 1;
    const M_Object chan = new_M_UGen();
    ugen_ini(UGEN(chan), u->connect.multi->n_in > j, u->connect.multi->n_out > j);
    UGEN(chan)->module.ref = u;
    UGEN(chan)->compute = compute_chan;
    u->connect.multi->channel[j] =  chan;
  }
}

ANN void ugen_gen(const UGen u, const f_tick tick, void* data, const m_bool trig) {
  u->module.gen.tick = tick;
  u->module.gen.data = data;
  if(trig) {
    u->module.gen.trig = new_UGen();
    u->module.gen.trig->compute = compute_mono;
    ugen_ini(u->module.gen.trig, 1, 1);
    u->compute = (u->compute == gen_compute_mono ? gen_compute_monotrig : gen_compute_multitrig);
  }
}

ANN void ugen_ini(const UGen u, const m_uint in, const m_uint out) {
  const m_uint chan = in > out ? in : out;
  if(chan == 1) {
    vector_init(&u->connect.net.from);
    vector_init(&u->connect.net.to);
  } else {
    u->connect.multi = mp_alloc(ugen_multi);
    u->connect.multi->n_in   = in;
    u->connect.multi->n_out  = out;
    u->connect.multi->n_chan = chan;
    assign_channel(u);
  }
}

ANN static m_bool connect_init(const VM_Shred shred, restrict M_Object* lhs, restrict M_Object* rhs) {
  POP_REG(shred, SZ_INT * 2);
  *rhs = *(M_Object*)REG(SZ_INT);
  *lhs = *(M_Object*)REG(0);
  if(!*lhs) {
    if(*rhs)
      _release(*rhs, shred);
    goto end;
  }
  if(!*rhs) {
    _release(*lhs, shred);
    goto end;
  }
  return 1;
end:
  exception(shred, "UgenConnectException");
  return -1;
}

#define describe_connect(name, func)                                   \
ANN static inline void name##onnect(const restrict UGen lhs, const restrict UGen rhs) { \
  func(&rhs->connect.net.from, (vtype)lhs);                            \
  func(&lhs->connect.net.to,   (vtype)rhs);                            \
  rhs->connect.net.size = vector_size(&rhs->connect.net.from);         \
}
describe_connect(C,vector_add)
describe_connect(Disc,vector_rem2)

ANN static void release_connect(const VM_Shred shred) {
  _release(*(M_Object*)REG(0), shred);
  _release(*(M_Object*)REG(SZ_INT), shred);
  *(M_Object*)REG(0) = *(M_Object*)REG(SZ_INT);
  PUSH_REG(shred, SZ_INT);
}

typedef ANN void (*f_connect)(const UGen lhs, const UGen rhs);
ANN /* static */ void _do_(const f_connect f, const UGen lhs, const UGen rhs) {
  const m_bool l_multi = GET_FLAG(lhs, UGEN_MULTI);
  const m_bool r_multi = GET_FLAG(rhs, UGEN_MULTI);
  const m_uint l_max = l_multi ? lhs->connect.multi->n_out : 1;
  const m_uint r_max = r_multi ? rhs->connect.multi->n_in  : 1;
  const m_uint max   = l_max > r_max ? l_max : r_max;
  m_uint i = 0;
  do {
    const UGen l = l_multi ? UGEN(lhs->connect.multi->channel[i % l_max]) : lhs;
    const UGen r = r_multi ? UGEN(rhs->connect.multi->channel[i % r_max]) : rhs;
    f(l, r);
  } while(++i < max);
}

ANN void ugen_connect(const restrict UGen lhs, const restrict UGen rhs) {
  _do_(Connect, lhs, rhs);
}

ANN void ugen_disconnect(const restrict UGen lhs, const restrict UGen rhs) {
  _do_(Disconnect, lhs, rhs);
}

#define TRIG_EX                         \
if(!UGEN(rhs)->module.gen.trig) {       \
  release_connect(shred);               \
  Except(shred, "NonTriggerException"); \
}
#define describe_connect_instr(name, func, opt) \
static INSTR(name##func) { GWDEBUG_EXE       \
  M_Object lhs, rhs;                            \
  if(connect_init(shred, &lhs, &rhs) > 0) {     \
    opt                                         \
    _do_(func, UGEN(lhs), UGEN(rhs)); /*}*/         \
  release_connect(shred);      }                 \
}
describe_connect_instr(Ugen, Connect,)
describe_connect_instr(Ugen, Disconnect,)
describe_connect_instr(Trig, Connect,    TRIG_EX)
describe_connect_instr(Trig, Disconnect, TRIG_EX)

static CTOR(ugen_ctor) {
  UGEN(o) = new_UGen();
  vector_add(&shred->vm->ugen, (vtype)UGEN(o));
}

#define describe_release_func(src, tgt, opt)                 \
ANN static void release_##src(const UGen ug) {               \
  for(m_uint i = vector_size(&ug->connect.net.src) + 1; --i;) {      \
    const UGen u = (UGen)vector_at(&ug->connect.net.src, i - 1);     \
    vector_rem2(&u->connect.net.tgt, (vtype)ug);                     \
    opt                                                      \
  }                                                          \
  vector_release(&ug->connect.net.src);                              \
}
describe_release_func(from, to,)
describe_release_func(to, from, --u->connect.net.size;)

ANN static void release_mono(const UGen ug) {
  release_to(ug);
  release_from(ug);
}

ANN static void release_multi(const UGen ug, const VM_Shred shred) {
  for(m_uint i = ug->connect.multi->n_chan + 1; --i;)
    release(ug->connect.multi->channel[i - 1], shred);
  xfree(ug->connect.multi->channel);
}

static DTOR(ugen_dtor) {
  const UGen ug = UGEN(o);
  vector_rem2(&shred->vm->ugen, (vtype)ug);
  if(!GET_FLAG(ug, UGEN_MULTI))
    release_mono(ug);
  else
    release_multi(ug, shred);
  if(ug->module.gen.trig) {
    release_mono(ug->module.gen.trig);
    mp_free(UGen, ug->module.gen.trig);
  }
  mp_free(UGen, ug);
}

static MFUN(ugen_channel) {
  const m_int i = *(m_int*)MEM(SZ_INT);
  if(!GET_FLAG(UGEN(o), UGEN_MULTI))
    *(M_Object*)RETURN = !i ? o : NULL;
  else if(i < 0 || (m_uint)i >= UGEN(o)->connect.multi->n_chan)
    *(M_Object*)RETURN = NULL;
  else
    *(M_Object*)RETURN = UGEN(o)->connect.multi->channel[i];
}

static MFUN(ugen_get_op) {
  const f_ugop f = UGEN(o)->op;
  if(f == ugop_add)
    *(m_uint*)RETURN = 1;
  else if(f == ugop_sub)
    *(m_uint*)RETURN = 2;
  else if(f == ugop_mul)
    *(m_uint*)RETURN = 3;
  else if(f == ugop_div)
    *(m_uint*)RETURN = 4;
}
ANN static void set_op(const UGen u, const f_ugop f) {
  if(GET_FLAG(u, UGEN_MULTI)) {
    for(m_uint i = u->connect.multi->n_chan + 1; --i;)
      UGEN(u->connect.multi->channel[i-1])->op = f;
  }
  u->op = f;
}

static MFUN(ugen_set_op) {
  const m_int i = *(m_int*)MEM(SZ_INT);
  if(i == 1)
    set_op(UGEN(o), ugop_add);
  else if(i == 2)
    set_op(UGEN(o), ugop_sub);
  else if(i == 3)
    set_op(UGEN(o), ugop_mul);
  else if(i == 4)
    set_op(UGEN(o), ugop_div);
  *(m_int*)RETURN = i;
}

static MFUN(ugen_get_last) {
  *(m_float*)RETURN = UGEN(o)->out;
}

static GWION_IMPORT(global_ugens) {
  VM* vm = gwi_vm(gwi);

  vm->dac       = new_M_UGen();
  ugen_ini(UGEN(vm->dac), vm->bbq->nchan, vm->bbq->nchan);
  ugen_gen(UGEN(vm->dac), dac_tick, vm, 0);
  vector_add(&vm->ugen, (vtype)UGEN(vm->dac));
  gwi_item_ini(gwi, "UGen", "dac");
  gwi_item_end(gwi, ae_flag_const, vm->dac);

  vm->blackhole = new_M_UGen();
  ugen_ini(UGEN(vm->blackhole), 1, 1);
  vector_add(&vm->ugen, (vtype)UGEN(vm->blackhole));
  gwi_item_ini(gwi, "UGen", "blackhole");
  gwi_item_end(gwi, ae_flag_const, vm->blackhole);
  UGEN(vm->blackhole)->compute = compute_mono;


  M_Object adc       = new_M_UGen();
  ugen_ini(UGEN(adc), vm->bbq->n_in, vm->bbq->n_in);
  ugen_gen(UGEN(adc), adc_tick, vm, 0);
  vector_add(&vm->ugen, (vtype)UGEN(adc));
  gwi_item_ini(gwi, "UGen", "adc");
  gwi_item_end(gwi, ae_flag_const, adc);
  SET_FLAG(t_ugen, ae_flag_abstract);
  return 1;
}

static OP_CHECK(opck_chuck_ugen) {
  const Exp_Binary* bin = (Exp_Binary*)data;
  return bin->rhs->type;
}

GWION_IMPORT(ugen) {
  t_ugen = gwi_mk_type(gwi, "UGen", SZ_INT, t_object);
  CHECK_BB(gwi_class_ini(gwi,  t_ugen, ugen_ctor, ugen_dtor))
  CHECK_BB(gwi_item_ini(gwi, "int", "@ugen"))
  CHECK_BB(gwi_item_end(gwi, ae_flag_member, NULL))

  CHECK_BB(gwi_func_ini(gwi, "UGen", "chan", ugen_channel))
  CHECK_BB(gwi_func_arg(gwi, "int", "arg0"))
  CHECK_BB(gwi_func_end(gwi, 0))

  CHECK_BB(gwi_func_ini(gwi, "int", "op", ugen_get_op))
  CHECK_BB(gwi_func_end(gwi, 0))

  CHECK_BB(gwi_func_ini(gwi, "int", "op", ugen_set_op))
  CHECK_BB(gwi_func_arg(gwi, "int", "arg0"))
  CHECK_BB(gwi_func_end(gwi, 0))

  CHECK_BB(gwi_func_ini(gwi, "float", "last", ugen_get_last))
  CHECK_BB(gwi_func_end(gwi, 0))
  CHECK_BB(gwi_class_end(gwi))

  CHECK_BB(gwi_oper_ini(gwi, "UGen", "UGen", "UGen"))
  _CHECK_OP(chuck, chuck_ugen, UgenConnect)
  _CHECK_OP(unchuck, chuck_ugen, UgenDisconnect)
  _CHECK_OP(trig, chuck_ugen, TrigConnect)
  _CHECK_OP(untrig, chuck_ugen, TrigDisconnect)
  CHECK_BB(import_global_ugens(gwi))
  return 1;
}
