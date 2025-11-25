
if (UNIX)
    add_library(dirconf STATIC
        config.c
        dirs-linux.c
    )
elseif (WIN32)
    add_library(dirconf STATIC
        config.c
        dirs-win32.c
    )
endif()

target_include_directories(dirconf PUBLIC include)
target_link_libraries(dirconf PRIVATE jansson platform vlog)
