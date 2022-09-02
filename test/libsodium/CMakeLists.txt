function(libsodium_correctness_test NAME)
  set(multi_value_args DEPENDS PASS)
  cmake_parse_arguments(LIBSODIUM "" "" "${multi_value_args}" ${ARGN})
  set(LIBSODIUM libsodium_${NAME})

  # Add arguments for pass
  set(cppflags -flegacy-pass-manager)
  foreach(pass IN LISTS LIBSODIUM_PASS)
    list(APPEND cppflags -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND LIBSODIUM_DEPENDS ${pass})
  endforeach()
  list(JOIN cppflags " " cppflags)
  
  ExternalProject_Add(${LIBSODIUM}
    URL https://download.libsodium.org/libsodium/releases/libsodium-1.0.18.tar.gz
    CONFIGURE_COMMAND ../${LIBSODIUM}/configure --quiet --prefix=${CMAKE_CURRENT_BINARY_DIR}/${LIBSODIUM}-prefix CC=${LLVM_BINARY_DIR}/bin/clang "CPPFLAGS=${cppflags}" "CFLAGS=-O1"
    BUILD_COMMAND ${MAKE_EXE} --quiet && ${MAKE_EXE} --quiet check # TESTS_ENVIRONMENT="true" # Don't actually run any tests; just want to build the runners.
    INSTALL_COMMAND ${MAKE_EXE} --quiet install
  )
  ExternalProject_Get_Property(${LIBSODIUM} BINARY_DIR)  
  ExternalProject_Add_Step(${LIBSODIUM} clean
    COMMAND make --quiet clean
    DEPENDEES configure
    DEPENDERS build
    WORKING_DIRECTORY ${BINARY_DIR}
  )
  ExternalProject_Add_StepDependencies(${LIBSODIUM} clean ${LIBSODIUM_DEPENDS}) # TODO: Merge this into Add_Step above?
  add_test(NAME ${NAME}
    COMMAND ${MAKE_EXE} check 
    WORKING_DIRECTORY ${BINARY_DIR}
  )
endfunction()

libsodium_correctness_test(libsodium_correctness_noop)

foreach(PASS IN ITEMS ZeroFillCalls SpectreV1_1 SpeculativeTaint)
  libsodium_correctness_test(libsodium_correctness_${PASS}
    PASS ${PASS}
  )
endforeach()


