# Build platform library as the first target so others
# can link against it. The platform library must be independant
add_subdirectory(platform)

# Build utility libraries before the system libraries
add_subdirectory(vafs)
add_subdirectory(yaml)

# build zstd
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "Disable legacy support for zstd")
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Do not build zstd programs")
set(ZSTD_BUILD_CONTRIB  OFF CACHE BOOL "Do not build zstd contrib")
set(ZSTD_BUILD_TESTS    OFF CACHE BOOL "Do not build zstd tests")
add_subdirectory(zstd/build/cmake)

# build wolfssl
set(WOLFSSL_EXAMPLES OFF CACHE BOOL "Do not build wolfssl examples")
set(WOLFSSL_CRYPT_TESTS OFF CACHE BOOL "Do not build wolfssl crypt tests")
add_subdirectory(wolfssl)

# build curl
set(BUILD_CURL_EXE OFF CACHE BOOL "Do not build curl executable")
set(CURL_USE_WOLFSSL ON CACHE BOOL "Use wolfSSL")
add_subdirectory(curl)

# create an interface library for zstd
add_library(zstd-api INTERFACE)
target_include_directories(zstd-api INTERFACE zstd/lib)

# build support libraries for tools
add_subdirectory(oven)
