# Distribution stuff related to packaging
# or system stuff

option(CHEF_DIST_SYSTEMD "Install systemd service units for daemons" OFF)
if (CHEF_DIST_SYSTEMD)
    include(GNUInstallDirs)

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-cookd.service
        ${CMAKE_CURRENT_BINARY_DIR}/chef-cookd.service
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-cvd.service
        ${CMAKE_CURRENT_BINARY_DIR}/chef-cvd.service
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-served.service
        ${CMAKE_CURRENT_BINARY_DIR}/chef-served.service
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-waiterd.service
        ${CMAKE_CURRENT_BINARY_DIR}/chef-waiterd.service
        @ONLY
    )

    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/chef-cookd.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    ) 
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/chef-cvd.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    ) 
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/chef-served.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    ) 
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/chef-waiterd.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    ) 
endif ()
