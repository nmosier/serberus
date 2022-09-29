include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_Declare(
  google-benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.7.0
)
FetchContent_MakeAvailable(google-benchmark)

# set(BENCHMARK_ENABLE_TESTING OFF)

if(0)
include(ExternalProject)
ExternalProject_Add(
  z3
  GIT_REPOSITORY https://github.com/Z3Prover/z3.git
  GIT_TAG z3-4.11.2
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
