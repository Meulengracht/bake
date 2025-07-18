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

# Build the compression libraries for our support libraries
add_subdirectory(compression)

# build jansson for json requests and responses
set(JANSSON_BUILD_DOCS OFF CACHE BOOL "Do not build jansson documentation")
set(JANSSON_EXAMPLES OFF CACHE BOOL "Do not build jansson examples")
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "Do not build jansson tests")
add_subdirectory(jansson)

# create an interface library for jansson
add_library(jansson-api INTERFACE)
target_include_directories(jansson-api INTERFACE ${CMAKE_BINARY_DIR}/libs/jansson/include)

# build libchefclient
add_subdirectory(chefclient)

# build support libraries for tools
add_subdirectory(cvd)
add_subdirectory(package)
add_subdirectory(oven)
add_subdirectory(fridge)
add_subdirectory(remote)
