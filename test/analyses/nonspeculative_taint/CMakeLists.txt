add_library(NonspeculativeTaintTest SHARED NonspeculativeTaintTest.cc)
register_llvm_pass(NonspeculativeTaintTest)
target_link_libraries(NonspeculativeTaintTest PRIVATE NonspeculativeTaintAnalysis)
target_include_directories(NonspeculativeTaintTest PRIVATE ${LLVM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src)
target_compile_options(NonspeculativeTaintTest PRIVATE -fno-rtti)

add_custom_command(OUTPUT xorbuf.ll
  COMMAND ${LLVM_BINARY_DIR}/bin/clang -emit-llvm -S ${CMAKE_CURRENT_SOURCE_DIR}/xorbuf.c -o xorbuf.ll
  DEPENDS xorbuf.c ${LLVM_BINARY_DIR}/bin/clang
)

add_test(NAME nonspeculative_taint_xorbuf_check
  COMMAND ${LLVM_BINARY_DIR}/bin/opt --enable-new-pm=0 --load=$<TARGET_FILE:NonspeculativeTaintTest> --clou-nonspeculative-taint-test --analyze xorbuf.ll
)

add_custom_command(OUTPUT xorbuf.out
  COMMAND ${LLVM_BINARY_DIR}/bin/opt --enable-new-pm=0 --load=$<TARGET_FILE:NonspeculativeTaintAnalysis> --clou-nonspeculative-taint-analysis --clou-test=xorbuf.out --analyze xorbuf.ll
  DEPENDS xorbuf.ll ${LLVM_BINARY_DIR}/bin/opt NonspeculativeTaintAnalysis
)

add_custom_target(nonspeculative_taint_xorbuf ALL
  DEPENDS xorbuf.out
)

add_test(NAME nonspeculative_taint_xorbuf
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/taint_diff.sh ${CMAKE_CURRENT_SOURCE_DIR}/xorbuf.exp xorbuf.out xorbuf.ll
)
