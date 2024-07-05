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
elseif (WIN32)
    add_sources_glob(windows/*.c)
endif()

add_sources(
    kitchen_step_clean.c
    kitchen_step_common.c
    kitchen_step_make.c
    kitchen_step_pack.c
    recipe_cache.c
    recipe_utils.c
    recipe.c
)

add_library(kitchen STATIC ${KITCHEN_SRCS})
target_link_libraries(kitchen PRIVATE jansson-api jansson yaml)
target_link_libraries(kitchen PUBLIC ${ADDITIONAL_LIBS} fridge oven ingredient containerv platform)
target_include_directories(kitchen PUBLIC include)
