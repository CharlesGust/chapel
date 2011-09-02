// IMPORTANT NOTE: This module is currently initialized manually in
// runtime/src/main.c and then again during the normal module
// initialization phase, and therefore must only contain
// initialization code that is safe to call early and twice.
//
// numThreadsPerLocale is used to tell the task layer how many threads
// to use to execute the user's tasks.  The interpretation of the
// value varies depending on task layer and is documented in README.tasks
// in the release documentation directory.  The sentinel value of 0
// indicates that the tasking layer can determine the number of
// threads to use.
//
config const numThreadsPerLocale = 0;
const chpl__maxThreadsPerLocale = chpl__initMaxThreadsPerLocale();

//
// Legality check for numThreadsPerLocale
//
if numThreadsPerLocale < 0 {
  __primitive("chpl_error", "numThreadsPerLocale must be >= 0");
}
if chpl__maxThreadsPerLocale != 0 then
  if (numThreadsPerLocale > chpl__maxThreadsPerLocale) then
    __primitive("chpl_warning",
                "specified value of " + numThreadsPerLocale
                + " for numThreadsPerLocale is too high; limit is " 
                + chpl__maxThreadsPerLocale);


//
// the size of a call stack
//
config const callStackSize: uint(64) = 0;


proc chpl__initMaxThreadsPerLocale() {
  extern proc chpl_maxThreads(): int(32);

  return chpl_maxThreads();
}
