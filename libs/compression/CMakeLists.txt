
# build zstd
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "Disable legacy support for zstd")
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Do not build zstd programs")
set(ZSTD_BUILD_CONTRIB  OFF CACHE BOOL "Do not build zstd contrib")
set(ZSTD_BUILD_TESTS    OFF CACHE BOOL "Do not build zstd tests")
add_subdirectory(zstd/build/cmake)

# create an interface library for zstd
add_library(zstd-api INTERFACE)
target_include_directories(zstd-api INTERFACE zstd/lib)

# build zip
add_subdirectory(zip)
