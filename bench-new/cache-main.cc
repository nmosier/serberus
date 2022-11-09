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

static long execute([[maybe_unused]] char *argv[]) {
  bool *shm;
  if ((shm = (bool *) mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0)) == MAP_FAILED)
    err(EXIT_FAILURE, "mmap");
  *shm = false;
  
  const pid_t pid = fork();
  if (pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (pid == 0) {
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
  ptrace_chk(PTRACE_CONT, pid, NULL, NULL);
  *shm = true;
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP);
  
  // start perf monitoring
  int pipefds[2];
  if (pipe(pipefds) < 0) err(EXIT_FAILURE, "pipe");
  const pid_t perf_pid = fork();
  if (perf_pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (perf_pid == 0) {
    close(pipefds[0]);
    if (dup2(pipefds[1], STDERR_FILENO) < 0) err(EXIT_FAILURE, "dup2");
    // TODO: parameterize perf tool
    execlp("perf-5.9.0+", "stat", "-p", std::to_string(perf_pid).c_str(),
	   "-e", "cache-misses", nullptr);
    err(EXIT_FAILURE, "execlp: perf-5.9.0+");
  }
  close(pipefds[1]);

  FILE *f;
  if ((f = fdopen(pipefds[0], "r")) == nullptr) err(EXIT_FAILURE, "fdopen");

  // run process until finish breakpoint
  ptrace_chk(PTRACE_CONT, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP);
    
  // kill perf tool
  if (kill(perf_pid, SIGINT) < 0) err(EXIT_FAILURE, "kill: %d", perf_pid);
  status = waitpid_chk(perf_pid);
  assert(WIFSIGNALED(status) && WTERMSIG(SIGINT));

  // let tracee run to completion
  ptrace(PTRACE_CONT, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);

  fprintf(stderr, "here\n");

  // finally, process perf's output
  char line[4096];
  while (std::fgets(line, sizeof line, f)) {
    fprintf(stderr, "%s", line);
    
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
      std::fclose(f);
      close(pipefds[1]);      
      return count;
    }
  }
  if (std::ferror(f)) {
    err(EXIT_FAILURE, "fgets");
  }

  errx(EXIT_FAILURE, "'perf' output didn't contain an entry for cache-misses!");
}
