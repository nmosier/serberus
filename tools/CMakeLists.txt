add_executable(ssbd
  ssbd.c
)

add_library(trace_runtime SHARED
  trace_runtime.cc
)
target_link_libraries(trace_runtime PRIVATE ${Libunwind_LIBRARIES})
target_link_directories(trace_runtime PRIVATE ${Libunwind_LIBRARY_DIRS})
target_include_directories(trace_runtime PRIVATE ${Libunwind_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

add_library(libssbd SHARED
  libssbd.c
)

