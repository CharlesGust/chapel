#ifndef _chplrt_H_
#define _chplrt_H_

#include "chpltypes.h"

#ifndef LAUNCHER

#include <stdint.h>
#include <comm_printf_macros.h>

extern chpl_fn_p chpl_ftable[];

extern int chpl_verbose_comm;     // set via startVerboseComm
extern int chpl_comm_diagnostics; // set via startCommDiagnostics
extern int chpl_verbose_mem;      // set via startVerboseMem

extern const char* chpl_compileCommand;
extern const char* chpl_compileVersion;
extern const char* CHPL_HOST_PLATFORM;
extern const char* CHPL_TARGET_PLATFORM;
extern const char* CHPL_HOST_COMPILER;
extern const char* CHPL_TARGET_COMPILER;
extern const char* CHPL_THREADS;
extern const char* CHPL_COMM;
extern char* chpl_executionCommand;

extern int chpl_threads_initialized;

#define CHPL_ASSIGN_SVEC(x, y)                     \
  memcpy(&x, &y, sizeof(x))

#define _CHECK_NIL(x, lineno, filename)                                 \
  do {                                                                  \
    if (x == nil)                                                       \
      chpl_error("attempt to dereference nil", lineno, filename);      \
  } while (0)

#define _ARRAY_GET(x, i) (&((x)->_data[i]))
#define _ARRAY_GET_VALUE(x, i) ((x)->_data[i])
#define _ARRAY_SET(x, i, v) ((x)->_data[i] = v)
#define _ARRAY_SET_SVEC(x, i, v) CHPL_ASSIGN_SVEC(((x)->_data[i]), v)

#define _ARRAY_ALLOC(x, type, size, lineno, filename) \
  (x)->_data = (size == 0) ? (void*)(0x0) : chpl_malloc(size, sizeof(type), CHPL_RT_MD_ARRAY_ELEMENTS, lineno, filename)

#define _WIDE_ARRAY_ALLOC(x, type, size, lineno, filename)              \
  do {                                                                 \
    if (x.locale != chpl_localeID)                                         \
      chpl_error("array vector data is not local", lineno, filename); \
    _ARRAY_ALLOC((x).addr, type, size, lineno, filename);               \
  } while (0)

#define _ARRAY_FREE_ELTS(x, i, call) \
  for(i = 0; i < (x)->size; i++) call

#define _ARRAY_FREE(x, lineno, filename) \
  chpl_free((x)->_data, lineno, filename)

#define _WIDE_ARRAY_FREE(x, lineno, filename)                          \
  do {                                                                 \
    if (x.locale != chpl_localeID)                                         \
      chpl_error("array vector data is not local", lineno, filename);  \
    _ARRAY_FREE((x).addr, lineno, filename);                           \
  } while (0)

#define CHPL_VMT_CALL(vmt, cid, ind)            \
  (vmt[cid][ind])

#define _noop(x)

#define malloc  dont_use_malloc_use_chpl_malloc_instead
#define calloc  dont_use_calloc_use_chpl_malloc_instead
#define free    dont_use_free_use_chpl_free_instead
#define realloc dont_use_realloc_use_chpl_realloc_instead
#define exit    dont_use_exit_use_chpl_exit_instead

#endif // LAUNCHER

#endif
