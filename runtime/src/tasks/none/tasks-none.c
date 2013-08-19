#ifdef __OPTIMIZE__
// Turn assert() into a no op if the C compiler defines the macro above.
#define NDEBUG
#endif

#include "chplrt.h"
#include "chplcgfns.h"
#include "chpl-gen-includes.h"
#include "chplcast.h"
#include "chpl-locale-model.h"
#include "chpl-mem.h"
#include "chpl-tasks.h"
#include "error.h"
#include <assert.h>
#include <stdint.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>

//
// task pool: linked list of tasks
//
typedef struct chpl_pool_struct* chpl_task_pool_p;

typedef struct chpl_pool_struct {
  chpl_taskID_t            id;         // task identifier
  chpl_fn_p                fun;        // function to call for task
  void*                    arg;        // argument to the function
  chpl_task_private_data_t chpl_data;  // task private data
  chpl_task_pool_p next;
} task_pool_t;

static chpl_task_pool_p     task_pool_head; // head of task pool
static chpl_task_pool_p     task_pool_tail; // tail of task pool
static int                  queued_cnt;     // number of tasks in the task pool

static chpl_bool launch_next_task(void);


// Sync variables

void chpl_sync_lock(chpl_sync_aux_t *s) { }
void chpl_sync_unlock(chpl_sync_aux_t *s) { }

void chpl_sync_waitFullAndLock(chpl_sync_aux_t *s,
                                  int32_t lineno, chpl_string filename) {
  // while blocked, try running tasks from the task pool
  while (!*s && launch_next_task())
    /* do nothing! */;
  if (!*s)
    chpl_error("sync var empty (running in CHPL_TASKS=none mode)",
               lineno, filename);
}

void chpl_sync_waitEmptyAndLock(chpl_sync_aux_t *s,
                                   int32_t lineno, chpl_string filename) {
  // while blocked, try running tasks from the task pool
  while (*s && launch_next_task())
    /* do nothing! */;
  if (*s)
    chpl_error("sync var full (running in CHPL_TASKS=none mode)",
               lineno, filename);
}

void chpl_sync_markAndSignalFull(chpl_sync_aux_t *s) {
  *s = true;
}

void chpl_sync_markAndSignalEmpty(chpl_sync_aux_t *s) {
  *s = false;
}

chpl_bool chpl_sync_isFull(void *val_ptr, chpl_sync_aux_t *s,
                            chpl_bool simple_sync_var) {
  return *s;
}

void chpl_sync_initAux(chpl_sync_aux_t *s) {
  *s = false;
}

void chpl_sync_destroyAux(chpl_sync_aux_t *s) { }


// Tasks

static chpl_taskID_t next_taskID = chpl_nullTaskID + 1;
static chpl_taskID_t curr_taskID;
static chpl_task_private_data_t s_chpl_data = { .serial_state = true,
                                                .localeID     = 0,
                                                .here         = NULL,
                                                .alloc        = chpl_malloc,
                                                .calloc       = chpl_calloc,
                                                .realloc      = chpl_realloc,
                                                .free         = chpl_free
                                              };
static uint64_t taskCallStackSize = 0;

void chpl_task_init(void) {
  {
    uint32_t lim;

    if ((lim = chpl_task_getenvNumThreadsPerLocale()) > 0
        && lim != 1)
      chpl_warning("setting CHPL_RT_NUM_THREADS_PER_LOCALE > 1 is ignored for "
                   "CHPL_TASKS=none", 0, NULL);
  }

  //
  // If a value was specified for the call stack size, use that (rounded
  // up to a whole number of pages) to set the system stack limit.  This
  // will in turn limit the stack for any task.
  //
  {
    size_t css;

    if ((css = chpl_task_getenvCallStackSize()) != 0) {
      uint64_t      pagesize = (uint64_t) sysconf(_SC_PAGESIZE);
      struct rlimit rlim;

      css = (css + pagesize - 1) & ~(pagesize - 1);

      if (getrlimit(RLIMIT_STACK, &rlim) != 0)
        chpl_internal_error("getrlimit() failed");

      if (rlim.rlim_max != RLIM_INFINITY && css > rlim.rlim_max) {
        char warning[128];
        sprintf(warning, "call stack size capped at %lu\n", 
                (unsigned long)rlim.rlim_max);
        chpl_warning(warning, 0, NULL);

        css = rlim.rlim_max;
      }

      rlim.rlim_cur = css;

      if (setrlimit(RLIMIT_STACK, &rlim) != 0)
        chpl_internal_error("setrlimit() failed");
    }
    taskCallStackSize = css;
  }

  curr_taskID = next_taskID++;

  task_pool_head = task_pool_tail = NULL;
  queued_cnt = 0;
}

void chpl_task_exit(void) { }

int chpl_task_createCommTask(chpl_fn_p fn, void* arg) {
  pthread_t thread;
  return pthread_create(&thread, NULL, (void* (*)(void*)) fn, arg);
}

void chpl_task_callMain(void (*chpl_main)(void)) {
  s_chpl_data.serial_state = false;
  chpl_main();
}

void chpl_task_addToTaskList(chpl_fn_int_t fid,
                             void* arg,
                             c_sublocid_t subLoc,
                             chpl_task_list_p *task_list,
                             int32_t task_list_locale,
                             chpl_bool is_begin_stmt,
                             int lineno,
                             chpl_string filename) {
  assert(subLoc == 0
         || subLoc == c_sublocid_any
         || subLoc == c_sublocid_curr);

  if (s_chpl_data.serial_state) {
    //
    // We're serial, so this doesn't create a new task in the Chapel
    // sense.  Just invoke the body of the construct.
    //
    chpl_ftable_call(fid, arg);
  } else {
    // create a task from the given function pointer and arguments
    // and append it to the end of the task pool for later execution
    chpl_task_pool_p task;

    task = (chpl_task_pool_p)chpl_mem_alloc(sizeof(task_pool_t),
                                            CHPL_RT_MD_TASK_DESCRIPTOR,
                                            0, 0);
    task->id = next_taskID++;
    task->fun = chpl_ftable[fid];
    task->arg = arg;
    task->chpl_data = s_chpl_data;
    task->next = NULL;

    if (task_pool_tail) {
      task_pool_tail->next = task;
    } else {
      task_pool_head = task;
    }
    task_pool_tail = task;

    queued_cnt++;
  }
}

void chpl_task_processTaskList(chpl_task_list_p task_list) { }

void chpl_task_executeTasksInList(chpl_task_list_p task_list) { }

void chpl_task_freeTaskList(chpl_task_list_p task_list) { }

void chpl_task_startMovedTask(chpl_fn_p fp,
                              void* a,
                              c_sublocid_t subLoc,
                              chpl_taskID_t id,
                              chpl_bool serial_state) {
  // create a task from the given function pointer and arguments
  // and append it to the end of the task pool for later execution
  chpl_task_pool_p task;

  assert(subLoc == 0 || subLoc == c_sublocid_any);
  assert(id == chpl_nullTaskID);

  task = (chpl_task_pool_p)chpl_mem_alloc(sizeof(task_pool_t),
                                          CHPL_RT_MD_TASK_DESCRIPTOR,
                                          0, 0);
  task->id = next_taskID++;
  task->fun = fp;
  task->arg = a;
  task->chpl_data = s_chpl_data;
  task->chpl_data.serial_state = serial_state;
  task->next = NULL;

  if (task_pool_tail) {
    task_pool_tail->next = task;
  } else {
    task_pool_head = task;
  }
  task_pool_tail = task;

  queued_cnt++;
}

c_sublocid_t chpl_task_getSubLoc(void) { return 0; }

void chpl_task_setSubLoc(c_sublocid_t subLoc) {
  assert(subLoc == 0
         || subLoc == c_sublocid_any
         || subLoc == c_sublocid_curr);
}

chpl_taskID_t chpl_task_getId(void) { return curr_taskID; }

void chpl_task_yield(void) {
}

void chpl_task_sleep(int secs) {
  sleep(secs);
}

chpl_task_private_data_t* chpl_task_getPrivateData(void) {
  return &s_chpl_data;
}

chpl_bool chpl_task_getSerial(void) { return s_chpl_data.serial_state; }

void chpl_task_setSerial(chpl_bool new_state) {
  s_chpl_data.serial_state = new_state;
}

void* chpl_task_getHere(void) { return s_chpl_data.here; }

void chpl_task_setHere(void* new_here) { s_chpl_data.here = new_here; }

c_localeid_t chpl_task_getLocaleID(void) { return s_chpl_data.localeID; }

void chpl_task_setLocaleID(c_localeid_t new_localeID) {
  s_chpl_data.localeID = new_localeID;
}

c_sublocid_t chpl_task_getNumSubLocales(void) {
#ifdef CHPL_LOCALE_MODEL_NUM_SUBLOCALES
  return CHPL_LOCALE_MODEL_NUM_SUBLOCALES;
#else
  return 0;
#endif
}

uint64_t chpl_task_getCallStackSize(void) {
  return taskCallStackSize;
}


uint32_t chpl_task_getNumQueuedTasks(void) { return queued_cnt; }

uint32_t chpl_task_getNumRunningTasks(void) { return 1; }

int32_t  chpl_task_getNumBlockedTasks(void) { return 0; }


// Internal utility functions for task management

static chpl_bool
launch_next_task(void) {
  chpl_taskID_t saved_taskID;
  chpl_task_private_data_t saved_chpl_data;

  if (task_pool_head) {
    // retrieve the first task from the task pool
    chpl_task_pool_p task = task_pool_head;
    task_pool_head = task_pool_head->next;
    if (task_pool_head == NULL)  // task pool is now empty
      task_pool_tail = NULL;

    assert(queued_cnt > 0);
    queued_cnt--;

    //
    // save old state and call new task body in new state
    //
    saved_taskID = curr_taskID;
    saved_chpl_data = s_chpl_data;
    s_chpl_data = task->chpl_data;

    (*task->fun)(task->arg);
    chpl_mem_free(task, 0, 0);

    //
    // restore old state
    //
    s_chpl_data = saved_chpl_data;
    curr_taskID = saved_taskID;

    return true;
  } else {
    return false;  // task pool was empty!
  }
}


// Threads

uint32_t chpl_task_getNumThreads(void) { return 1; }

uint32_t chpl_task_getNumIdleThreads(void) { return 0; }
