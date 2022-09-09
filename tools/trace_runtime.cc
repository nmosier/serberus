#include <libunwind.h>
#include <err.h>
#include <cstdlib>
#include <string>
#include <map>
#include <cstdint>

#define UNW_CHK(name, res)					\
  do {								\
    const int err_ = (res);					\
    if (err_ < 0) {						\
      errx(EXIT_FAILURE, name ": %s", unw_strerror(-err));	\
    }								\
  } while (false)

namespace {

  static FILE *log = stderr;

  struct Trace {
    using Key = uintptr_t;
    struct Value {
      char func[256];
      unw_word_t off;
      size_t n = 0;
    };

    std::map<Key, Value> locs;

    ~Trace() {
      for (const auto& [key, value] : locs) {
	fprintf(log, "%zu %s%+zd\n", value.n, value.func, value.off);
      }
    }
    
  } trace;

}

extern "C" void clou_trace(void) {
  unw_context_t ctx;
  int err;
  UNW_CHK("unw_getcontext", unw_getcontext(&ctx));

  unw_cursor_t cursor;
  UNW_CHK("unw_init_local", unw_init_local(&cursor, &ctx));

  err = unw_step(&cursor);
  if (err < 0) {
    errx(EXIT_FAILURE, "unw_step: %s", unw_strerror(-err));
  } else if (err == 0) {
    std::abort();
  }

  unw_word_t rip;
  UNW_CHK("unw_get_reg", unw_get_reg(&cursor, UNW_X86_64_RIP, &rip));

  
  auto& record = trace.locs[rip];
  if (record.n == 0) {
    char name[1024];
    UNW_CHK("unw_get_proc_name", unw_get_proc_name(&cursor, record.func, sizeof record.func, &record.off));
  }
  ++record.n;
}
