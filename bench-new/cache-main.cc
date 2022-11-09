#include <sys/types.h>
#include <unistd.h>
#include <err.h>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include "shared-pseudo.h"
#include <sys/ptrace.h>
#include <csignal>
#include <cstring>
#include <sys/mman.h>
#include <sys/wait.h>
#include <cassert>
#include <string>

#define ptrace_chk(request, pid, addr, data)				\
  do {									\
    if (ptrace(request, pid, addr, data) < 0)				\
      err(EXIT_FAILURE, "%s:%d: ptrace: %s", __FILE__, __LINE__, #request); \
  } while (false)

static int waitpid_chk(pid_t pid) {
  int status;
  if (waitpid(pid, &status, 0) < 0)
    err(EXIT_FAILURE, "waitpid");
  return status;
}

bool *shm = nullptr;
__attribute__((constructor)) void init_shared_memory(void) {
  if ((shm = (bool *) mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0)) == MAP_FAILED)
    err(EXIT_FAILURE, "mmap");
}

static long execute([[maybe_unused]] char *argv[]) {
  *shm = false;
  const pid_t pid = fork();
  if (pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (pid == 0) {
    fprintf(stderr, "pid = %d\n", getpid());
    while (!*shm) {}
    asm volatile ("int3");
    benchmark::State state(BENCH_ARG);
    BENCH_NAME(state);
    asm volatile ("int3");
    exit(EXIT_SUCCESS);
  }

  int status;

  // attach to process
  ptrace_chk(PTRACE_ATTACH, pid, NULL, NULL);
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP);
  *shm = true;
  ptrace_chk(PTRACE_CONT, pid, NULL, NULL);
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP);
  
  // start perf monitoring
  FILE *f;
  char cmd[1024];
  const char *perf = "perf-5.9.0+";
  sprintf(cmd, "%s stat -e cache-misses -p %d 2>&1", perf, pid);
  if ((f = popen(cmd, "r")) == nullptr) err(EXIT_FAILURE, "popen: %s", cmd);

  // give perf time to be able to start up
  sleep(1);
  
  // run process until finish breakpoint then kill it
  ptrace_chk(PTRACE_CONT, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP);
  ptrace_chk(PTRACE_KILL, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);

#if 0
  status = waitpid_chk(perf_pid);
  assert(WIFEXITED(status));
  if (WEXITSTATUS(status) != EXIT_SUCCESS) {
    char line[4096];
    while (std::fgets(line, sizeof line, f)) {
      std::fprintf(stderr, "%s", line);
    }
    errx(EXIT_FAILURE, "'perf' command failed");
  }
#endif

  // finally, process perf's output
  char line[4096];
  while (std::fgets(line, sizeof line, f)) {
    fputs(line, stderr);
    if (std::strstr(line, "cache-misses")) {
      // find first non-empty token
      char *s = line;
      const char *token;
      while ((token = strsep(&s, " ")) && *token == '\0') {}
      if (token == nullptr) {
	errx(EXIT_FAILURE, "unable to parse output of 'perf' command");
      }

      // remove commas
      std::string num(token);
      std::erase(num, ',');
      char *end;
      const unsigned long count = std::strtoul(num.c_str(), &end, 0);
      if (num.empty() || *end) {
	errx(EXIT_FAILURE, "bad count in output of 'perf' command");
      }
      fclose(f);
      return count;
    }
  }
  if (std::ferror(f)) {
    err(EXIT_FAILURE, "fgets");
  }

  errx(EXIT_FAILURE, "'perf' output didn't contain an entry for cache-misses!");
}
