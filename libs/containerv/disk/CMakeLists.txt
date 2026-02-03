
set (SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}/disk" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND SRCS "${_relPath}/${_src}")
        else()
            list (APPEND SRCS "${_src}")
        endif()
    endforeach()
endmacro()

if (WIN32)
    add_sources(
        empty.c
    )
elseif(UNIX)
    add_sources(
        ubuntu.c
    )
endif()

add_library(containerv-disk STATIC ${SRCS})
target_include_directories(containerv-disk PRIVATE ../include)
target_link_libraries(containerv-disk PUBLIC common dirconf platform vlog)
