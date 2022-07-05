function(register_llvm_pass TARGET)
  target_include_directories(${TARGET} SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(${TARGET} PRIVATE ${LLVM_DEFINITIONS})
  if(APPLE)
    target_link_options(${TARGET} PRIVATE -undefined dynamic_lookup)
  endif()
endfunction()

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${Libprofiler_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
link_directories(${Libprofiler_LIBRARY_DIRS})
link_libraries(${Libprofiler_LIBRARIES})

add_library(graph STATIC
  graph.cc
  graph.h
  )

add_library(min_cut STATIC
  min-cut.cc
  min-cut.h
  )

add_library(speculative_taint SHARED
  speculative_taint.cc
  util.cc
  util.h
  )
register_llvm_pass(speculative_taint)
target_link_libraries(speculative_taint PRIVATE min_cut)

add_library(count_addr_stores SHARED
  count_addr_stores.cc
  )
register_llvm_pass(count_addr_stores)

add_library(count_spectre_v1_1 SHARED
  count_spectre_v1_1.cc
  util.cc
  util.h
  )
register_llvm_pass(count_spectre_v1_1)

add_library(mitigate SHARED
  mitigate.cc
  util.cc
  )
register_llvm_pass(mitigate)
