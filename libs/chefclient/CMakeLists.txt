# Project setup
project (chef-client C)

# The version 2 - https://api.chef.io
# The version 1 - https://chef-api.azurewebsites.net/api
option(CHEF_CLIENT_API_URL "Chef client API" "https://api.chef.io")

add_library(chef-client STATIC
    base64/encode.c

    oauth/devicecode.c
    oauth/oauth.c

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
target_link_libraries(chef-client PUBLIC ${CURL_LIBRARIES} jansson vafs common platform)
