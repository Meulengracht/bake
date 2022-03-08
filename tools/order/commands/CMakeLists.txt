
add_library(order-commands STATIC
    account.c
    info.c
    publish.c
)
target_link_libraries(order-commands PUBLIC chef-client)
