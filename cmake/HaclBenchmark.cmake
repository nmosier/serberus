function(hacl_benchmark NAME BENCHSRC SRC)
  set(multi_value_args CFLAGS LDFLAGS LLVMFLAGS PASSES DEPENDS)
  cmake_parse_arguments(Hacl "" "" "${multi_value_args}" ${ARGN})

  set(OBJ ${CMAKE_CURRENT_BINARY_DIR}/lib${NAME}.so)

  list(APPEND Hacl_CFLAGS -fcf-protection=branch)
  list(APPEND Hacl_LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:cet> -L$<TARGET_FILE_DIR:cet> -lcet -Wl,-z,ibt)
  list(APPEND Hacl_DEPENDS cet)

  make_directory(${CMAKE_CURRENT_BINARY_DIR}/${NAME}_logs)
  list(APPEND Hacl_LLVMFLAGS -clou-log=${NAME}_logs)

  foreach(pass IN LISTS Hacl_PASSES)
    list(APPEND Hacl_CFLAGS -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND Hacl_DEPENDS ${pass})
  endforeach()

  list(APPEND Hacl_CFLAGS -Wno-pedantic -Wno-unused-parameter)

  foreach(flag IN LISTS Hacl_LLVMFLAGS)
    list(APPEND Hacl_CFLAGS -mllvm ${flag})
  endforeach()

  FetchContent_GetProperties(Hacl SOURCE_DIR Hacl_SOURCE_DIR)
  set(Hacl_INCLUDE_DIRS ${Hacl_SOURCE_DIR}/dist/gcc-compatible ${Hacl_SOURCE_DIR}/dist/karamel/include ${Hacl_SOURCE_DIR}/dist/karamel/krmllib/dist/minimal)

  foreach(inc IN LISTS Hacl_INCLUDE_DIRS)
    list(APPEND Hacl_CFLAGS -I ${inc})
  endforeach()
  
  add_custom_command(OUTPUT ${OBJ}
    COMMAND rm -f ${NAME}_logs/*
    COMMAND ${LLVM_BINARY_DIR}/bin/clang -flegacy-pass-manager -fPIE ${Hacl_CFLAGS} ${Hacl_LDFLAGS} ${Hacl_SOURCE_DIR}/dist/gcc-compatible/${SRC} -o ${OBJ} -fuse-ld=${LLVM_BINARY_DIR}/bin/ld.lld -shared
    DEPENDS ${Hacl_DEPENDS} ${LLVM_BINARY_DIR}/bin/clang ${LLVM_BINARY_DIR}/bin/ld.lld ${Hacl_SOURCE_DIR}/dist/gcc-compatible/${SRC}
  )

  add_custom_target(${NAME}_obj
    DEPENDS ${OBJ}
  )
  add_library(${NAME}_so INTERFACE)
  target_link_libraries(${NAME}_so INTERFACE ${OBJ})
  target_include_directories(${NAME}_so INTERFACE ${Hacl_INCLUDE_DIRS})
  add_dependencies(${NAME}_so ${NAME}_obj)
  
  add_executable(${NAME} ${BENCHSRC} ${OBJ})
  target_link_libraries(${NAME} PRIVATE benchmark::benchmark ${NAME}_so)
  # target_link_directories(${NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_include_directories(${NAME} PRIVATE ${Hacl_INCLUDE_DIRS})
    
endfunction()

