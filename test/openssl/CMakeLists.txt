include(OpensslLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(openssl_base_library NAME)
  openssl_library(${NAME}
    CFLAGS -O3 -fno-stack-protector
    CONFIGURE_OPTIONS no-asm
    ${ARGN}
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
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

openssl_cloucc_library(openssl_cloucc_frame
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes FramePromotion
  LLVMFLAGS -clou
)

openssl_cloucc_library(openssl_cloucc_opt
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-expand-sts
)

openssl_cloucc_library(openssl_cloucc_naive
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=load_xmit,prech
)

openssl_cloucc_library(openssl_cloucc_ncal_xmit
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit
)

openssl_cloucc_library(openssl_cloucc_ncal_glob
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_glob
)

openssl_cloucc_library(openssl_cloucc_ncas_xmit
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_xmit
)

openssl_cloucc_library(openssl_cloucc_ncas_ctrl
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_ctrl
)

openssl_cloucc_library(openssl_cloucc_load_xmit
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=load_xmit
)

openssl_cloucc_library(openssl_cloucc_sts
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl
)

openssl_cloucc_library(openssl_cloucc_fps
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps
)

openssl_cloucc_library(openssl_cloucc_fps+postch
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps,postch
)

openssl_cloucc_library(openssl_cloucc_prech
  PASS DuplicatePass InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=prech
)

if(0)
openssl_cloucc_library(openssl_cloucc_trace
  PASS MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes TracePass
  LLVMFLAGS -clou
  CFLAGS -g
  LDFLAGS -L$<TARGET_FILE_DIR:trace_runtime> -l$<TARGET_LINKER_FILE_BASE_NAME:trace_runtime>
)
target_link_libraries(openssl_cloucc_trace INTERFACE trace_runtime)
endif()

openssl_cloucc_library(openssl_cloucc_baseptr
  PASS DuplicatePass InlinePass MitigatePass BasePointerPass Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,prech
)
