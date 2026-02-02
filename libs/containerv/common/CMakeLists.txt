add_library(containerv-common STATIC
    oci-spec.c
)

target_include_directories(containerv-common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(containerv-common PUBLIC platform)
