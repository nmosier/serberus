include(LibsodiumLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(libsodium_benchmark_library NAME)
  libsodium_library(${NAME}
    CFLAGS -O3 -fno-stack-protector
    CONFIGURE_OPTIONS --disable-asm
    ${ARGN}
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
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

libsodium_cloucc_library(libsodium_cloucc_ncasall
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -llsct-ncas-all
)

libsodium_cloucc_library(libsodium_cloucc_csrs
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,prech,fps,postch
)

libsodium_cloucc_library(libsodium_cloucc_ssbd
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,prech,stackinit
  ${baseline_ssbd_args}
)

libsodium_cloucc_library(libsodium_cloucc_init
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,prech,stackinit
)

libsodium_cloucc_library(libsodium_cloucc_mem
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

libsodium_cloucc_library(libsodium_cloucc_naive
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=load_xmit,prech
)

libsodium_cloucc_library(libsodium_cloucc_opt
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-expand-sts
)

libsodium_cloucc_library(libsodium_cloucc_ncal_xmit
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit
)

libsodium_cloucc_library(libsodium_cloucc_ncal_glob
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_glob
)

libsodium_cloucc_library(libsodium_cloucc_ncas_xmit
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit
)

libsodium_cloucc_library(libsodium_cloucc_ncas_ctrl
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_ctrl
)

libsodium_cloucc_library(libsodium_cloucc_load_xmit
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=load_xmit
)

libsodium_cloucc_library(libsodium_cloucc_entry
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,entry_xmit,prech
)

libsodium_cloucc_library(libsodium_cloucc_sts
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl
)


libsodium_cloucc_library(libsodium_cloucc_fps
  PASS DuplicatePass InlinePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps
)

libsodium_cloucc_library(libsodium_cloucc_fps+postch
  PASS DuplicatePass InlinePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps,postch
)

libsodium_cloucc_library(libsodium_cloucc_prech
  PASS DuplicatePass InlinePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=prech
)
