# determine the system to know which implementation
# of served to use
if (UNIX)
    add_subdirectory(linux)
endif()

if (WIN32)
    add_subdirectory(windows)
endif()