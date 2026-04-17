set (BAKE_SRCS
    main.c
)

add_executable(bake ${BAKE_SRCS})
add_subdirectory(commands)
target_link_libraries(bake PRIVATE bake-commands dirconf platform vlog)

if (WIN32)
    add_custom_command(
        TARGET bake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:gracht>
            $<TARGET_FILE_DIR:bake>
        COMMENT "Copying gracht runtime for bake"
        VERBATIM
    )
endif ()
