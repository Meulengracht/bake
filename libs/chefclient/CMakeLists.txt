# Project setup
project (chef-client C)

add_library(chef-client STATIC
    base64/decode.c
    base64/encode.c

    oauth/devicecode.c
    oauth/oauth.c

    client.c
    download.c
    info.c
    login.c
    publish.c
    trace.c
)
target_include_directories(chef-client PUBLIC include)
target_link_libraries(chef-client PUBLIC libcurl regex platform)
