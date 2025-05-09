
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(bake)
add_subdirectory(bakectl)
add_subdirectory(order)
add_subdirectory(serve)
add_subdirectory(serve-exec)

# testing tools
add_subdirectory(cvctl)

if (CHEF_BUILD_AS_SNAP)
    # only on linux anyway
    install(
        TARGETS bake order serve
        RUNTIME DESTINATION bin
    )
    install(
        TARGETS bakectl serve-exec
        RUNTIME DESTINATION libexec/chef
    )
else()
    install(
        TARGETS order serve
        RUNTIME DESTINATION bin
    )

    install(
        TARGETS bakectl
        RUNTIME DESTINATION libexec/chef
    )

    install(
        TARGETS bake
        RUNTIME DESTINATION bin
    )
    
    install(
        TARGETS serve-exec
        PERMISSIONS SETGID SETUID OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        RUNTIME DESTINATION libexec/chef
    )
    
    if (UNIX)
        install(CODE "execute_process(COMMAND sh -c \"chown root:root $<TARGET_FILE:serve-exec>\" )")
    elseif (WIN32)
        SET_TARGET_PROPERTIES(serve-exec PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\" /SUBSYSTEM:CONSOLE")
    endif()
endif()
