# determine the system to know which implementation
# of served to use
if (UNIX)
    add_subdirectory(linux)
endif()
