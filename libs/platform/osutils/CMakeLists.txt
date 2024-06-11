# determine the system to know which implementation
# of served to use
if (UNIX)
    add_subdirectory(linux)
elseif (WIN32)
    add_subdirectory(windows)
endif()
