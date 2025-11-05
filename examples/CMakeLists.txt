cmake_minimum_required(VERSION 3.12)

# Examples for Chef Containerv Library
project(chef-containerv-examples C)

# Only build examples if containerv library is available
find_library(CONTAINERV_LIB containerv HINTS ${CMAKE_BINARY_DIR}/lib)
find_path(CONTAINERV_INCLUDE chef/containerv.h HINTS ${CMAKE_SOURCE_DIR}/libs/containerv/include)

if(CONTAINERV_LIB AND CONTAINERV_INCLUDE)
    message(STATUS "Building Chef Containerv examples")
    
    # Set output directory for examples
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    
    # Container Image System example
    add_executable(container-image-system container-image-system.c)
    target_link_libraries(container-image-system containerv)
    target_include_directories(container-image-system PRIVATE ${CONTAINERV_INCLUDE})
    
    # Windows Volume Management example
    add_executable(windows-volume-management windows-volume-management.c)
    target_link_libraries(windows-volume-management containerv)
    target_include_directories(windows-volume-management PRIVATE ${CONTAINERV_INCLUDE})
    
    # Make examples optional install targets
    install(TARGETS container-image-system windows-volume-management
            RUNTIME DESTINATION examples
            OPTIONAL)
    
    message(STATUS "Examples will be built to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
else()
    message(STATUS "Skipping Chef Containerv examples - library not found")
endif()