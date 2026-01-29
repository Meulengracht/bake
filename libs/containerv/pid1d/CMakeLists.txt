cmake_minimum_required(VERSION 3.14.3)

# pid1d: small guest-side supervisor used as PID1-equivalent inside VMs
add_executable(pid1d
    pid1d.c
)

target_link_libraries(pid1d PRIVATE containerv-pid1 jansson)

target_include_directories(pid1d PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Match existing convention for tool binaries
set_target_properties(pid1d PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
