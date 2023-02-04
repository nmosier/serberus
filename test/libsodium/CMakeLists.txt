include(LibsodiumLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(libsodium_benchmark_library NAME)
  libsodium_library(${NAME}
    CFLAGS -O3 -fno-stack-protector
    ${ARGN}
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
libsodium_cloucc_library(libsodium_cloucc
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

libsodium_cloucc_library(libsodium_cloucc_logs
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  CFLAGS -g
  LLVMFLAGS -clou -clou-log=${CMAKE_CURRENT_BINARY_DIR}/logs
)

libsodium_cloucc_library(libsodium_cloucc_ncal_xmit
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit
)

libsodium_cloucc_library(libsodium_cloucc_ncas_xmit
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit
)

libsodium_cloucc_library(libsodium_cloucc_ncas_ctrl
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_ctrl
)

libsodium_cloucc_library(libsodium_cloucc_sts
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncas_xmit,ncas_ctrl
)


libsodium_cloucc_library(libsodium_cloucc_fps
  PASS InlinePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps,postch
)
