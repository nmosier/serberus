include(OpensslLibrary2)

make_directory(logs)

openssl_library(openssl_baseline_none
  CFLAGS -O3 -fno-stack-protector
)

openssl_library(openssl_baseline_lfence
  PASS # BaselinePass # For now, don't include this.
  CFLAGS -O3 -fno-stack-protector -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
  LLVMFLAGS -x86-speculative-load-hardening -x86-slh-lfence -x86-slh-fence-call-and-ret
)

openssl_library(openssl_baseline_slh
  CFLAGS -O3 -fno-stack-protector -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
  LLVMFLAGS -x86-speculative-load-hardening
)

openssl_library(openssl_cloucc
  PASS MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  CFLAGS -O3 -fno-stack-protector -fPIC
  LDFLAGS -Wl,--no-relax
  LLVMFLAGS -x86-function-local-stacks -no-stack-slot-sharing -clou-stack-mitigation=fps
  CLOUFLAGS -clou-log=${CMAKE_CURRENT_BINARY_DIR}/logs
)
