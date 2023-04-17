include(GeneratorExpressionToString)

function(add_driver NAME)
  set(multi_value_args PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS)
  set(one_value_args EXE)
  cmake_parse_arguments(DRIVER "" "${one_value_args}" "${multi_value_args}" ${ARGN})

  set(passes_genexprs )
  foreach(pass IN LISTS DRIVER_PASS)
    list(APPEND passes_genexprs $<TARGET_FILE:${pass}>)
  endforeach()
  generator_expression_to_string(passes "${passes_genexprs}")
  
endfunction()
    
    






      
