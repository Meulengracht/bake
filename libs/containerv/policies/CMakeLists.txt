# Add policy modules
add_subdirectory(ebpf)
add_subdirectory(seccomp)

add_library(containerv-policy STATIC
    policy.c
)
target_include_directories(containerv-policy PRIVATE ../include)
target_link_libraries(containerv-policy PUBLIC containerv-policy-ebpf)
target_link_libraries(containerv-policy PUBLIC containerv-policy-seccomp)
target_link_libraries(containerv-policy PUBLIC platform vlog)
