
set (BAKE_SRCS)

add_executable(bake ${BAKE_SRCS})
target_include_directories(libyaml PRIVATE include)
target_link_libraries(bake libyaml)
