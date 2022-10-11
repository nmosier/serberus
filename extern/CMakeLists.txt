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

# set(BENCHMARK_ENABLE_TESTING OFF)

if(0)
include(ExternalProject)
ExternalProject_Add(
  z3
  GIT_REPOSITORY https://github.com/Z3Prover/z3.git
  GIT_BRANCH main
)
ExternalProject_Get_Property(z3 install_dir)
set(Z3_LIBRARIES z3)
set(Z3_LIBRARY_DIRS ${install_dir}/lib)
set(Z3_INCLUDE_DIRS ${install_dir}/include)
add_library(libz3 INTERFACE)
target_link_libraries(libz3 INTERFACE z3)
target_link_directories(libz3 INTERFACE ${install_dir}/lib)
target_include_directories(libz3 INTERFACE ${install_dir}/include)
add_dependencies(lizb3 z3)
endif()
