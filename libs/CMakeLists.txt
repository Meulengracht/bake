# Build platform library as the first target so others
# can link against it. The platform library must be independant
add_subdirectory(platform)
add_subdirectory(vlog)

# Build utility libraries before the system libraries
set(VAFS_BUILD_TOOLS OFF CACHE BOOL "Do not build vafs utilities")
add_subdirectory(vafs)
add_subdirectory(yaml)
add_subdirectory(gracht)

# build zstd
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "Disable legacy support for zstd")
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Do not build zstd programs")
set(ZSTD_BUILD_CONTRIB  OFF CACHE BOOL "Do not build zstd contrib")
set(ZSTD_BUILD_TESTS    OFF CACHE BOOL "Do not build zstd tests")
add_subdirectory(zstd/build/cmake)

# create an interface library for zstd
add_library(zstd-api INTERFACE)
target_include_directories(zstd-api INTERFACE zstd/lib)

# build jansson for json requests and responses
set(JANSSON_BUILD_DOCS OFF CACHE BOOL "Do not build jansson documentation")
set(JANSSON_EXAMPLES OFF CACHE BOOL "Do not build jansson examples")
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "Do not build jansson tests")
add_subdirectory(jansson)

# create an interface library for jansson
add_library(jansson-api INTERFACE)
target_include_directories(jansson-api INTERFACE ${CMAKE_BINARY_DIR}/libs/jansson/include)

# build wolfssl
set(WOLFSSL_OPENSSLEXTRA ON CACHE BOOL "Enable OpenSSL compatibility")
set(WOLFSSL_EXAMPLES OFF CACHE BOOL "Do not build wolfssl examples")
set(WOLFSSL_CRYPT_TESTS OFF CACHE BOOL "Do not build wolfssl crypt tests")
add_subdirectory(wolfssl)

# build curl
set(BUILD_CURL_EXE OFF CACHE BOOL "Do not build curl executable")
set(CURL_USE_WOLFSSL ON CACHE BOOL "Use wolfSSL")
add_subdirectory(curl)

# build libchefclient
add_subdirectory(chefclient)

# build support libraries for tools
add_subdirectory(oven)
add_subdirectory(fridge)
