# create an interface library for a backend
add_library(oven-backend INTERFACE)
target_include_directories(oven-backend INTERFACE include)

add_subdirectory(builders)
add_subdirectory(generators)
