include(OpensslLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(openssl_base_library NAME)
  openssl_library(${NAME}
    CFLAGS -O3 -fno-stack-protector
    ${ARGN}
  )
endfunction()

function(openssl_baseline_library NAME)
  openssl_base_library(${NAME} ${ARGN})
endfunction()

function(openssl_cloucc_library NAME)
  openssl_base_library(${NAME} ${cloucc_base_args})
endfunction()

openssl_baseline_library(openssl_baseline_none
  CFLAGS -O3 -fno-stack-protector
)

openssl_baseline_library(openssl_baseline_lfence
  CFLAGS -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
  LLVMFLAGS -x86-speculative-load-hardening -x86-slh-lfence -x86-slh-fence-call-and-ret
)

openssl_baseline_library(openssl_baseline_slh
  CFLAGS -O3 -fno-stack-protector -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
  LLVMFLAGS -x86-speculative-load-hardening
)

openssl_cloucc_library(openssl_cloucc
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  # LDFLAGS -Wl,--no-relax
  LLVMFLAGS -clou
  CLOUFLAGS -clou-log=${CMAKE_CURRENT_BINARY_DIR}/logs
)
