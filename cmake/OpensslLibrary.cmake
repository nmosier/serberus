function(openssl_library NAME)
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS CLOUFLAGS CONFIGURE_OPTIONS)
  cmake_parse_arguments(OPENSSL "" "" "${multi_value_args}" ${ARGN})

  list(APPEND OPENSSL_CFLAGS -flegacy-pass-manager)

  foreach(pass IN LISTS OPENSSL_PASS)
    list(APPEND OPENSSL_CFLAGS -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND OPENSSL_DEPENDS ${pass})
  endforeach()

  foreach(llvmflag IN LISTS OPENSSL_LLVMFLAGS)
    list(APPEND OPENSSL_LLVMFLAGS -mllvm ${llvmflag})
  endforeach()

  foreach(clouflag IN LISTS OPENSSL_CLOUFLAGS)
    list(APPEND OPENSSL_CLOUFLAGS -mllvm ${llvmflag})
  endforeach()

  list(APPEND OPENSSL_LDFLAGS -pthread) # Not sure why
  
  list(JOIN OPENSSL_CFLAGS " " OPENSSL_CFLAGS)
  list(JOIN OPENSSL_CPPFLAGS " " OPENSSL_CPPFLAGS)
  list(JOIN OPENSSL_LDFLAGS " " OPENSSL_LDFLAGS)

  ExternalProject_Add(${NAME}
    SOURCE_DIR ${OPENSSL_DIR}
    CONFIGURE_COMMAND ${OPENSSL_DIR}/Configure "--prefix=${CMAKE_CURRENT_BINARY_DIR}/${NAME}-prefix" "CC=${LLVM_BINARY_DIR}/bin/clang" "LD=${LLVM_BINARY_DIR}/bin/ld.lld" "CPPFLAGS=${OPENSSL_CPPFLAGS}" "CFLAGS=${OPENSSL_CFLAGS}" "LDFLAGS=${OPENSSL_LDFLAGS}" ${OPENSSL_CONFIGURE_OPTIONS}
    BUILD_COMMAND ${MAKE_EXE} -j64 2>&1 | tee ${NAME}.build.log
    INSTALL_COMMAND ${MAKE_EXE} --quiet install
  )
  ExternalProject_Get_Property(${NAME} BINARY_DIR)
  ExternalProject_Add_Step(${NAME} clean
    COMMAND make --quiet clean
    DEPENDEES configure
    DEPENDERS build
    WORKING_DIRECTORY ${BINARY_DIR}
  )
  ExternalProject_Add_StepDependencies(${NAME} clean ${OPENSSL_DEPENDS})

  add_test(${NAME}_test
    COMMAND ${MAKE_EXE} test
    WORKING_DIRECTORY ${BINARY_DIR}
  )

endfunction()

