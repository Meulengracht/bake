set (GENERATED_SRCS
        ${CMAKE_BINARY_DIR}/protocols/chef_cvd_service_server.c
    )
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    ${GENERATED_SRCS}

    server/api.c
    server/server.c

    bpf_manager.c
    config.c
    init.c
    main.c
)

add_executable(cvd ${SRCS})
add_dependencies(cvd service_server)
target_include_directories(cvd PRIVATE ${CMAKE_BINARY_DIR}/protocols include)
target_link_libraries(cvd PRIVATE containerv jansson vlog dirconf platform gracht)

# If BPF programs are available, link against libbpf and add BPF skeleton include path
if(BPF_PROGRAMS_AVAILABLE)
    find_path(LIBBPF_INCLUDE_DIR NAMES bpf/libbpf.h)
    find_library(LIBBPF_LIBRARY NAMES bpf libbpf)

    if(LIBBPF_INCLUDE_DIR AND LIBBPF_LIBRARY)
        target_include_directories(cvd PRIVATE ${BPF_OUTPUT_DIR} ${LIBBPF_INCLUDE_DIR})
        target_link_libraries(cvd PRIVATE ${LIBBPF_LIBRARY})
        target_compile_definitions(cvd PRIVATE HAVE_BPF_SKELETON)
        
        # Add dependency on BPF programs to ensure they're built first
        add_dependencies(cvd bpf_programs)
        message(STATUS "cvd daemon will have BPF LSM enforcement support")
    else()
        message(WARNING "BPF programs built but libbpf not found for cvd; BPF support disabled")
    endif()
endif()

install(
    TARGETS cvd
    RUNTIME DESTINATION libexec/chef
)
