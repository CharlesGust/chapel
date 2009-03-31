#include <stdio.h>
#include <stdlib.h>
#include "chplcomm.h"
#include "chplexit.h"
#include "chplmem.h"
#include "chplmemtrack.h"
#include "chplrt.h"
#include "gdb.h"

#undef exit

static void _chpl_exit_common(int status, int all) {
  fflush(stdout);
  fflush(stderr);
  if (status != 0) {
    gdbShouldBreakHere();
  }
  if (all) {
    chpl_comm_barrier("chpl_comm_exit_all");
    exitChplThreads();         // tear down the threads
    chpl_comm_exit_all(status);
  } else {
    chpl_comm_exit_any(status);
  }
  exit(status);
}


void _chpl_exit_all(int status) {
  printFinalMemStat(0, 0);   // print the final memory statistics
  _chpl_exit_common(status, 1);
}


void _chpl_exit_any(int status) {
  _chpl_exit_common(status, 0);
}
