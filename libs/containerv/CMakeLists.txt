project (containerv C)

set(ADDITIONAL_LIBS)
if (UNIX)
    add_subdirectory(linux)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-linux)
endif()

add_library(containerv STATIC
    shared.c
)
target_link_libraries(containerv PUBLIC ${ADDITIONAL_LIBS})
target_include_directories(containerv PUBLIC include)
