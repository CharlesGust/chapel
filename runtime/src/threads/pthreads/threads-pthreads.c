//
// Pthread implementation of Chapel threading interface
//

#ifdef __OPTIMIZE__
// Turn assert() into a no op if the C compiler defines the macro above.
#define NDEBUG
#endif

#include "chpl_mem.h"
#include "chplcast.h"
#include "chplrt.h"
#include "chpltasks.h"
#include "config.h"
#include "error.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
  void* (*fn)(void*);
  void* arg;
} thread_func_t;


//
// Types and variables for our list of threads.
//
typedef struct thread_list* thread_list_p;
struct thread_list {
  pthread_t     thread;
  thread_list_p next;
};

static pthread_mutex_t thread_list_lock;        // mutual exclusion lock
static thread_list_p   thread_list_head = NULL; // head of thread_list
static thread_list_p   thread_list_tail = NULL; // tail of thread_list

static pthread_attr_t thread_attributes;

static pthread_key_t  thread_private_key;

static int32_t         threadMaxThreadsPerLocale  = -1;
static uint32_t        threadNumThreads           =  1;  // count main thread
static pthread_mutex_t threadNumThreadsLock;

static uint64_t threadCallStackSize = 0;


static void*           initial_pthread_func(void*);
static void            destroy_thread_private_data(void*);
static void*           pthread_func(void*);


// Mutexes

void threadlayer_mutex_init(threadlayer_mutex_p mutex) {
  // WAW: how to explicitly specify blocking-type?
  if (pthread_mutex_init((pthread_mutex_t*) mutex, NULL))
    chpl_internal_error("pthread_mutex_init() failed");
}

threadlayer_mutex_p threadlayer_mutex_new(void) {
  threadlayer_mutex_p m;
  m = (threadlayer_mutex_p) chpl_alloc(sizeof(threadlayer_mutex_t),
                                       CHPL_RT_MD_MUTEX, 0, 0);
  threadlayer_mutex_init(m);
  return m;
}

void threadlayer_mutex_lock(threadlayer_mutex_p mutex) {
  if (pthread_mutex_lock((pthread_mutex_t*) mutex))
    chpl_internal_error("pthread_mutex_lock() failed");
}

void threadlayer_mutex_unlock(threadlayer_mutex_p mutex) {
  if (pthread_mutex_unlock((pthread_mutex_t*) mutex))
    chpl_internal_error("pthread_mutex_unlock() failed");
}


// Thread id

threadlayer_threadID_t threadlayer_thread_id(void) {
  return (threadlayer_threadID_t) pthread_self();
}


// Thread yield

void threadlayer_yield(void) {
  int last_cancel_state;

  // check for cancellation, or we won't be able to terminate
  (void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &last_cancel_state);
  pthread_testcancel();
  (void) pthread_setcancelstate(last_cancel_state, NULL);

  sched_yield();
}


// Thread callbacks for the FIFO tasking layer

void threadlayer_init(int32_t maxThreadsPerLocale, uint64_t callStackSize) {
  //
  // Tuck maxThreadsPerLocale away in a static global for use by other routines
  //
  threadMaxThreadsPerLocale = maxThreadsPerLocale;

  //
  // If a value was specified for the call stack size config const, use
  // that (rounded up to a whole number of pages) to set the system and
  // pthread stack limits.
  //
  if (pthread_attr_init(&thread_attributes) != 0)
    chpl_internal_error("pthread_attr_init() failed");

  //
  // If a value was specified for the call stack size config const, use
  // that (rounded up to a whole number of pages) to set the system
  // stack limit.
  //
  if (callStackSize != 0) {
    uint64_t      pagesize = (uint64_t) sysconf(_SC_PAGESIZE);
    struct rlimit rlim;

    callStackSize = (callStackSize + pagesize - 1) & ~(pagesize - 1);

    if (getrlimit(RLIMIT_STACK, &rlim) != 0)
      chpl_internal_error("getrlimit() failed");

    if (rlim.rlim_max != RLIM_INFINITY && callStackSize > rlim.rlim_max) {
      char warning[128];
      sprintf(warning, "callStackSize capped at %lu\n", 
              (unsigned long)rlim.rlim_max);
      chpl_warning(warning, 0, NULL);

      callStackSize = rlim.rlim_max;
    }

    rlim.rlim_cur = callStackSize;

    if (setrlimit(RLIMIT_STACK, &rlim) != 0)
      chpl_internal_error("setrlimit() failed");

    if (pthread_attr_setstacksize(&thread_attributes, callStackSize) != 0)
      chpl_internal_error("pthread_attr_setstacksize() failed");
  }
  threadCallStackSize = callStackSize;

  if (pthread_key_create(&thread_private_key, destroy_thread_private_data))
    chpl_internal_error("pthread_key_create failed");

  pthread_mutex_init(&thread_list_lock, NULL);
  pthread_mutex_init(&threadNumThreadsLock, NULL);

  //
  // This is something of a hack, but it makes us a bit more resilient
  // if we're out of memory or near to it at shutdown time.  Launch,
  // cancel, and join with an initial pthread, forcing initialization
  // needed by any of those activities.  (In particular we have found
  // that cancellation needs to dlopen(3) a shared object, which fails
  // if we are out of memory.  Doing it now means that shared object is
  // already available when we need it later.)
  //
  {
    pthread_t initial_pthread;

    if (!pthread_create(&initial_pthread, NULL, initial_pthread_func, NULL)) {
      (void) pthread_cancel(initial_pthread);
      (void) pthread_join(initial_pthread, NULL);
    }
  }
}

//
// The initial pthread just waits to be canceled.  See the comment in
// threadlayer_init() for the purpose of this.
//
static void* initial_pthread_func(void* ignore) {
  while (1) {
    pthread_testcancel();
    sched_yield();
  }
  return NULL;
}

static void destroy_thread_private_data(void* p) {
  if (p)
    chpl_free(p, 0, 0);
}

void threadlayer_exit(void) {
  chpl_bool debug = false;
  thread_list_p tlp;

  if (debug)
    fprintf(stderr, "A total of %u threads were created\n", threadNumThreads);

  // shut down all threads
  for (tlp = thread_list_head; tlp != NULL; tlp = tlp->next) {
    if (pthread_cancel(tlp->thread) != 0)
      chpl_internal_error("thread cancel failed");
  }
  while (thread_list_head != NULL) {
    if (pthread_join(thread_list_head->thread, NULL) != 0)
      chpl_internal_error("thread join failed");
    tlp = thread_list_head;
    thread_list_head = thread_list_head->next;
    chpl_free(tlp, 0, 0);
  }

  if (pthread_key_delete(thread_private_key) != 0)
    chpl_internal_error("pthread_key_delete() failed");

  if (pthread_attr_destroy(&thread_attributes) != 0)
    chpl_internal_error("pthread_attr_destroy() failed");
}

chpl_bool threadlayer_can_start_thread(void) {
  return (threadMaxThreadsPerLocale == 0 ||
          threadNumThreads < (uint32_t) threadMaxThreadsPerLocale);
}

int threadlayer_thread_create(threadlayer_threadID_t* thread,
                              void*(*fn)(void*), void* arg)
{
  //
  // An implementation note:
  //
  // It's important to keep the thread counter as accurate as possible,
  // because it's used to throttle thread creation so that we don't go
  // over the user's specified limit.  We could count the new thread
  // when it starts executing, in pthread_func().  But if the kernel
  // executed parent threads in preference to children, the resulting
  // delay in updating the counter could cause us to create many more
  // threads than the limit.  Or we could count them after creating
  // them, here.  But if grabbing the mutex that protects the counter
  // stalled the parent and led the kernel to schedule other threads,
  // updates to the counter could again be delayed and too many threads
  // created.  The solution adopted is to update the counter in the
  // parent before creating the new thread, and then decrement it if
  // thread creation fails.  The idea is that if the only thing that
  // separates the counter update from the thread creation is a mutex
  // unlock which won't cause the parent to be rescheduled, we maximize
  // the likelihood that everyone will see accurate counter values.
  //

  thread_func_t* f;
  pthread_t pthread;

  f = (thread_func_t*) chpl_alloc(sizeof(thread_func_t),
                                  CHPL_RT_MD_THREAD_CALLEE, 0, 0);
  f->fn  = fn;
  f->arg = arg;
  pthread_mutex_lock(&threadNumThreadsLock);
  threadNumThreads++;
  pthread_mutex_unlock(&threadNumThreadsLock);

  if (pthread_create(&pthread, &thread_attributes, pthread_func, f)) {
    pthread_mutex_lock(&threadNumThreadsLock);
    threadNumThreads--;
    pthread_mutex_unlock(&threadNumThreadsLock);

    return -1;
  }

  if (thread != NULL)
    *thread = (threadlayer_threadID_t) pthread;
  return 0;
}

static void* pthread_func(void* void_f) {
  thread_func_t* f = (thread_func_t*) void_f;
  void*          (*fn)(void*) = f->fn;
  void*          arg = f->arg;
  thread_list_p  tlp;

  chpl_free(f, 0, 0);

  // disable cancellation immediately
  // enable only while waiting for new work
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); 

  // add us to the list of threads
  tlp = (thread_list_p) chpl_alloc(sizeof(struct thread_list),
                                   CHPL_RT_MD_THREAD_LIST_DESCRIPTOR, 0, 0);

  tlp->thread = pthread_self();
  tlp->next   = NULL;

  pthread_mutex_lock(&thread_list_lock);
  if (thread_list_head == NULL)
    thread_list_head = tlp;
  else
    thread_list_tail->next = tlp;
  thread_list_tail = tlp;
  pthread_mutex_unlock(&thread_list_lock);

  return (*(fn))(arg);
}

void threadlayer_thread_destroy(void) {
  // for the sake of simplicity, we never destroy a thread
  return;
}

void* threadlayer_get_thread_private_data(void) {
  return pthread_getspecific(thread_private_key);
}

void threadlayer_set_thread_private_data(void* p) {
  if (pthread_setspecific(thread_private_key, p))
    chpl_internal_error("thread private data key doesn't work");
}

uint32_t threadlayer_get_max_threads(void) {
  return threadMaxThreadsPerLocale;
}

uint32_t threadlayer_get_num_threads(void) {
  return threadNumThreads;
}

uint64_t threadlayer_call_stack_size(void) {
  return threadCallStackSize;
}
