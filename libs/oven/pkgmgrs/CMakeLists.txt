set(SRCS
    pkg-config.c
)

add_library(oven-pkgmgrs STATIC ${SRCS})
target_include_directories(oven-pkgmgrs PRIVATE ../include)
target_link_libraries(oven-pkgmgrs PRIVATE platform ingredient)
