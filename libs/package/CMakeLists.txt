add_library(libpackage STATIC container.c)
target_include_directories(libpackage PUBLIC include)
target_link_libraries(libpackage PUBLIC vafs common dirconf platform)
