# Linux reference implementation of the served daemon
find_package(FUSE REQUIRED)

# Try to build BPF programs
add_subdirectory(bpf)

add_library(containerv-linux STATIC
    cgroups.c
    container-options.c
    container.c
    control-socket.c
    layers.c
    network.c
    policy.c
    policy-ebpf.c
    policy-seccomp.c
    user.c
    utils.c
)
target_include_directories(containerv-linux PRIVATE ../include)
target_link_libraries(containerv-linux PUBLIC platform vlog vafs ${FUSE_LIBRARIES})
target_link_libraries(containerv-linux PUBLIC cap seccomp)

# If BPF programs were built, include the skeleton header directory
if(BPF_PROGRAMS_AVAILABLE)
    target_include_directories(containerv-linux PRIVATE ${BPF_OUTPUT_DIR})
    target_compile_definitions(containerv-linux PRIVATE HAVE_BPF_SKELETON)
    # Add dependency on BPF programs to ensure they're built first
    add_dependencies(containerv-linux bpf_programs)
    message(STATUS "BPF LSM enforcement will be available in containerv")
endif()

