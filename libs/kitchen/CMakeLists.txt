project (kitchen C)

set(ADDITIONAL_LIBS)
if (UNIX)
    add_subdirectory(linux)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} kitchen-linux)
endif()

add_library(kitchen STATIC
    recipe.c
    recipe_utils.c
)
target_link_libraries(kitchen PRIVATE oven fridge)
target_link_libraries(kitchen PUBLIC ${ADDITIONAL_LIBS} yaml platform)
target_include_directories(kitchen PUBLIC include)
