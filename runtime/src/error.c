#include "error.h"
#include "chplexit.h"
#include "chplrt.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

int verbosity = 1;

void chpl_warning(const char* message, int32_t lineno, chpl_string filename) {
  // squash warnings if --quiet flag is used
  if (verbosity == 0) {
    return;
  }
  fflush(stdout);
  if (lineno > 0)
    fprintf(stderr, "%s:%" PRId32 ": warning: %s\n", filename, lineno, message);
  else if (filename)
    fprintf(stderr, "%s: warning: %s\n", filename, message);
  else
    fprintf(stderr, "warning: %s\n", message);
}

void chpl_stm_error(int32_t lineno, chpl_string filename) {
  fflush(stdout);
  chpl_exit_any(1);
}

void chpl_error(const char* message, int32_t lineno, chpl_string filename) {
  fflush(stdout);
  if (lineno > 0)
    fprintf(stderr, "%s:%" PRId32 ": error: %s\n", filename, lineno, message);
  else if (filename)
    fprintf(stderr, "%s: error: %s\n", filename, message);
  else
    fprintf(stderr, "error: %s\n", message);
  chpl_exit_any(1);
}

void chpl_internal_error(const char* message) {
  fflush(stdout);
  fprintf(stderr, "internal error: %s\n", message);
  chpl_exit_any(2);
}


void chpl_msg(int verbose_level, const char* fmt, ...) {
  if (verbosity >= verbose_level) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fflush(stdout);
  }
}
