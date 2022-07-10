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

add_library(my_scc_pass STATIC
  my_scc_pass.cc
  my_scc_pass.h
  )

add_library(util STATIC
  util.cc
  util.h
  )

add_library(graph STATIC
  graph.cc
  graph.h
  )

add_library(min_cut STATIC
  min-cut.cc
  min-cut.h
  )

add_library(transmitter STATIC
  transmitter.cc
  transmitter.h
  )

add_library(speculative_taint SHARED
  speculative_taint.cc
  )
register_llvm_pass(speculative_taint)
target_link_libraries(speculative_taint PRIVATE min_cut transmitter util)

add_library(count_addr_stores SHARED
  count_addr_stores.cc
  )
register_llvm_pass(count_addr_stores)

add_library(count_spectre_v1_1 SHARED
  count_spectre_v1_1.cc
  )
register_llvm_pass(count_spectre_v1_1)
target_link_libraries(count_spectre_v1_1 PRIVATE util)

add_library(mitigate SHARED
  mitigate.cc
  )
register_llvm_pass(mitigate)
target_link_libraries(mitigate PRIVATE util)

add_executable(min_cut_driver
  min-cut-driver.cc
  )
target_link_libraries(min_cut_driver PRIVATE min_cut)

add_library(print_dom_loop SHARED
  print_dom_loop.cc
  )
register_llvm_pass(print_dom_loop)
target_link_libraries(print_dom_loop PRIVATE util)

add_library(secret_param_analysis SHARED
  secret_param_analysis.cc
  )
register_llvm_pass(secret_param_analysis)
target_link_libraries(secret_param_analysis PRIVATE util transmitter my_scc_pass)
