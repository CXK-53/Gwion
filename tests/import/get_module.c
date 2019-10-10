#include <stdlib.h>
#include <unistd.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "env.h"
#include "vm.h"
#include "type.h"
#include "value.h"
#include "object.h"
#include "driver.h"
#include "gwion.h"
#include "instr.h"
#include "operator.h"
#include "import.h"
#include "gwi.h"
#include "plug.h"

GWMODSTR(dummy_module);

GWMODINI(dummy_module) {
  puts(__func__);
  return NULL;
}
GWMODEND(dummy_module) {
  puts(__func__);
}

GWION_IMPORT(dummy_module) {
  GWI_OB(get_module(gwi->gwion, "dummy_module"))
  puts("test passed!");
  get_module(gwi->gwion, "non_existant_module");
  return GW_OK;
}
