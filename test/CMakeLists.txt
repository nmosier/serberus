if(NOT DEFINED LLSCT_LOOP_WEIGHT)
  set(LLSCT_LOOP_WEIGHT 1.0)
endif()
if(NOT DEFINED LLSCT_DOM_WEIGHT)
  set(LLSCT_DOM_WEIGHT 1.0)
endif()
if(NOT DEFINED LLSCT_ST_WEIGHT)
  set(LLSCT_ST_WEIGHT 1.0)
endif()

if(NOT DEFINED TEST_DEBUG)
  set(TEST_DEBUG Off)
endif()

add_subdirectory(libsodium)
add_subdirectory(openssl)
add_subdirectory(hacl)
add_subdirectory(litmus)

