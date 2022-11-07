#include "benchmark_memory.h"
#include "shared-main.h"
#include <sys/resource.h>
#include <err.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <climits>
#include <cstring>
#include <vector>
#include <fstream>

static void print_memusage(std::ostream& os) {
  struct rusage ru;
  if (getrusage(RUSAGE_CHILDREN, &ru) < 0)
    err(EXIT_FAILURE, "getrusage");
  os << "{\"mem\": " << ru.ru_maxrss << ", \"unit\": \"KB\"}" << "\n";
}

int main(int argc, char *argv[]) {  
  const pid_t pid = fork();
  if (pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (pid == 0) {
    benchmark::State state(BENCH_ARG);
    BENCH_NAME(state);
  } else {
    
    if (wait(nullptr) < 0) {
      err(EXIT_FAILURE, "wait");
    }

    std::ofstream ofs;
    std::ostream *os = &std::cout;
    for (int i = 1; i < argc; ++i) {
      const char *s = argv[i];
      std::vector<char> path(std::strlen(s) + 1);
      if (std::sscanf(argv[i], "--benchmark_out=%s", path.data()) == 1) {
	ofs.open(path.data());
	os = &ofs;
      }
    }    
    print_memusage(*os);
    
  }
}
