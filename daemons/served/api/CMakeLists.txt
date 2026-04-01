set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_served_local_upload_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/chef_served_service_server.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

add_library(served-api STATIC
    ${GENERATED_SRCS}
    info.c
    install.c
    list.c
    local_install.c
    logs.c
    remove.c
    switch.c
    update.c
)
add_dependencies(served-api service_server)
target_include_directories(served-api PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../include
    ${CMAKE_BINARY_DIR}/protocols
)
target_link_libraries(served-api PRIVATE
    served-state store gracht platform vlog
)

# Set C11 standard for proper threads.h support on Windows
if(MSVC)
    target_compile_features(served-api PRIVATE c_std_11)
endif()
