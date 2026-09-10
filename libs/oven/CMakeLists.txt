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
target_link_libraries(oven PUBLIC vafs ${ADDITIONAL_LIBS} oven-backends store jansson zstd dirconf common platform)

if (NOT MSVC)
    target_link_libraries(oven PUBLIC m)
endif()

# Python-backed workflow tests require a POSIX test runner and interpreter.
if (BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    # Register Python scenarios only when the interpreter is available.
    if (Python3_Interpreter_FOUND)
        # The output harness compiles a small backend translation unit directly
        # and is only portable to the POSIX logging implementation.
        # The direct output harness includes the POSIX backend implementation.
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(NAME oven_backend_output
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_backend_output.py
                    ${CMAKE_C_COMPILER})
            set_tests_properties(oven_backend_output PROPERTIES TIMEOUT 60)
        endif()
        add_executable(oven_recipe_test_driver tests/recipe_driver.c)
        target_link_libraries(oven_recipe_test_driver PRIVATE oven)
        add_test(NAME oven_recipe_workflow
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_recipe_workflow.py
                $<TARGET_FILE:oven_recipe_test_driver> ${CMAKE_COMMAND})
        set_tests_properties(oven_recipe_workflow PROPERTIES TIMEOUT 120)
    endif()
endif()

# This C test covers argument serialization and the backend helper contracts on
# every platform with a working C compiler.
if (BUILD_TESTING)
    add_executable(oven_backend_arguments_test tests/test_backend_arguments.c)
    target_link_libraries(oven_backend_arguments_test PRIVATE oven)
    add_test(NAME oven_backend_arguments COMMAND oven_backend_arguments_test)
    set_tests_properties(oven_backend_arguments PROPERTIES TIMEOUT 30)
endif()
