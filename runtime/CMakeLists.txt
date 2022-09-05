add_library(clou_runtime SHARED
  clou.c
)
target_compile_options(clou_runtime PRIVATE -masm=intel)
