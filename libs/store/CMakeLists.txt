set(SRCS
    inventory.c
    store.c
)

add_library(store STATIC ${SRCS})
target_include_directories(store PUBLIC include)
target_link_libraries(store PRIVATE dirconf platform)
target_link_libraries(store PUBLIC jansson vafs common vlog)
