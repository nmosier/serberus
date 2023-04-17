set(base_args
  CFLAGS -O3 -fcf-protection=branch -flegacy-pass-manager
  LDFLAGS -fuse-ld=${LLVM_BINARY_DIR}/bin/ld.lld
  DEPENDS ${LLVM_LIBRARY_DIR}/bin/ld.lld
)

set(baseline_none_args ${base_args})

set(_baseline_lfence_args_only LLVMFLAGS -x86-speculative-load-hardening -x86-slh-lfence -x86-slh-fence-call-and-ret)
set(baseline_lfence_args ${baseline_none_args} ${_baseline_lfence_args_only})

set(_baseline_slh_args_only LLVMFLAGS -x86-speculative-load-hardening)
set(baseline_slh_args ${baseline_none_args} ${_baseline_slh_args_only})

set(_baseline_retpoline_args_only
  CFLAGS -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
)
set(baseline_retpoline_args ${baseline_none_args} ${_baseline_retpoline_args_only})

set(_baseline_ssbd_args_only
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:libssbd> -L$<TARGET_FILE_DIR:libssbd> -l$<TARGET_FILE_BASE_NAME:libssbd>
  DEPENDS libssbd
)
set(baseline_ssbd_args ${baseline_none_args} ${_baseline_ssbd_args_only})

set(baseline_lfence_retpoline_ssbd_args ${baseline_none_args}
  ${_baseline_lfence_args_only} ${_baseline_retpoline_args_only} ${_baseline_ssbd_args_only}
)
set(baseline_slh_retpoline_ssbd_args ${baseline_none_args}
  ${_baseline_slh_args_only} ${_baseline_retpoline_args_only} ${_baseline_ssbd_args_only}
)

# CLOU

set(cloucc_base_args ${base_args}
  LLVMFLAGS -clou
  
  # Restrictions
  LLVMFLAGS -no-stack-slot-sharing -no-promote-arguments
  CFLAGS -fno-jump-tables

  # CET
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:cet> -L$<TARGET_FILE_DIR:cet> -lcet
  DEPENDS cet
  LDFLAGS -Wl,-z,force-ibt
)

set(cloucc_args ${cloucc_base_args}
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
)

# TODO: finish this




