
add_library(serve-commands STATIC
    install.c
    list.c
    remove.c
    update.c
)
target_link_libraries(serve-commands PUBLIC chef-client)
