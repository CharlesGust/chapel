#ifndef _chpl_locale_model_h_
#define _chpl_locale_model_h_

#include "sys_basic.h"
#include "chpltypes.h"

//
// The flat locale model doesn't have sublocales.  It only has top
// level (network-connected) locales.
//

//
// The type of a global locale ID, and functions to assemble and
// disassemble global locale IDs.
//
// Note: these should be declared as in r21672, as bitfields.  But, they
// needed to be changed to regular 32-bit ints in order to work with a
// change in modules/internal/localeModels/flat/LocaleModel.chpl which
// solved a --baseline failure on 10-Aug-13 in arrays/bradc/arrayassign
// and a number of other tests.
//
typedef struct {
  int32_t node;
  int32_t subloc;
} chpl_localeID_t;

static ___always_inline
chpl_localeID_t chpl_rt_buildLocaleID(c_nodeid_t node, c_sublocid_t subloc) {
  chpl_localeID_t loc = { node, c_sublocid_any };
  //assert(subloc == c_sublocid_any);
  return loc;
}

static ___always_inline
c_nodeid_t chpl_rt_nodeFromLocaleID(chpl_localeID_t loc) {
  return loc.node;
}

static ___always_inline
c_sublocid_t chpl_rt_sublocFromLocaleID(chpl_localeID_t loc) {
  return c_sublocid_any;
}

//
// Force the tasking layer to say there are no sublocales even if it
// knows otherwise (NUMA, e.g.).
//
#define CHPL_LOCALE_MODEL_NUM_SUBLOCALES 0

#endif // _chpl_locale_model_h_
