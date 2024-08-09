cmake_minimum_required(VERSION 3.14.3)
project(chef VERSION "1.3.0")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

option(CHEF_BUILD_BAKE "Build the bake executable" ON)
option(CHEF_BUILD_ORDER "Build the order executable" ON)
option(CHEF_BUILD_SERVE "Build the serve executable" ON)
option(CHEF_BUILD_AS_SNAP "Build binaries without setuid on unix" OFF)

include_directories(${CMAKE_BINARY_DIR})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/chef-config.h.in"
               "${CMAKE_BINARY_DIR}/chef-config.h"
               @ONLY
)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(protocols)
add_subdirectory(libs)
add_subdirectory(tools)
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
