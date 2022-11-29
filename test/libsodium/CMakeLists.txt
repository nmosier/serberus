include(LibsodiumLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(libsodium_benchmark_library NAME)
  libsodium_library(${NAME}
    CFLAGS -O3 -fno-stack-protector
    ${ARGN}
    LLVMFLAGS -clou-mitigation-trap
  )
  libsodium_library(${NAME}_marked
    CFLAGS -O3 -fno-stack-protector
    ${ARGN}
    LLVMFLAGS -clou-mitigation-trap
    LDFLAGS -Wl,-z,trace-mitigations
  )
endfunction()

function(libsodium_baseline_library NAME)
  libsodium_benchmark_library(${NAME} ${ARGN})
endfunction()

function(libsodium_cloucc_library NAME)
  libsodium_benchmark_library(${NAME} ${cloucc_base_args} ${ARGN})
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
  # PASS InlinePass MitigatePass Attributes
  PASS MitigatePass Attributes # InlinePass causes it to time out...
  LLVMFLAGS -clou=udt
)

libsodium_cloucc_library(libsodium_cloucc_ncas
  PASS InlinePass MitigatePass Attributes
  LLVMFLAGS -clou=oobs
)

libsodium_cloucc_library(libsodium_cloucc_fps
  PASS InlinePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps,postch
)

libsodium_cloucc_library(libsodium_cloucc_prech
  PASS InlinePass Attributes
  LLVMFLAGS -clou=prech
)

libsodium_cloucc_library(libsodium_cloucc
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

libsodium_cloucc_library(libsodium_cloucc_dbg
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
  CFLAGS -g
)

libsodium_benchmark_library(libsodium_cloucc_unsafeaa
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-unsafe-aa
)

libsodium_cloucc_library(libsodium_cloucc_unweighted
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-weight=0
)

libsodium_cloucc_library(libsodium_cloucc_check
  PASS ContractChecker
  CFLAGS -g -Werror
)

libsodium_cloucc_library(libsodium_cloucc_nofps
  PASS InlinePass MitigatePass Attributes
  LLVMFLAGS -clou=udt,oobs,prech
)
  
