include(LibsodiumLibrary2)

make_directory(logs)

function(libsodium_benchmark_library NAME)
  libsodium_library(${NAME}
    CFLAGS -O3 -fno-stack-protector
    ${ARGN}
  )
endfunction()

function(libsodium_baseline_library NAME)
  libsodium_benchmark_library(${NAME} ${ARGN})
endfunction()

function(libsodium_cloucc_library NAME)
  libsodium_benchmark_library(${NAME} ${ARGN})
endfunction()

set(baseline_lfence_args
  LLVMFLAGS -x86-speculative-load-hardening -x86-slh-lfence -x86-slh-fence-call-and-ret
)
set(baseline_slh_args
  LLVMFLAGS -x86-speculative-load-hardening
)
set(baseline_retpoline_args
  CFLAGS -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
)
set(baseline_ssbd_args
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:libssbd> -L$<TARGET_FILE_DIR:libssbd> -l$<TARGET_FILE_BASE_NAME:libssbd>
  DEPENDS libssbd
)

# Component Mitigations
libsodium_baseline_library(libsodium_baseline_none)
libsodium_baseline_library(libsodium_baseline_lfence ${baseline_lfence_args})
libsodium_baseline_library(libsodium_baseline_slh ${baseline_slh_args})
libsodium_baseline_library(libsodium_baseline_retpoline ${baseline_retpoline_args})
libsodium_baseline_library(libsodium_baseline_ssbd ${baseline_ssbd_args})

# Composite Mitigations
libsodium_baseline_library(libsodium_baseline_lfence+retpoline+ssbd ${baseline_lfence_args} ${baseline_retpoline_args} ${baseline_ssbd_args})
libsodium_baseline_library(libsodium_baseline_slh+retpoline+ssbd ${baseline_slh_args} ${baseline_retpoline_args} ${baseline_ssbd_args})

# ClouCC Mitigation
libsodium_cloucc_library(libsodium_cloucc
  PASS MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -x86-function-local-stacks -no-stack-slot-sharing -clou-stack-mitigation=fps
  CLOUFLAGS -clou-log=${CMAKE_CURRENT_BINARY_DIR}/logs
)

