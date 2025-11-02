set (PACK_SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}/libs/package" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND PACK_SRCS "${_relPath}/${_src}")
        else()
            list (APPEND PACK_SRCS "${_src}")
        endif()
    endforeach()
endmacro()

add_sources(
    resolvers/common.c
    resolvers/elf.c
    resolvers/pe.c
    commands.c
    container.c
)
if (UNIX)
    add_sources(resolvers/resolver_linux.c)
endif()

add_library(libpackage STATIC ${PACK_SRCS})
target_include_directories(libpackage PUBLIC include)
target_link_libraries(libpackage PUBLIC vafs common dirconf platform)
