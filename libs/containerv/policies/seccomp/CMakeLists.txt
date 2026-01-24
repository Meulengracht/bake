add_library(containerv-policy-seccomp STATIC
    seccomp.c
)
target_include_directories(containerv-policy-seccomp PRIVATE ../../include)
target_link_libraries(containerv-policy-seccomp PUBLIC platform vlog)
target_link_libraries(containerv-policy-seccomp PUBLIC cap seccomp)
