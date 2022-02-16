
add_library(oven-generators STATIC
    cmake.c
    configure.c
)

target_include_directories(oven-generators PRIVATE ../../include)
target_link_libraries(oven-generators oven-backend platform)
