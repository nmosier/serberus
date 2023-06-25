set(compile_base
  CFLAGS -O3 -fno-stack-protector -fcf-protection=none
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:msr> -L$<TARGET_FILE_DIR:msr> -lmsr
  LDFLAGS -fuse-ld=${LLVM_BINARY_DIR}/bin/ld.lld
  DEPENDS msr ${LLVM_BINARY_DIR}/bin/ld.lld
)
set(run_base)

set(compile_lfence
  LLVMFLAGS -x86-speculative-load-hardening -x86-slh-lfence
)
set(run_lfence)

set(compile_slh
  LLVMFLAGS -x86-speculative-load-hardening
)
set(run_slh)

set(compile_retpoline
  CFLAGS -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
)
set(run_retpoline)

set(compile_ssbd)
set(run_ssbd SSBD=1)

set(compile_hwmodel
  CFLAGS -fcf-protection=full
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:cet> -L$<TARGET_FILE_DIR:cet> -lcet
  DEPENDS cet
  LDFLAGS -Wl,-z,force-ibt -Wl,-z,force-shstk
)
set(run_hwmodel RRSBA_DIS_U=1 PSFD=1 DOITM=1)

set(compile_swmodel
  LLVMFLAGS -no-stack-slot-sharing -no-promote-arguments
  CFLAGS -fno-jump-tables -mno-red-zone
  PASS DuplicatePass MemIntrinsicPass InlinePass Attributes
)
set(run_swmodel)

set(compile_llsct_fence
  LLVMFLAGS -clou=+ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl
  PASS MitigatePass
)
set(run_llsct_fence)

set(compile_llsct_fps
  LLVMFLAGS -clou=+fps
  PASS FunctionLocalStacks
)
set(run_llsct_fps)

set(compile_llsct_regclean
  LLVMFLAGS -clou=+prech
)
set(run_llsct_regclean)

set(compile_llsctssbd_fence ${compile_llsct_fence}
  LLVMFLAGS -clou=+call_xmit -llsct-timeout=10
)
set(run_llsctssbd_fence ${run_llsct_fence})

set(compile_llsctssbd_stkinit
  PASS StackInitPass
)

set(compile_hwmodel+psf ${compile_hwmodel})
set(run_hwmodel+psf ${run_hwmodel} PSFD=0)

set(compile_llsctpsf_fence
  LLVMFLAGS -clou=load_xmit -llsct-timeout=10
  PASS MitigatePass
)
set(run_llsctpsf_fence)

set(compile_llsct_fallthru LLVMFLAGS -clou=+fallthru)
set(run_llsct_fallthru)


# Ultimate SLH
set(compile_uslh
  LLVMFLAGS --x86-speculative-load-hardening --x86-slh-fixed --x86-slh-indirect --x86-slh-ip --x86-slh-loads --x86-slh-sbhAll --x86-slh-vtInstr --x86-slh-post-load=0 --x86-slh-store
)
set(run_uslh)

# Blade
set(compile_blade
  LLVMFLAGS --x86-speculative-load-hardening --x86-slh-blade --x86-slh-loads=0
)
set(run_blade)

include(LibsodiumLibrary)
function(add_libsodium_library NAME)
  libsodium_library(${NAME}
    CONFIGURE_OPTIONS --disable-asm
    ${ARGN}
  )
endfunction()

include(OpensslLibrary)
function(add_openssl_library NAME)
  openssl_library(${NAME}
    CONFIGURE_OPTIONS no-asm
    ${ARGN}
  )
endfunction()

include(HaclLibrary)
function(add_hacl_library NAME)
  hacl_library(${NAME}
    ${ARGN}
  )
endfunction()

set(benchmark_runtime_flags --benchmark_repetitions=5 --benchmark_min_warmup_time=1 --benchmark_display_aggregates_only=true
  --benchmark_time_unit=ns)
