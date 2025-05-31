set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_cvd_service_client.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set(SRCS
    cache.c
    client.c
    context_create.c
    context_destroy.c
    step_clean.c
    step_container.c
    step_make.c
    step_pack.c
    step_source.c
)

add_library(libcvd STATIC ${GENERATED_SRCS} ${SRCS})
add_dependencies(libcvd service_client)
target_include_directories(libcvd PRIVATE ${CMAKE_BINARY_DIR}/protocols)
target_include_directories(libcvd PUBLIC include)
target_link_libraries(libcvd PUBLIC libpackage gracht common libdirconf platform)
