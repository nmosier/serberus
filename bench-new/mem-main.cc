#include "benchmark_memory.h"
#include "shared-main.h"
#include <sys/resource.h>
#include <err.h>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <cstring>

static void print_memusage(std::ostream& os) {
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) < 0)
    err(EXIT_FAILURE, "getrusage");
  os << "{\"mem\": " << ru.ru_maxrss << ", \"unit\": \"KB\"}" << "\n";
}

int main() {
  print_memusage(std::cout);
  benchmark::State state(BENCH_ARG);
  BENCH_NAME(state);

  std::vector<char> vec(1000000);
  for (char &c : vec) c = rand();
  
  print_memusage(std::cout);
}
