add_library(oven-backends STATIC
    common.c
    autotools.c
    cmake.c
    make.c
    meson.c
    ninja.c
)

target_include_directories(oven-backends PRIVATE ../include)
target_include_directories(oven-backends PUBLIC include)
target_link_libraries(oven-backends PUBLIC common platform)
