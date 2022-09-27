add_link_options(-fPIE -fPIC -pthread)

function(add_benchmark NAME SRC LIB INC)
  add_executable(${NAME} ${SRC})
  target_link_libraries(${NAME} PRIVATE ${GoogleBenchmark_LIBRARIES} ${LIB})
  target_link_directories(${NAME} PRIVATE ${GoogleBenchmark_LIBRARY_DIRS})
  target_include_directories(${NAME} PRIVATE ${GoogleBenchmark_INCLUDE_DIRS} ${INC})
endfunction()

ExternalProject_Get_Property(libsodium_mitigate install_dir)
add_benchmark(cloucc_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)
add_benchmark(cloucc_cache libsodium_sha256_main.c ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_baseline_slh install_dir)
add_benchmark(baseline_slh_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_baseline_lfence install_dir)
add_benchmark(baseline_lfence_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_baseline_none install_dir)
add_benchmark(baseline_none_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)
add_benchmark(baseline_none_cache libsodium_sha256_main.c ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_fps install_dir)
add_benchmark(fps_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

