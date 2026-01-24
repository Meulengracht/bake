# Add policy modules
add_subdirectory(ebpf)
add_subdirectory(seccomp)

add_library(containerv-policy STATIC
    policy.c
)
target_include_directories(containerv-policy PRIVATE ../include)
target_link_libraries(containerv-policy PUBLIC platform vlog)
target_link_libraries(containerv-policy PUBLIC cap seccomp)
