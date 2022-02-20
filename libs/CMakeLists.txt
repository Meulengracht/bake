# Build platform library as the first target so others
# can link against it. The platform library must be independant
add_subdirectory(platform)

# Build utility libraries before the system libraries
add_subdirectory(vafs)
add_subdirectory(yaml)

# build zstd
set(ZSTD_LEGACY_SUPPORT CACHE BOOL OFF "Disable legacy support for zstd")
set(ZSTD_BUILD_PROGRAMS CACHE BOOL OFF "Do not build zstd programs")
set(ZSTD_BUILD_CONTRIB  CACHE BOOL OFF "Do not build zstd contrib")
set(ZSTD_BUILD_TESTS    CACHE BOOL OFF "Do not build zstd tests")
add_subdirectory(zstd/build/cmake)

# build support libraries for tools
add_subdirectory(oven)
