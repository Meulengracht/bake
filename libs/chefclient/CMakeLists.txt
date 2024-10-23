# Project setup
project (chef-client C)

add_library(chef-client STATIC
    base64/encode.c

    oauth/devicecode.c
    oauth/oauth.c

    storage/bashupload.c

    account.c
    client.c
    download.c
    find.c
    info.c
    login.c
    publish.c
    request.c
    settings.c
    trace.c
)
target_include_directories(chef-client PUBLIC include)
target_link_libraries(chef-client PUBLIC libcurl wolfssl jansson-api jansson vafs common platform)
