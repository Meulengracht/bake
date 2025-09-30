# Project setup
project (chef-client C)

option(CHEF_CLIENT_API_URL "Chef client API" "https://chef-api.meulen.io")
option(CHEF_CLIENT_API_SECURE "Use secure connection for Chef client API" ON)

add_library(chef-client STATIC
    base64/encode.c

    oauth/devicecode.c
    oauth/oauth.c

    pubkey/login.c

    storage/bashupload.c
    storage/gendownload.c

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

target_include_directories(chef-client PUBLIC include ${CURL_INCLUDE_DIR})

target_compile_definitions(chef-client PUBLIC CHEF_CLIENT_API_URL="${CHEF_CLIENT_API_URL}")
if(CHEF_CLIENT_API_SECURE)
    target_compile_definitions(chef-client PUBLIC CHEF_CLIENT_API_SECURE=1)
else()
    target_compile_definitions(chef-client PUBLIC CHEF_CLIENT_API_SECURE=0)
endif()

target_link_libraries(chef-client PUBLIC ${CURL_LIBRARIES} OpenSSL::SSL jansson vafs common platform)
