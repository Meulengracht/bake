# Distribution stuff related to packaging
# or system stuff

option(CHEF_DIST_SYSTEMD "Install systemd service units for daemons" OFF)
if (CHEF_DIST_SYSTEMD)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-cookd.service DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}/systemd/system) 
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-cvd.service DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}/systemd/system) 
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-served.service DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}/systemd/system) 
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/systemd/chef-waiterd.service DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}/systemd/system) 
endif ()
