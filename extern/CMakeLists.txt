include(FetchContent)

set(FETCHCONTENT_QUIET Off)


macro(declare name)
  FetchContent_Declare(
    ${name}
    ${ARGN}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ""
  )
  FetchContent_MakeAvailable(${name})
  FetchContent_GetProperties(${name} POPULATED populated)
  if(NOT ${populated})
    message(FATAL_ERROR "${name} not populated")
  endif()
endmacro()

FetchContent_Declare(Hacl
  GIT_REPOSITORY https://github.com/hacl-star/hacl-star.git
  GIT_TAG 0ea88bcfc3f6da97dc3f8eb46bf44816ddcf3da8
  # GIT_TAG ocaml-v0.4.5
  # GIT_SHALLOW 1
  GIT_PROGRESS On
  # DOWNLOAD_NO_PROGRESS Off
  # URL https://github.com/hacl-star/hacl-star/archive/refs/tags/ocaml-v0.4.5.tar.gz
)  
FetchContent_MakeAvailable(Hacl)

if(DEFINED LIBSODIUM_DIR)
  declare(Libsodium
    SOURCE_DIR ${LIBSODIUM_DIR}
  )
else()
  declare(Libsodium
    # DOWNLOAD_NO_PROGRESS Off
    # URL https://github.com/jedisct1/libsodium/releases/download/1.0.18-RELEASE/libsodium-1.0.18.tar.gz
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG e2bd4024008c73f1c0402e1c2f4db9a2300e9c95
    GIT_PROGRESS On
  )
endif()

if(DEFINED OPENSSL_DIR)
  declare(OpenSSL
    SOURCE_DIR ${OPENSSL_DIR}
  )
else()
  declare(OpenSSL
    GIT_REPOSITORY https://github.com/openssl/openssl.git
    GIT_TAG openssl-3.0.5
    GIT_PROGRESS On
    GIT_SHALLOW On
  )    
endif()

if(DEFINED PIN_DIR)
  FetchContent_Declare(Pin
    SOURCE_DIR ${PIN_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ""
  )
else()
  FetchContent_Declare(Pin
    URL https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.26-98690-g1fc9d60e6-gcc-linux.tar.gz
    DOWNLOAD_NO_PROGRESS Off
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ""
  )
endif()
FetchContent_MakeAvailable(Pin)
FetchContent_GetProperties(Pin SOURCE_DIR Pin_SOURCE_DIR)
set(PIN_DIR ${Pin_SOURCE_DIR} CACHE PATH "Path to Pin source directory")

block()
  
  set(BENCHMARK_ENABLE_TESTING OFF)
  set(CMAKE_BUILD_TYPE Release)
  set(CMAKE_C_FLAGS -fcf-protection=branch)
  set(CMAKE_CXX_FLAGS -fcf-protection=branch)
  add_link_options(-Wl,-z,ibt)
  FetchContent_Declare(
    google-benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG main
    GIT_PROGRESS On
    # CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
  )
  FetchContent_MakeAvailable(google-benchmark)
endblock()
