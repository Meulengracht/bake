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

# Set C11 standard for proper threads.h support on Windows, and enable
# MSVC's opt-in C11 atomics support required by <stdatomic.h>
if(MSVC)
    target_compile_features(vlog PRIVATE c_std_11)
    target_compile_options(vlog PRIVATE /experimental:c11atomics)
endif()

option(VLOG_BUILD_TESTS "Build vlog tests" ON)
if(VLOG_BUILD_TESTS)
    function(vlog_add_test name source)
        add_executable(${name} tests/${source})
        target_link_libraries(${name} PRIVATE vlog)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    # Portable tests: no fork()/PTY usage, safe to build and run on Windows.
    vlog_add_test(vlog_text_sink_level_filtering_test test_text_sink_level_filtering.c)
    vlog_add_test(vlog_text_sink_decoration_options_test test_text_sink_decoration_options.c)
    vlog_add_test(vlog_sink_remove_stops_output_test test_sink_remove_stops_output.c)
    vlog_add_test(vlog_step_lifecycle_test test_step_lifecycle.c)

    # These rely on fork()/posix_openpt() and are POSIX-only; they cannot run on Windows.
    if(NOT WIN32)
        vlog_add_test(vlog_fork_transport_test test_fork_transport.c)
        vlog_add_test(vlog_view_close_restores_text_test test_view_close_restores_text.c)
    endif()
endif()
