# Project setup
project (vlog C)

add_library(vlog STATIC
    logf.c
)
target_include_directories(vlog PUBLIC include)

# Set C11 standard for proper threads.h support on Windows
if(MSVC)
    target_compile_features(vlog PRIVATE c_std_11)
    target_compile_options(vlog PRIVATE /std:c11)
endif()
