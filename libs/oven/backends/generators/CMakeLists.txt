
add_library(oven-generators STATIC
    cmake.c
    configure.c
)

target_include_directories(oven-generators PRIVATE ../../include)
target_link_libraries(oven-generators PRIVATE chef-client)
target_link_libraries(oven-generators PUBLIC oven-backend oven-utils platform)
