project (containerv-pid1 C)

# Shared/common PID1 code
set(PID1_SHARED_SOURCES
    shared/pid1_common.c
    shared/logging.c
)

# Platform-specific sources
if (UNIX)
    set(PID1_PLATFORM_SOURCES
        linux/pid1_linux.c
    )
elseif(WIN32)
    set(PID1_PLATFORM_SOURCES
        windows/pid1_windows.c
    )
endif()

# Create PID1 library
add_library(containerv-pid1 STATIC
    ${PID1_SHARED_SOURCES}
    ${PID1_PLATFORM_SOURCES}
)

# Include directories
target_include_directories(containerv-pid1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
)

if (UNIX)
    target_include_directories(containerv-pid1 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/linux
    )
    # Link pthread for Linux
    target_link_libraries(containerv-pid1 PRIVATE pthread)
elseif(WIN32)
    target_include_directories(containerv-pid1 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/windows
    )
endif()

# Optionally build example program
add_subdirectory(examples)
