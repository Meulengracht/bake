add_library(served-state STATIC
    logging.c
    runner.c
    sm.c
    state.c
)
target_include_directories(served-state PRIVATE ../include ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(served-state PUBLIC common platform vlog gracht SQLite::SQLite3)

# Set C11 standard for proper threads.h support on Windows
if(MSVC)
    target_compile_features(served-state PRIVATE c_std_11)
endif()
