function(hacl_library NAME)
  # TODO: Make these shared def.
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS CLOUFLAGS CONFIGURE_OPTIONS)
  cmake_parse_arguments(HACL "" "" "${multi_value_args}" ${ARGN})

  set(PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NAME})
  set(BUILD_DIR ${PREFIX_DIR}/build)
  set(STAMP_DIR ${PREFIX_DIR}/stamp)
  set(INSTALL_DIR ${PREFIX_DIR})
  set(LOG_DIR ${PREFIX_DIR}/logs)

  make_directory(${INSTALL_DIR}/include)
  make_directory(${INSTALL_DIR}/lib)
  make_directory(${BUILD_DIR})
  make_directory(${LOG_DIR})

  # CET stuff
  list(APPEND HACL_LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:cet> -L$<TARGET_FILE_DIR:cet> -lcet)
  list(APPEND HACL_DEPENDS cet)
  list(APPEND HACL_CFLAGS -fcf-protection=branch)

  # Logging 
  list(APPEND HACL_LLVMFLAGS -clou-log=${LOG_DIR})

  # Leveling the playing field
  list(APPEND HACL_CFLAGS -flegacy-pass-manager)

  list(APPEND HACL_CFLAGS -fPIC -Wno-unused-parameter)

  # Convert PASSes to CFLAGS
  foreach(pass IN LISTS HACL_PASS)
    list(APPEND HACL_CFLAGS -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND HACL_DEPENDS ${pass})
  endforeach()

  foreach(llvmflag IN LISTS HACL_LLVMFLAGS)
    list(APPEND HACL_CFLAGS -mllvm ${llvmflag})
  endforeach()

  foreach(clouflag IN LISTS HACL_CLOUFLAGS)
    list(APPEND HACL_CPPFLAGS -mllvm ${clouflag})
  endforeach()

  list(APPEND HACL_LDFLAGS -pthread -fuse-ld=${LLVM_BINARY_DIR}/bin/ld.lld)
  list(APPEND HACL_DEPENDS ${LLVM_BINARY_DIR}/bin/ld.lld)

  FetchContent_GetProperties(Hacl SOURCE_DIR Hacl_SOURCE_DIR)

  set(output ${INSTALL_DIR}/lib/libhacl.so)
  set(inputs Hacl_Chacha20.c Hacl_Poly1305_32.c Hacl_Curve25519_51.c)
  list(TRANSFORM inputs PREPEND ${Hacl_SOURCE_DIR}/dist/gcc-compatible/)
  set(include_dirs ${Hacl_SOURCE_DIR}/dist/gcc-compatible ${Hacl_SOURCE_DIR}/dist/karamel/include ${Hacl_SOURCE_DIR}/dist/karamel/krmllib/dist/minimal)
    
  foreach(include_dir IN ITEMS gcc-compatible karamel/include karamel/krmllib/dist/minimal)
    list(APPEND HACL_CPPFLAGS -I ${Hacl_SOURCE_DIR}/dist/${include_dir})
  endforeach()
    
  # list(JOIN HACL_CFLAGS " " HACL_CFLAGS)
  # list(JOIN HACL_CPPFLAGS " " HACL_CPPFLAGS)
  # list(JOIN HACL_LDFLAGS " " HACL_LDFLAGS)
  
  add_custom_command(OUTPUT ${output}
    COMMAND ${LLVM_BINARY_DIR}/bin/clang ${HACL_CPPFLAGS} ${HACL_CFLAGS} ${HACL_LDFLAGS} -shared -o ${output} ${inputs}
    DEPENDS ${HACL_DEPENDS} ${LLVM_BINARY_DIR}/bin/clang ${inputs}
  )

  add_custom_target(${NAME}_so
    DEPENDS ${output}
  )
  add_library(${NAME} INTERFACE)
  target_link_libraries(${NAME} INTERFACE ${output})
  target_include_directories(${NAME} INTERFACE ${include_dirs})
  add_dependencies(${NAME} ${NAME}_so)

endfunction()
