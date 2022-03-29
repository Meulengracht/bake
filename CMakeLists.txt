cmake_minimum_required(VERSION 3.14.3)
project(chef C)

option(CHEF_BUILD_BAKE "Build the bake executable" ON)
option(CHEF_BUILD_ORDER "Build the order executable" ON)
option(CHEF_BUILD_SERVE "Build the serve executable" ON)

add_subdirectory(protocols)
add_subdirectory(libs)
add_subdirectory(tools)

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
