add_library(zstd INTERFACE)

option(FORCE_BUNDLED_ZSTD "Always build zstd, instead of using the system version" OFF)
if (NOT FORCE_BUNDLED_ZSTD)
  find_library(ZSTD_LIB NAMES zstd)
  find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)

  if(NOT DEFINED CAN_USE_SYSTEM_ZSTD)
    set(CAN_USE_SYSTEM_ZSTD OFF)
    set(CMAKE_REQUIRED_INCLUDES "${ZSTD_INCLUDE_DIR}")
    check_cxx_source_compiles("
  #include <zstd.h>


    int main() {
    static_assert(ZSTD_VERSION_MAJOR == 1);
    static_assert(ZSTD_VERSION_MINOR >= 5);
      return 0;
    }
    " CAN_USE_SYSTEM_ZSTD)
    set(CMAKE_REQUIRED_INCLUDES)
  endif()

  if (CAN_USE_SYSTEM_ZSTD)
    message(STATUS "Using system zstd")
    target_include_directories(zstd INTERFACE "${ZSTD_INCLUDE_DIR}")
    target_link_libraries(zstd INTERFACE "${ZSTD_LIB}")
    return ()
  endif ()
endif ()

message(STATUS "Building zstd from source")
include(ExternalProject)

# zstd's CMake exports the static library under different basenames depending
# on platform/toolchain. On Windows the installed static library is typically
# named "zstd_static.lib", while on Unix it is usually "libzstd.a".
if (WIN32)
  set(ZSTD_BUNDLED_LIB_BASENAME zstd_static)
else ()
  set(ZSTD_BUNDLED_LIB_BASENAME zstd)
endif ()

ExternalProject_Add(bundled_zstd
  URL
    https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
  CONFIGURE_COMMAND
    ${CMAKE_COMMAND}
    <SOURCE_DIR>/build/cmake
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_INCLUDEDIR=include
    -DCMAKE_INSTALL_LIBDIR=lib
    -DZSTD_BUILD_STATIC=ON
    -DZSTD_BUILD_SHARED=OFF
    -DZSTD_BUILD_PROGRAMS=OFF

    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
  BUILD_BYPRODUCTS
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${ZSTD_BUNDLED_LIB_BASENAME}${CMAKE_STATIC_LIBRARY_SUFFIX}
  BUILD_COMMAND
    ${CMAKE_COMMAND} --build . --config $<CONFIG>
  INSTALL_COMMAND
    ${CMAKE_COMMAND} --build . --target install --config $<CONFIG>
)
add_dependencies(zstd bundled_zstd)
ExternalProject_Get_Property(bundled_zstd INSTALL_DIR)
target_include_directories(zstd INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(zstd INTERFACE "${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${ZSTD_BUNDLED_LIB_BASENAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")
