
add_library(order-commands STATIC
    info.c
)
target_link_libraries(order-commands PUBLIC chef-client)
