# this is just to add the various chef daemons

# build the runtime daemon if requested
if (CHEF_BUILD_RUNTIME)
    add_subdirectory(served)
endif (CHEF_BUILD_RUNTIME)

# build the container manager daemon if requested
if (CHEF_BUILD_CONTAINER)
    add_subdirectory(cvd)
endif (CHEF_BUILD_CONTAINER)

# build the build server if requested
if (CHEF_BUILD_SERVER)
    add_subdirectory(cookd)
endif (CHEF_BUILD_SERVER)

# build the manager if requested
if (CHEF_BUILD_MANAGER)
    add_subdirectory(waiterd)
endif (CHEF_BUILD_MANAGER)
