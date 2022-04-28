
set (GEN_SERVICE_HEADERS
    ${CMAKE_CURRENT_BINARY_DIR}/chef_served_service.h
)

set (GEN_SERVICE_CLIENT_HEADERS
    ${CMAKE_CURRENT_BINARY_DIR}/chef_served_service_client.h
)

set (GEN_SERVICE_SERVER_HEADERS
    ${CMAKE_CURRENT_BINARY_DIR}/chef_served_service_server.h
)

set (GEN_SERVICE_CLIENT_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/chef_served_service_client.c
)

set (GEN_SERVICE_SERVER_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/chef_served_service_server.c
)

add_custom_command(
    OUTPUT
        ${GEN_SERVICE_HEADERS}
        ${GEN_SERVICE_CLIENT_HEADERS}
        ${GEN_SERVICE_CLIENT_SOURCES}
        ${GEN_SERVICE_SERVER_HEADERS}
        ${GEN_SERVICE_SERVER_SOURCES}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/libs/gracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/served.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/served.gr
)

add_custom_target(
    service_client
    DEPENDS ${GEN_SERVICE_CLIENT_HEADERS} ${GEN_SERVICE_CLIENT_SOURCES}
)

add_custom_target(
    service_server
    DEPENDS ${GEN_SERVICE_SERVER_HEADERS} ${GEN_SERVICE_SERVER_SOURCES}
)
