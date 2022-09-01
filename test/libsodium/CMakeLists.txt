set(cppflags "-flegacy-pass-manager -Xclang -load -Xclang $<TARGET_FILE:ZeroFillCalls>")
ExternalProject_Add(libsodium
  URL https://download.libsodium.org/libsodium/releases/libsodium-1.0.18.tar.gz
  CONFIGURE_COMMAND ../libsodium/configure --quiet --prefix=${CMAKE_CURRENT_BINARY_DIR}/libsodium-prefix --disable-asm CC=${LLVM_BINARY_DIR}/bin/clang && make --quiet clean # cleaning necessary for autotooled build configs
  "CPPFLAGS=${cppflags}"
  BUILD_COMMAND ${MAKE_EXE} --quiet
  # INSTALL_COMMAND -- skip for now, since we want to run tests in the build folder.
  DEPENDS ZeroFillCalls
)
ExternalProject_Get_Property(libsodium BINARY_DIR)

add_test(NAME libsodium_correctness
  COMMAND ${MAKE_EXE} check -j
  WORKING_DIRECTORY ${BINARY_DIR}
)

