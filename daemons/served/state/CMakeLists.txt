add_library(served-state STATIC
    runner.c
    sm.c
    state.c
)
target_include_directories(served-state PRIVATE ../include ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(served-state PUBLIC common platform vlog SQLite::SQLite3)
