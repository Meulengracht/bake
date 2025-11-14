add_subdirectory(backends)

set (OVEN_SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}/libs/oven" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND OVEN_SRCS "${_relPath}/${_src}")
        else()
            list (APPEND OVEN_SRCS "${_src}")
        endif()
    endforeach()
endmacro()

add_sources(
    pkgmgrs/pkg-config.c
    cache.c
    oven.c
    script.c
)
if (UNIX)
    add_sources(linux_context.c)
elseif (WIN32)
    add_sources(win32_context.c)
endif()

# add platform libraries
set(ADDITIONAL_LIBS ${LUA_LIBRARIES})
if (UNIX)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} -lpthread)
endif (UNIX)

add_library(oven STATIC ${OVEN_SRCS})
add_dependencies(oven lua54)
target_include_directories(oven PRIVATE ${LUA_INCLUDE_DIRS})
target_include_directories(oven PUBLIC include)
target_link_libraries(oven PUBLIC vafs ${ADDITIONAL_LIBS} oven-backends store jansson zstd dirconf common platform m)
