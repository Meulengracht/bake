set(SRCS
    elf.c
    pe.c
)

if (UNIX)
    set(SRCS ${SRCS} linux.c)
endif ()

add_library(oven-resolvers STATIC ${SRCS})
target_include_directories(oven-resolvers PRIVATE ../include)
target_link_libraries(oven-resolvers PRIVATE platform)
