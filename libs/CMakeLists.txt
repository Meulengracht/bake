# Build platform library as the first target so others
# can link against it. The platform library must be independant
add_subdirectory(platform)
add_subdirectory(vlog)
add_subdirectory(dirconf)
add_subdirectory(common)
add_subdirectory(containerv)

# Build utility libraries before the system libraries
add_subdirectory(vafs)
add_subdirectory(yaml)
add_subdirectory(gracht)

# build libchefclient
add_subdirectory(chefclient)

# build support libraries for tools
add_subdirectory(disk)
add_subdirectory(cvd)
add_subdirectory(package)
add_subdirectory(oven)
add_subdirectory(store)
add_subdirectory(remote)
