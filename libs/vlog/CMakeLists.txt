# Project setup
project (vlog C)

add_library(vlog STATIC
    sinks/tty.c
    sinks/text.c
    utils/event.c
    utils/helpers.c
    utils/pipe.c
    logf.c
    renderer.c
)
target_include_directories(vlog PUBLIC include)

# Set C11 standard for proper threads.h support on Windows
if(MSVC)
    target_compile_features(vlog PRIVATE c_std_11)
endif()

option(VLOG_BUILD_TESTS "Build vlog tests" ON)
if(VLOG_BUILD_TESTS)
    if(NOT WIN32)
        add_executable(vlog_fork_transport_test
            tests/test_fork_transport.c
        )
        target_link_libraries(vlog_fork_transport_test PRIVATE vlog)
        add_test(NAME vlog_fork_transport_test COMMAND vlog_fork_transport_test)
    endif()
endif()
