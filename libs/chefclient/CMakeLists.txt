# Project setup
project (chef-client C)

add_library(chef-client STATIC
    base64/decode.c
    base64/encode.c

    client.c
    download.c
    info.c
    publish.c
)
target_include_directories(chef-client PUBLIC include)
target_link_libraries(chef-client PUBLIC libcurl)
