include(ConfigureLibrary)
include(BenchmarkFlags)

function(add_tar_binary NAME)
  add_configure_library(tar libtar.so tar.h ${CMAKE_SOURCE_DIR}/../tar ${NAME}
    CFLAGS -O3 -flegacy-pass-manager -fno-stack-protector
    ${ARGN})
endfunction()

add_tar_binary(tar_baseline_none)
add_tar_binary(tar_cloucc
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)
