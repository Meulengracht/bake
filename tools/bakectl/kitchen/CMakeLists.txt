project (kitchen C)

set (KITCHEN_SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND KITCHEN_SRCS "${_relPath}/${_src}")
        else()
            list (APPEND KITCHEN_SRCS "${_src}")
        endif()
    endforeach()
endmacro()

add_sources(
    pkgmgrs/pkg-config.c
    cache.c
)
if (UNIX)
    add_sources(linux_context.c)
elseif (WIN32)
    add_sources(win32_context.c)
endif()

add_library(libbake STATIC ${KITCHEN_SRCS})
target_include_directories(libbake PUBLIC include)
target_link_libraries(libbake PRIVATE jansson-api)
target_link_libraries(libbake PUBLIC jansson fridge libdirconf platform)
