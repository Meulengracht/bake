
if (UNIX)
    add_library(libdirconf STATIC
        dirs-linux.c
    )
elseif (WIN32)
    add_library(libdirconf STATIC
        dirs-win32.c
    )
endif()

target_include_directories(libdirconf PUBLIC include)
target_link_libraries(libdirconf PRIVATE platform vlog)
