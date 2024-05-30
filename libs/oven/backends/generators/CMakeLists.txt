
add_library(oven-generators STATIC
    autotools.c
    cmake.c
    meson.c
)

target_include_directories(oven-generators PRIVATE ../../include)
target_link_libraries(oven-generators PRIVATE chef-client)
target_link_libraries(oven-generators PUBLIC zip ingredient oven-backend oven-utils platform)
