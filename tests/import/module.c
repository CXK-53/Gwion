#include <stdlib.h>
#include <unistd.h>
#include "gwion_util.h"
#include "gwion_ast.h"
#include "oo.h"
#include "env.h"
#include "vm.h"
#include "driver.h"
#include "gwion.h"
#include "plug.h"

GWMODSTR(dummy_module);

GWMODINI(dummy_module) {
  puts(__func__);
  return NULL;
}
GWMODEND(dummy_module) {
  puts(__func__);
}
