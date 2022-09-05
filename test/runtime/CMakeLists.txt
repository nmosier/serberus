add_executable(clou_transform_xor
  clou_transform_xor.c
)
target_link_libraries(clou_transform_xor PRIVATE clou_runtime)
target_include_directories(clou_transform_xor PRIVATE ${CMAKE_SOURCE_DIR}/runtime)

add_test(NAME clou_transform_xor
  COMMAND clou_transform_xor
)

