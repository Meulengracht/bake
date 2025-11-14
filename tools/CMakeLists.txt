
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(bake)
add_subdirectory(bakectl)
add_subdirectory(mkcdk)
add_subdirectory(order)
add_subdirectory(serve)
add_subdirectory(serve-exec)

# testing tools
add_subdirectory(cvctl)

install(
    TARGETS bake order serve mkcdk
    RUNTIME DESTINATION bin
)
install(
    TARGETS bakectl serve-exec
    RUNTIME DESTINATION libexec/chef
)

if (NOT CHEF_BUILD_AS_SNAP)
    install(
        TARGETS order serve bake mkcdk
        RUNTIME DESTINATION bin
    )

    install(
        TARGETS bakectl serve-exec
        RUNTIME DESTINATION libexec/chef
    )
    
    if (UNIX)
        install(CODE "execute_process(COMMAND sh -c \"chown root:root $<TARGET_FILE:serve-exec>\" )")
    elseif (WIN32)
        SET_TARGET_PROPERTIES(serve-exec PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\" /SUBSYSTEM:CONSOLE")
    endif()
endif()
