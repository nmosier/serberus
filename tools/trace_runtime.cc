#include <libunwind.h>
#include <err.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <cstdint>

#define UNW_CHK(name, res)					\
  do {								\
    const int err_ = (res);					\
    if (err_ < 0) {						\
      errx(EXIT_FAILURE, name ": %s", unw_strerror(-err_));	\
    }								\
  } while (false)

namespace {

  static FILE *log = stderr;

  struct Trace {
    std::map<const char *, unsigned> lfences;
    ~Trace() {
      std::map<std::string, unsigned> counts;
      for (const auto& [s, n] : lfences)
	counts[s] += n;
      for (const auto& [s, n] : counts)
	std::fprintf(log, "%u %s\n", n, s.c_str());
    }
    
  } trace;

}

extern "C" void clou_trace(const char *s) {
  trace.lfences[s]++;
}
