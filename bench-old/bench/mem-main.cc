#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <err.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/ptrace.h>
#include <sys/mman.h>

#include <sstream>
#include <fstream>
#include <string>
#include <map>
#include <iostream>

#include "shared.h"
#include "shared-pseudo.h"


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

volatile bool *shm = nullptr;
__attribute__((constructor)) void init_shared_memory(void) {
  if ((shm = (bool *) mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0)) == MAP_FAILED)
    err(EXIT_FAILURE, "mmap");
}

static long compute_memory_usage(pid_t pid, std::map<std::string, long>& rss_per_map, bool add);

static void copy_file(const std::string& from, const std::string& to);

static long execute([[maybe_unused]] char *argv[]) {
  *shm = false;

  const pid_t pid = fork();
  if (pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (pid == 0) {
    benchmark::State state(BENCH_ARG);
    while (!*shm) {}
    asm volatile ("int3");
    SAFE_CALL(BENCH_NAME(state));
    asm volatile ("int3");
    std::exit(EXIT_SUCCESS);
  }

  [[maybe_unused]] int status;

  // attach to process
  ptrace_chk(PTRACE_ATTACH, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP);
  *shm = true;
  ptrace_chk(PTRACE_CONT, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP);

  // compute initial memory usage
  std::map<std::string, long> rss_per_map;
  const long initial = compute_memory_usage(pid, rss_per_map, true);
  {
    std::stringstream in; in << "/proc/" << pid << "/smaps";
    std::stringstream out; out << "smaps." << pid << ".initial";
    copy_file(in.str(), out.str());
  }

  ptrace_chk(PTRACE_CONT, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP);

  // compute final memory usage
  const long final_ = compute_memory_usage(pid, rss_per_map, false);
  {
    std::stringstream in; in << "/proc/" << pid << "/smaps";
    std::stringstream out; out << "smaps." << pid << ".final";
    copy_file(in.str(), out.str());
  }

  {
    std::stringstream in; in << "/proc/" << pid << "/maps";
    std::stringstream out; out << "maps." << pid;
    copy_file(in.str(), out.str());
  }
  
  ptrace_chk(PTRACE_KILL, pid, nullptr, nullptr);
  status = waitpid_chk(pid);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);

  assert(final_ >= initial);

  fprintf(stderr, "initial: %ldK\nfinal:  %ldK\n", initial / 1024, final_ / 1024);

  for (const auto& [map, rss] : rss_per_map) {
    std::cerr << rss << "K \t" << map << "\n";
  }
  
  return final_ - initial;
}

static long compute_memory_usage(pid_t pid, std::map<std::string, long>& rss_per_map, bool sub) {
  std::stringstream path;
  path << "/proc/" << pid << "/smaps";
  std::ifstream is(path.str());

  long total_rss = 0;

  std::string map;
  std::string s;
  while (std::getline(is, s)) {
    long n;
    if (std::sscanf(s.c_str(), "Rss: %ld kB", &n) == 1) {
      static const char *allowlist[] = {
	BENCH_LIB,
	"[stack]",
	"[heap]",
      };
      if (std::any_of(std::begin(allowlist), std::end(allowlist), [&map] (const char *substr) {
	return map.find(substr) != std::string::npos;
      })) {
	total_rss += n;
      }
      if (!sub) {
	rss_per_map[map] += n;
      } else {
	rss_per_map[map] -= n;
      }
      continue;
    }

    if (std::sscanf(s.c_str(), "Swap: %ld kB", &n) == 1) {
      assert(n == 0);
      continue;
    }

    assert(!(s.starts_with("Rss:") || s.starts_with("Swap:")));

    char map_tmp[256];
    size_t start, stop;
    map_tmp[0] = '\0';
    char perm[8];
    perm[0] = '\0';
    if (std::sscanf(s.c_str(), "%zx-%zx %s %*s %*s %*s %s", &start, &stop, perm, map_tmp) >= 2) {
      map = std::string(map_tmp) + "/" + perm;
      continue;
    }
  }

  return total_rss * 1024; // return in bytes
}

static void copy_file(const std::string& from, const std::string& to) {
  std::ifstream ifs(from);
  std::ofstream ofs(to);
  std::string line;
  while (std::getline(ifs, line)) {
    ofs << line << "\n";
  }
}
