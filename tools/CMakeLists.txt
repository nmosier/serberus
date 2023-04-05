add_compile_options(-fcf-protection=branch)

add_executable(ssbd
  ssbd.c
)

add_library(trace_runtime STATIC
  trace_runtime.cc
)
target_link_libraries(trace_runtime PRIVATE ${Libunwind_LIBRARIES})
target_link_directories(trace_runtime PRIVATE ${Libunwind_LIBRARY_DIRS})
target_include_directories(trace_runtime PRIVATE ${Libunwind_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

add_library(libssbd SHARED
  libssbd.c
)

add_library(cet SHARED
  cet.c
)

add_executable(memusage
  memusage.c
)

add_subdirectory(pin)

add_library(msr SHARED
  msr.c
)
