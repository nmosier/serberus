add_link_options(-fPIE -fPIC -pthread)

configure_file(breakdown.py.in breakdown.py)

foreach(benchspec IN ITEMS baseline_breakdown cloucc_breakdown)
  file(GENERATE
    OUTPUT ${benchspec}.json
    INPUT ${benchspec}.json.in
  )
endforeach()

function(add_benchmark NAME SRC LIB INC)
  message(LIB=${LIB})
  add_executable(${NAME} ${SRC})
  target_link_libraries(${NAME} PRIVATE ${LIB} benchmark::benchmark)
  add_custom_command(OUTPUT ${NAME}.json
    COMMAND ${NAME} --benchmark_out=${NAME}.json --benchmark_out_format=json
    DEPENDS ${NAME}
  )
  add_custom_target(${NAME}_json
    DEPENDS ${NAME}.json
  )
endfunction()

set(libsodium_benchmark_libraries
  libsodium_baseline_none
  libsodium_baseline_lfence
  libsodium_baseline_slh
  libsodium_baseline_retpoline
  libsodium_baseline_ssbd
  libsodium_baseline_lfence+retpoline+ssbd
  libsodium_baseline_slh+retpoline+ssbd
  libsodium_cloucc_udt
  libsodium_cloucc_ncas
  libsodium_cloucc_fps
  libsodium_cloucc_prech
  libsodium_cloucc
)

foreach(libsodium_benchmark_library IN LISTS libsodium_benchmark_libraries)
  # ExternalProject_Get_Property(${libsodium_benchmark_library} install_dir)
  string(REGEX REPLACE "^libsodium_" "" name ${libsodium_benchmark_library})
  add_benchmark(${name}_benchmark libsodium_sha256.cc ${libsodium_benchmark_library} ${install_dir}/include)
endforeach()

# ExternalProject_Get_Property(libsodium_cloucc install_dir)
# add_benchmark(cloucc_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)
# 
# ExternalProject_Get_Property(libsodium_baseline_slh install_dir)
# add_benchmark(baseline_slh_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)
# 
# ExternalProject_Get_Property(libsodium_baseline_lfence install_dir)
# add_benchmark(baseline_lfence_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)
# 
# ExternalProject_Get_Property(libsodium_baseline_none install_dir)
# add_benchmark(baseline_none_benchmark libsodium_sha256.cc ${install_dir}/lib/libsodium.so ${install_dir}/include)
