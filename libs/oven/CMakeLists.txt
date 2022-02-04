# Project setup
project (oven C)

add_library(liboven STATIC
        oven.c
)
target_include_directories(liboven PUBLIC include)
target_link_libraries(liboven libplatform)
