
add_library(order-commands STATIC
    account_setup.c
    account.c
    info.c
    publish.c
)
target_link_libraries(order-commands PUBLIC chef-client)
