project (containerv C)

set(ADDITIONAL_LIBS)
if (UNIX)
    add_subdirectory(linux)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-linux)
elseif(WIN32)
    add_subdirectory(windows)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-windows)
endif()

add_library(containerv STATIC
    shared.c
    image-manager.c
    registry-client.c
    layer-manager.c
)

target_link_libraries(containerv PUBLIC ${ADDITIONAL_LIBS} ${CURL_LIBRARIES} jansson)
target_include_directories(containerv PUBLIC include ${CURL_INCLUDE_DIR})

# Platform-specific dependencies
if (WIN32)
    target_link_libraries(containerv PRIVATE virtdisk)
endif()
