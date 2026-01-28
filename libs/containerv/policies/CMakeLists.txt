# Add policy modules (Linux/UNIX only)
set(POLICY_DEPS)
if (UNIX)
    add_subdirectory(ebpf)
    add_subdirectory(seccomp)
    set(POLICY_DEPS containerv-policy-ebpf containerv-policy-seccomp)
endif()

add_library(containerv-policy STATIC
    policy.c
)
target_include_directories(containerv-policy PRIVATE ../include)
if (POLICY_DEPS)
    target_link_libraries(containerv-policy PUBLIC ${POLICY_DEPS})
endif()
target_link_libraries(containerv-policy PUBLIC platform vlog)
