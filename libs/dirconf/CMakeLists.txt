
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
target_link_libraries(libdirconf PRIVATE jansson platform vlog)
