
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(bake)
add_subdirectory(order)
add_subdirectory(serve)
add_subdirectory(serve-exec)

if (CHEF_BUILD_AS_SNAP)
    install(
        TARGETS bake order serve serve-exec
        RUNTIME DESTINATION bin
    )
else()
    install(
        TARGETS order serve
        RUNTIME DESTINATION bin
    )

    install(
        TARGETS bake serve-exec
        PERMISSIONS SETGID SETUID OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        RUNTIME DESTINATION bin
    )
    
    if (UNIX)
        install(CODE "execute_process(COMMAND sh -c \"chown root:root $<TARGET_FILE:bake>\" )")
        install(CODE "execute_process(COMMAND sh -c \"chown root:root $<TARGET_FILE:serve-exec>\" )")
    endif()
endif()


