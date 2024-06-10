project (fridge C)

set(SRCS
    fridge.c
    inventory.c
    store.c
)

add_library(fridge STATIC ${SRCS})
target_include_directories(fridge PUBLIC include)
target_link_libraries(fridge PRIVATE platform)
target_link_libraries(fridge PUBLIC chef-client vafs ingredient vlog)
