project (kitchen C)

set(KITCHEN_SRCS)
macro (add_sources)
    foreach (_src ${ARGN})
        list (APPEND KITCHEN_SRCS "${_src}")
    endforeach()
endmacro()
macro (add_sources_glob)
    foreach (_src ${ARGN})
        file (GLOB KITCHEN_SRCS "${_src}")
    endforeach()
endmacro()

if (UNIX)
    add_sources_glob(linux/*.c)
endif()

add_sources(
    recipe_cache.c
    recipe_utils.c
    recipe.c
)

add_library(kitchen STATIC ${KITCHEN_SRCS})
target_link_libraries(kitchen PRIVATE jansson-api jansson yaml)
target_link_libraries(kitchen PUBLIC ${ADDITIONAL_LIBS} fridge oven ingredient containerv platform)
target_include_directories(kitchen PUBLIC include)
