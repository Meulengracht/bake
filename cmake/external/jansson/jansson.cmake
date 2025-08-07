add_library(jansson INTERFACE)

option(FORCE_BUNDLED_JANSSON "Always build jansson, instead of using the system version" OFF)
if (NOT FORCE_BUNDLED_JANSSON)
  find_library(JANSSON_LIB NAMES jansson)
  find_path(JANSSON_INCLUDE_DIR NAMES jansson.h)

  if(NOT DEFINED CAN_USE_SYSTEM_JANSSON)
    set(CAN_USE_SYSTEM_JANSSON OFF)
    set(CMAKE_REQUIRED_INCLUDES "${jansson_INCLUDE_DIR}")
    check_cxx_source_compiles("
  #include <jansson.h>


    int main() {
    static_assert(JANSSON_MAJOR_VERSION == 2);
    static_assert(JANSSON_MINOR_VERSION >= 14);
      return 0;
    }
    " CAN_USE_SYSTEM_JANSSON)
    set(CMAKE_REQUIRED_INCLUDES)
  endif()

  if (CAN_USE_SYSTEM_JANSSON)
    message(STATUS "Using system jansson")
    target_include_directories(jansson INTERFACE "${JANSSON_INCLUDE_DIR}")
    target_link_libraries(jansson INTERFACE "${JANSSON_LIB}")
    return ()
  endif ()
endif ()

message(STATUS "Building jansson from source")
include(ExternalProject)

ExternalProject_Add(bundled_jansson
  URL
    https://github.com/akheron/jansson/archive/refs/tags/v2.14.1.tar.gz
  CONFIGURE_COMMAND
    ${CMAKE_COMMAND}
    <SOURCE_DIR>
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_INCLUDEDIR=include
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DJANSSON_BUILD_SHARED_LIBS=OFF
    -DJANSSON_EXAMPLES=OFF
    -DJANSSON_BUILD_DOCS=OFF
    -DJANSSON_BUILD_MAN=OFF
    -DJANSSON_WITHOUT_TESTS=ON
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}

    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
)
add_dependencies(jansson bundled_jansson)
ExternalProject_Get_Property(bundled_jansson INSTALL_DIR)
target_include_directories(jansson INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(jansson INTERFACE "${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jansson${CMAKE_STATIC_LIBRARY_SUFFIX}")
