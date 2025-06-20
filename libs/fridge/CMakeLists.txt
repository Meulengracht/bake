project (fridge C)

set(SRCS
    fridge.c
    inventory.c
    store.c
)

add_library(fridge STATIC ${SRCS})
target_include_directories(fridge PUBLIC include)
target_link_libraries(fridge PRIVATE jansson-api libdirconf platform)
target_link_libraries(fridge PUBLIC jansson vafs common vlog)
