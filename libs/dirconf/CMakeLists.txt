
if (UNIX)
    add_library(libdirconf STATIC
        config.c
        dirs-linux.c
    )
elseif (WIN32)
    add_library(libdirconf STATIC
        config.c
        dirs-win32.c
    )
endif()

target_include_directories(libdirconf PUBLIC include)
target_include_directories(libdirconf PRIVATE ${JANSSON_INCLUDE_DIR})
target_link_libraries(libdirconf PRIVATE ${JANSSON_LIBRARIES} platform vlog)
