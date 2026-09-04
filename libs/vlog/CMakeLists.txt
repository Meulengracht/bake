# Project setup
project (vlog C)

add_library(vlog STATIC
    sinks/tty.c
    sinks/text.c
    utils/helpers.c
    utils/queue.c
    logf.c
    renderer.c
)
target_include_directories(vlog PUBLIC include)

# Set C11 standard for proper threads.h support on Windows
if(MSVC)
    target_compile_features(vlog PRIVATE c_std_11)
endif()
