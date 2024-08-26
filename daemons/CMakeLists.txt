# this is just to add the various chef daemons

# build the runtime daemon if requested
if (CHEF_BUILD_RUNTIME)
    add_subdirectory(served)
endif (CHEF_BUILD_RUNTIME)

# build the build server if requested


# build the manager if requested
