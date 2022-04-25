
add_library(oven-utils STATIC
    checkpoints.c
    environment.c
)

target_include_directories(oven-utils PRIVATE ../include)
target_include_directories(oven-utils PUBLIC include)
target_link_libraries(oven-utils PRIVATE chef-client platform)
