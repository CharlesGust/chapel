#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "chpllaunch.h"
#include "chpl_mem.h"
#include "chpltypes.h"
#include "error.h"


#define baseExpectFilename ".chpl-expect-"
#define baseSysFilename ".chpl-sys-"

char expectFilename[FILENAME_MAX];
char sysFilename[FILENAME_MAX];

/* copies of binary to run per node */
#define procsPerNode 1  
#define versionBuffLen 80

#define launcherAccountEnvvar "CHPL_LAUNCHER_ACCOUNT"

typedef enum {
  pbspro,
  nccs,
  unknown
} qsubVersion;

static qsubVersion determineQsubVersion(void) {
  char version[versionBuffLen+1] = "";
  char* versionPtr = version;
  FILE* sysFile;
  int i;

  char* command = chpl_glom_strings(3, "qsub --version > ", sysFilename, " 2>&1");
  system(command);
  sysFile = fopen(sysFilename, "r");
  for (i=0; i<versionBuffLen; i++) {
    char tmp;
    fscanf(sysFile, "%c", &tmp);
    if (tmp == '\n') {
      *versionPtr++ = '\0';
      break;
    } else {
      *versionPtr++ = tmp;
    }
  }

  fclose(sysFile);
  if (strstr(version, "NCCS")) {
    return nccs;
  } else if (strstr(version, "PBSPro")) {
    return pbspro;
  } else {
    return unknown;
  }
}

static int getNumCoresPerLocale(void) {
  FILE* sysFile;
  int coreMask;
  int bitMask = 0x1;
  int numCores;
  char* numCoresString = getenv("CHPL_LAUNCHER_CORES_PER_LOCALE");
  char* command = NULL;

  if (numCoresString) {
    numCores = atoi(numCoresString);
    if (numCores != 0)
      return numCores;
  }

  command = chpl_glom_strings(2, "cnselect -Lcoremask > ", sysFilename);
  system(command);
  sysFile = fopen(sysFilename, "r");
  if (fscanf(sysFile, "%d\n", &coreMask) != 1 || !feof(sysFile)) {
    chpl_error("unable to determine number of cores per locale; please set CHPL_LAUNCHER_CORES_PER_LOCALE", 0, 0);
  }
  coreMask >>= 1;
  numCores = 1;
  while (coreMask & bitMask) {
    coreMask >>= 1;
    numCores += 1;
  }
  fclose(sysFile);
  return numCores;
}

static char* genQsubOptions(char* genFilename, char* projectString, qsubVersion qsub, 
                            int32_t numLocales, int32_t numCoresPerLocale) {
  const size_t maxOptLength = 256;
  char* queue = getenv("CHPL_LAUNCHER_QUEUE");
  char* walltime = getenv("CHPL_LAUNCHER_WALLTIME");
  char* optionString = chpl_malloc(maxOptLength, sizeof(char), CHPL_RT_MD_COMMAND_BUFFER, -1, "");
  int length = 0;

  length += snprintf(optionString + length, maxOptLength - length,
                     "-z -V -I -N Chpl-%.10s", genFilename);

  if (projectString && strlen(projectString) != 0) {
    length += snprintf(optionString + length, maxOptLength - length,
                       " -A %s", projectString);
  }
  if (queue) {
    length += snprintf(optionString + length, maxOptLength - length,
                       " -q %s", queue);
  }
  if (walltime) {
    length += snprintf(optionString + length, maxOptLength - length,
                       " -l walltime=%s", walltime);
  }
  switch (qsub) {
  case pbspro:
  case unknown:
    length += snprintf(optionString + length, maxOptLength - length,
                       " -l mppwidth=%d -l mppnppn=%d -l mppdepth=%d",
                       numLocales, procsPerNode, numCoresPerLocale);
    break;
  case nccs:
    if (!queue && !walltime)
      chpl_error("An execution time must be specified for the NCCS launcher if no queue is\n"
                 "specified -- use the CHPL_LAUNCHER_WALLTIME and/or CHPL_LAUNCHER_QUEUE\n"
                 "environment variables", 0, 0);
    length += snprintf(optionString + length, maxOptLength - length,
                       " -l size=%d\n", numCoresPerLocale*numLocales);
    break;
  }
  return optionString;
}

static char* chpl_launch_create_command(int argc, char* argv[], 
                                        int32_t numLocales) {
  int i;
  int size;
  char baseCommand[256];
  char* command;
  FILE* expectFile;
  char* projectString = getenv(launcherAccountEnvvar);
  char* basenamePtr = strrchr(argv[0], '/');
  char* qsubOptions;
  pid_t mypid;
  int numCoresPerLocale;

  if (basenamePtr == NULL) {
      basenamePtr = argv[0];
  } else {
      basenamePtr++;
  }
  chpl_compute_real_binary_name(argv[0]);

#ifndef DEBUG_LAUNCH
  mypid = getpid();
#else
  mypid = 0;
#endif
  sprintf(sysFilename, "%s%d", baseSysFilename, (int)mypid);
  sprintf(expectFilename, "%s%d", baseExpectFilename, (int)mypid);

  numCoresPerLocale = getNumCoresPerLocale();

  qsubOptions = genQsubOptions(basenamePtr, projectString, determineQsubVersion(),
                               numLocales, numCoresPerLocale);

  expectFile = fopen(expectFilename, "w");
  if (verbosity < 2) {
    fprintf(expectFile, "log_user 0\n");
  }
  fprintf(expectFile, "set timeout -1\n");
  fprintf(expectFile, "set prompt \"(%%|#|\\\\$|>) $\"\n");
  if (verbosity > 0) {
    fprintf(expectFile, "spawn tcsh -f\n");
    fprintf(expectFile, "expect -re $prompt\n");
    fprintf(expectFile, "send \"df -T . \\n\"\n");
    fprintf(expectFile, "expect {\n");
    fprintf(expectFile, "  -re $prompt {\n");
    fprintf(expectFile, "    send_user \"warning: Executing this program from a non-Lustre file system may cause it \\nto be unlaunchable, or for file I/O to be performed on a non-local file system.\\nContinue anyway? (\\[y\\]/n) \"\n");
    fprintf(expectFile, "    interact {\n");
    fprintf(expectFile, "      \\015           {return}\n");
    fprintf(expectFile, "      -echo -re \"y|Y\" {return}\n");
    fprintf(expectFile, "      -echo -re \"n|N\" {send_user \"\\n\" ; exit 0}\n");
    fprintf(expectFile, "      eof               {send_user \"\\n\" ; exit 0}\n");
    fprintf(expectFile, "      \003              {send_user \"\\n\" ; exit 0}\n");
    fprintf(expectFile, "      -echo -re \".\"   {send_user \"\\nContinue anyway? (\\[y\\]/n) \"}\n");
    fprintf(expectFile, "    }\n");
    fprintf(expectFile, "    send_user  \"\\n\"\n");
    fprintf(expectFile, "  }\n");
    fprintf(expectFile, "  lustre\n");
    fprintf(expectFile, "}\n");
    fprintf(expectFile, "send \"exit\\n\"\n");
  }
  fprintf(expectFile, "spawn qsub %s\n", qsubOptions);
  fprintf(expectFile, "expect {\n");
  fprintf(expectFile, "  \"A project was not specified\" {send_user "
          "\"error: A project account must be specified via \\$" 
          launcherAccountEnvvar "\\n\" ; exit 1}\n");
  fprintf(expectFile, "  -re $prompt\n");
  fprintf(expectFile, "}\n");
  fprintf(expectFile, "send \"cd \\$PBS_O_WORKDIR\\n\"\n");
  fprintf(expectFile, "expect -re $prompt\n");
  fprintf(expectFile, "send \"aprun -q -b -d%d -n1 -N1 ls %s\\n\"\n", numCoresPerLocale, chpl_get_real_binary_name());
  fprintf(expectFile, "expect {\n");
  fprintf(expectFile, "  \"failed: chdir\" {send_user "
          "\"error: %s must be launched from and/or stored on a "
          "cross-mounted file system\\n\" ; exit 1}\n", 
          basenamePtr);
  fprintf(expectFile, "  -re $prompt\n");
  fprintf(expectFile, "}\n");
  fprintf(expectFile, "send \"aprun ");
  if (verbosity < 2) {
    fprintf(expectFile, "-q ");
  }
  fprintf(expectFile, "-d%d -n%d -N%d %s", numCoresPerLocale, numLocales, procsPerNode, chpl_get_real_binary_name());
  for (i=1; i<argc; i++) {
    fprintf(expectFile, " '%s'", argv[i]);
  }
  fprintf(expectFile, "\\n\"\n");
  fprintf(expectFile, "interact -o -re $prompt {return}\n");
  fprintf(expectFile, "send_user \"\\n\"\n");
  fprintf(expectFile, "send \"exit\\n\"\n");
  fclose(expectFile);

  sprintf(baseCommand, "expect %s", expectFilename);

  size = strlen(baseCommand) + 1;

  command = chpl_malloc(size, sizeof(char), CHPL_RT_MD_COMMAND_BUFFER, -1, "");
  
  sprintf(command, "%s", baseCommand);

  if (strlen(command)+1 > size) {
    chpl_internal_error("buffer overflow");
  }

  return command;
}

static void chpl_launch_cleanup(void) {
#ifndef DEBUG_LAUNCH
  char command[1024];

  sprintf(command, "rm %s", expectFilename);
  system(command);

  sprintf(command, "rm %s", sysFilename);
  system(command);
#endif
}


void chpl_launch(int argc, char* argv[], int32_t numLocales) {
  chpl_launch_using_system(chpl_launch_create_command(argc, argv, numLocales),
                           argv[0]);
  chpl_launch_cleanup();
}


int chpl_launch_handle_arg(int argc, char* argv[], int argNum,
                           int32_t lineno, chpl_string filename) {
  return 0;
}
