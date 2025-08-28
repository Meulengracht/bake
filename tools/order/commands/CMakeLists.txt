
add_library(order-commands STATIC
    account_setup.c
    account.c
    config.c
    find.c
    info.c
    package.c
    publish.c
)
target_link_libraries(order-commands PUBLIC chef-client libdirconf)
