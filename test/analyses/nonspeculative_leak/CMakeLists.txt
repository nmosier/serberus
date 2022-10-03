
add_custom_command(OUTPUT xorbuf.ll
  COMMAND ${LLVM_BINARY_DIR}/bin/clang -emit-llvm -S ${CMAKE_CURRENT_SOURCE_DIR}/xorbuf.c -o xorbuf.ll
  DEPENDS xorbuf.c ${LLVM_BINARY_DIR}/bin/clang
)

add_custom_command(OUTPUT xorbuf.out
  COMMAND ${LLVM_BINARY_DIR}/bin/opt --enable-new-pm=0 --load=$<TARGET_FILE:NonspeculativeLeakAnalysis> --clou-nonspeculative-leak --clou-test=xorbuf.out --analyze xorbuf.ll
  DEPENDS xorbuf.ll ${LLVM_BINARY_DIR}/bin/opt NonspeculativeLeakAnalysis
)

add_custom_target(nonspeculative_leak_xorbuf ALL
  DEPENDS xorbuf.out
)

add_test(NAME nonspeculative_leak_xorbuf
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/taint_diff.sh ${CMAKE_CURRENT_SOURCE_DIR}/xorbuf.exp xorbuf.out xorbuf.ll
)
