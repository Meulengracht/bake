add_library(containerv-policy-ebpf STATIC
    ebpf.c
)
target_include_directories(containerv-policy-ebpf PRIVATE ../../include)
target_link_libraries(containerv-policy-ebpf PUBLIC platform vlog)
