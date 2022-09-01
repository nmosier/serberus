add_compile_options(-fno-rtti)

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

add_library(util OBJECT
  util.cc
  util.h
)

add_library(InstructionPass OBJECT
  InstructionPass.cc
  InstructionPass.h
)
register_llvm_pass(InstructionPass)

add_library(min_cut OBJECT
  min-cut.cc
  min-cut.h
  )

add_library(transmitter OBJECT
  transmitter.cc
  transmitter.h
  )

add_library(SpeculativeTaint SHARED
  SpeculativeTaint.cc
  $<TARGET_OBJECTS:min_cut>
  $<TARGET_OBJECTS:transmitter>
  util
  )
register_llvm_pass(SpeculativeTaint)

add_library(count_addr_stores SHARED
  count_addr_stores.cc
  )
register_llvm_pass(count_addr_stores)

add_library(count_spectre_v1_1 SHARED
  count_spectre_v1_1.cc
  util
  )
register_llvm_pass(count_spectre_v1_1)

add_library(mitigate SHARED
  mitigate.cc
  util
  )
register_llvm_pass(mitigate)

add_executable(min_cut_driver
  min-cut-driver.cc
  $<TARGET_OBJECTS:min_cut>
  )

add_library(print_dom_loop SHARED
  print_dom_loop.cc
  util
  )
register_llvm_pass(print_dom_loop)

add_library(secret_param_analysis SHARED
  secret_param_analysis.cc
  util
  transmitter
  my_scc_pass
  )
register_llvm_pass(secret_param_analysis)

add_library(NonspeculativeTaint SHARED
  NonspeculativeTaint.cc
  NonspeculativeTaint.h
  $<TARGET_OBJECTS:transmitter>
  $<TARGET_OBJECTS:util>
  )
register_llvm_pass(NonspeculativeTaint)
target_link_libraries(NonspeculativeTaint PUBLIC my_scc_pass)

add_library(SpectreV1_1 SHARED
  SpectreV1_1.cc
  $<TARGET_OBJECTS:util>
  NonspeculativeTaint
  $<TARGET_OBJECTS:transmitter>
  )
register_llvm_pass(SpectreV1_1)

add_library(ZeroFillCalls SHARED
  ZeroFillCalls.cc
  $<TARGET_OBJECTS:InstructionPass>
  $<TARGET_OBJECTS:util>
)
register_llvm_pass(ZeroFillCalls)
  
