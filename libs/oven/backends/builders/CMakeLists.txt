
add_library(oven-builders STATIC
    make.c
    meson.c
    ninja.c
)

target_include_directories(oven-builders PRIVATE ../../include)
target_link_libraries(oven-builders PRIVATE chef-client)
target_link_libraries(oven-builders PUBLIC oven-backend oven-utils platform)
