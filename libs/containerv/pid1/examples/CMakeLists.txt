# Example program for PID1 service
# This is optional and can be built separately to test the PID1 API

cmake_minimum_required(VERSION 3.10)

# Only build example if requested
option(BUILD_PID1_EXAMPLE "Build PID1 example program" OFF)

if(BUILD_PID1_EXAMPLE)
    add_executable(pid1_example
        pid1_example.c
    )

    target_link_libraries(pid1_example
        containerv-pid1
    )

    # Link pthread on Linux
    if(UNIX)
        target_link_libraries(pid1_example pthread)
    endif()

    # Set output directory
    set_target_properties(pid1_example PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    message(STATUS "PID1 example will be built")
endif()
