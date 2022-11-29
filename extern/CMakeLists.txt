include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_FLAGS -fcf-protection=branch)
set(CMAKE_CXX_FLAGS -fcf-protection=branch)
add_link_options(-Wl,-z,ibt)
FetchContent_Declare(
  google-benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG main
  # CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
)
FetchContent_MakeAvailable(google-benchmark)

FetchContent_Declare(
  Hacl
  GIT_REPOSITORY https://github.com/hacl-star/hacl-star.git
  GIT_TAG 0ea88bcfc3f6da97dc3f8eb46bf44816ddcf3da8
  CONFIGURE_COMMAND
  BUILD_COMMAND
  TEST_COMMAND
  INSTALL_COMMAND
)
FetchContent_MakeAvailable(Hacl)

