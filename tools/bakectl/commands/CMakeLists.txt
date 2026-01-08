set (CMD_SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}/tools/bakectl/commands" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND CMD_SRCS "${_relPath}/${_src}")
        else()
            list (APPEND CMD_SRCS "${_src}")
        endif()
    endforeach()
endmacro()

add_sources(
    resolvers/common.c
    resolvers/elf.c
    resolvers/pe.c
    
    build.c
    clean.c
    common.c
    init.c
    source.c
    stage.c
)

if (UNIX)
    add_sources(resolvers/resolver_linux.c)
endif()

add_library(bakectl-commands STATIC ${CMD_SRCS})

target_include_directories(bakectl-commands PRIVATE ../include)
target_link_libraries(bakectl-commands oven store jansson platform)
