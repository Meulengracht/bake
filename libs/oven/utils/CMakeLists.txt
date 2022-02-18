
add_library(oven-utils STATIC
    environment.c
)

target_include_directories(oven-utils PRIVATE ../include)
target_include_directories(oven-utils PUBLIC include)
target_link_libraries(oven-utils PRIVATE platform)
