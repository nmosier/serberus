add_link_options(-fPIE -fPIC -pthread)

function(add_benchmark NAME SRC LIB INC)
  add_executable(${NAME} ${SRC})
  target_link_libraries(${NAME} PRIVATE ${LIB} benchmark::benchmark)
  target_include_directories(${NAME} PRIVATE ${INC})
endfunction()

ExternalProject_Get_Property(libsodium_cloucc_lfence install_dir)
add_benchmark(cloucc_lfence_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_cloucc_fps install_dir)
add_benchmark(cloucc_fps_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_baseline_slh install_dir)
add_benchmark(baseline_slh_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_baseline_lfence install_dir)
add_benchmark(baseline_lfence_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_baseline_none install_dir)
add_benchmark(baseline_none_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)

ExternalProject_Get_Property(libsodium_ncsrs install_dir)
add_benchmark(ncsrs_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)
