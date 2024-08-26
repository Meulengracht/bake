add_subdirectory(backends)

# initialize common sources
set(SRCS
    oven.c
    script.c
)

# add platform libraries
set(ADDITIONAL_LIBS ${LUA_LIBRARIES})
if (UNIX)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} -lpthread)
endif (UNIX)

add_library(oven STATIC ${SRCS})
add_dependencies(oven lua54)
target_include_directories(oven PRIVATE ${LUA_INCLUDE_DIRS})
target_include_directories(oven PUBLIC include)
target_link_libraries(oven PRIVATE oven-backends common platform)
target_link_libraries(oven PUBLIC vafs ${ADDITIONAL_LIBS} zstd-api libzstd_static m)
