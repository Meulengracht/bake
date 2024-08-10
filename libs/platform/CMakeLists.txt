# Project setup
project (platform C)

add_subdirectory(ioutils)
add_subdirectory(strutils)
add_subdirectory(osutils)

add_library(platform STATIC
    environment.c
    package.c
)
target_include_directories(platform PUBLIC include)
target_link_libraries(platform PUBLIC platform-osutils platform-ioutils platform-strutils vafs vlog)
