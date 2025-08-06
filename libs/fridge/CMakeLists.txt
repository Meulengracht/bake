project (fridge C)

set(SRCS
    fridge.c
    inventory.c
    store.c
)

add_library(fridge STATIC ${SRCS})
target_include_directories(fridge PRIVATE ${JANSSON_INCLUDE_DIR})
target_include_directories(fridge PUBLIC include)
target_link_libraries(fridge PRIVATE libdirconf platform)
target_link_libraries(fridge PUBLIC ${JANSSON_LIBRARIES} vafs common vlog)
