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
    find_path(LIBBPF_INCLUDE_DIR NAMES bpf/libbpf.h)
    find_library(LIBBPF_LIBRARY NAMES bpf libbpf)

    if(LIBBPF_INCLUDE_DIR AND LIBBPF_LIBRARY)
        target_include_directories(containerv-linux PRIVATE ${BPF_OUTPUT_DIR} ${LIBBPF_INCLUDE_DIR})
        target_link_libraries(containerv-linux PUBLIC ${LIBBPF_LIBRARY})
        target_compile_definitions(containerv-linux PRIVATE HAVE_BPF_SKELETON)
        
        # Add dependency on BPF programs to ensure they're built first
        add_dependencies(containerv-linux bpf_programs)
        message(STATUS "BPF LSM enforcement will be available in containerv")
    else()
        message(WARNING "BPF programs built but libbpf not found; disabling BPF skeleton loading")
    endif()
endif()
