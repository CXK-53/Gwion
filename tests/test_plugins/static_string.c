#include <stdlib.h>
#include "type.h"
#include "import.h"
#include "object.h"

IMPORT {
  CHECK_BB(gwi_item_ini(gwi, "string", "self"))
  M_Object obj = new_String(NULL, "test static string");
  CHECK_BB(gwi_item_end(gwi, ae_flag_global, obj))
  return 1;
}
