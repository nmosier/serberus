include(OpensslLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(openssl_base_library NAME)
  openssl_library(${NAME}
    CFLAGS -O3 -fno-stack-protector
    CONFIGURE_OPTIONS no-asm
    ${ARGN}
  )
  openssl_library(${NAME}_marked
    CFLAGS -O3 -fno-stack-protector
    CONFIGURE_OPTIONS no-asm
    ${ARGN}
    LLVMFLAGS -clou-mitigation-trap
    LDFLAGS -Wl,-z,trace-mitigations
  )
endfunction()

function(openssl_baseline_library NAME)
  openssl_base_library(${NAME} ${ARGN})
endfunction()

function(openssl_cloucc_library NAME)
  openssl_base_library(${NAME} ${cloucc_base_args} ${ARGN})
endfunction()

# Component Mitigations
openssl_baseline_library(openssl_baseline_none)
openssl_baseline_library(openssl_baseline_lfence ${baseline_lfence_args})
openssl_baseline_library(openssl_baseline_slh ${baseline_slh_args})
openssl_baseline_library(openssl_baseline_retpoline ${baseline_retpoline_args})
openssl_baseline_library(openssl_baseline_ssbd ${baseline_ssbd_args})

# Composite Mitigations
openssl_baseline_library(openssl_baseline_lfence+retpoline+ssbd ${baseline_lfence_args} ${baseline_retpoline_args} ${baseline_ssbd_args})
openssl_baseline_library(openssl_baseline_slh+retpoline+ssbd ${baseline_slh_args} ${baseline_retpoline_args} ${baseline_ssbd_args})


openssl_cloucc_library(openssl_cloucc
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  # LDFLAGS -Wl,--no-relax
  LLVMFLAGS -clou
  # CLOUFLAGS -clou-log=${CMAKE_CURRENT_BINARY_DIR}/logs
)

openssl_cloucc_library(openssl_cloucc_ncal_xmit
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit
)

openssl_cloucc_library(openssl_cloucc_ncas_xmit
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_xmit
)

openssl_cloucc_library(openssl_cloucc_ncas_ctrl
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_ctrl
)
