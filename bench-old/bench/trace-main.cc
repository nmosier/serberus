#define NOMAIN 1
#include "shared-pseudo.h"
#include "shared.h"

int main() {
  benchmark::State state(BENCH_ARG);
  SAFE_CALL(BENCH_NAME(state));
}
