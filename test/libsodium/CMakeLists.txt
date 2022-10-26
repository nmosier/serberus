include(LibsodiumLibrary2)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

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
libsodium_cloucc_library(libsodium_cloucc_udt
  PASS MitigatePass
  LLVMFLAGS -clou=udt
  CLOUFLAGS -clou=udt
)

libsodium_cloucc_library(libsodium_cloucc_ncas
  PASS MitigatePass
  LLVMFLAGS -clou=oobs
  CLOUFLAGS -clou=oobs
)

libsodium_cloucc_library(libsodium_cloucc_fps
  PASS NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps,postch -no-stack-slot-sharing
  CLOUFLAGS -clou=fps,postch
)

libsodium_cloucc_library(libsodium_cloucc_prech
  LLVMFLAGS -clou=prech
  CLOUFLAGS -clou=prech
)

libsodium_cloucc_library(libsodium_cloucc
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -no-stack-slot-sharing
)

libsodium_cloucc_library(libsodium_cloucc_dbg
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -no-stack-slot-sharing
  CFLAGS -g
)

libsodium_benchmark_library(libsodium_cloucc_unsafeaa
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -no-stack-slot-sharing -clou-unsafe-aa
)

libsodium_cloucc_library(libsodium_cloucc_unweighted
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -no-stack-slot-sharing -clou-weight=0
)

libsodium_cloucc_library(libsodium_cloucc_check
  PASS ContractChecker
)
