// chpl-gen-includes.h
//
// Inline functions used in code generation.
//
// TODO: Check if any of these are redundant with functions in chpl-wide-ptr-fns.h.
//

#include "chpl-comm-compiler-macros.h"
#include "chplcgfns.h"
#include "chpl-tasks.h"
#include "chpltypes.h"

//
// Call a function in the compiler-produced function table, passing it
// one argument.
//
static ___always_inline
void chpl_ftable_call(chpl_fn_int_t fid, void* arg)
{
  (*chpl_ftable[fid])(arg);
}


// used for converting between the Chapel idea of a locale ID: chpl_localeID_t
// and the runtime idea of a locale ID: c_localeid_t.
static ___always_inline
c_localeid_t id_pub2rt(chpl_localeID_t s)
{
  return
    ((c_localeid_t) s.node << 32) | ((c_localeid_t) s.subloc & 0xffffffff);
}

static ___always_inline
chpl_localeID_t id_rt2pub(c_localeid_t i)
{
  return (chpl_localeID_t) { .node = i >> 32, .subloc = i & 0xffffffff };
}

static ___always_inline
chpl_localeID_t chpl_gen_getLocaleID(void)
{
  return id_rt2pub(chpl_task_getLocaleID());
}

static ___always_inline
void chpl_gen_setLocaleID(chpl_localeID_t locale)
{
  chpl_task_setLocaleID(id_pub2rt(locale));
}

static ___always_inline
chpl_bool chpl_is_here(chpl_localeID_t locale)
{
  return id_pub2rt(locale) == chpl_task_getLocaleID();
}
