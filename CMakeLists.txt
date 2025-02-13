cmake_minimum_required(VERSION 3.14.3)
project(chef VERSION "1.4.0")

# on ubuntu 24+ wolfssl craps out on some string operations which
# we do not care about
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-stringop-overflow")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

option(CHEF_BUILD_TOOLS "Build the bake toolset" ON)
option(CHEF_BUILD_RUNTIME "Build the serve daemon" ON)
option(CHEF_BUILD_SERVER "Build the cook build server" ON)
option(CHEF_BUILD_MANAGER "Build the waiter build server manager" ON)
option(CHEF_BUILD_AS_SNAP "Build binaries without setuid on unix" OFF)

include_directories(${CMAKE_BINARY_DIR})

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/chef-config.h.in"
    "${CMAKE_BINARY_DIR}/chef-config.h"
    @ONLY
)

# fetch external libraries
include(${CMAKE_SOURCE_DIR}/cmake/external/lua/lua54.cmake)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

if(CHEF_BUILD_AS_SNAP)
    add_definitions(-DCHEF_AS_SNAP)
endif()

# always register all the protocols and libs
add_subdirectory(protocols)
add_subdirectory(libs)

# if requested, build the toolset
if(CHEF_BUILD_TOOLS)
    add_subdirectory(tools)
endif(CHEF_BUILD_TOOLS)

add_subdirectory(daemons)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY
)

if(NOT TARGET uninstall)
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()
