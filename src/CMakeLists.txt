function(register_llvm_pass TARGET)
  target_include_directories(${TARGET} SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(${TARGET} PRIVATE ${LLVM_DEFINITIONS})
  if(APPLE)
    target_link_options(${TARGET} PRIVATE -undefined dynamic_lookup)
  endif()
endfunction()

add_library(speculative_taint SHARED
  speculative_taint.cc
  util.cc
  util.h
  )
register_llvm_pass(speculative_taint)

