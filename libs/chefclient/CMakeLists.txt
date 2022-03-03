# Project setup
project (chef-client C)

add_library(chef-client STATIC
    oauth/devicecode.c
    oauth/oauth.c

    client.c
    download.c
    info.c
    login.c
    package.c
    publish.c
    trace.c
)
target_include_directories(chef-client PUBLIC include)
target_link_libraries(chef-client PUBLIC libcurl regex jansson-api jansson vafs platform)
