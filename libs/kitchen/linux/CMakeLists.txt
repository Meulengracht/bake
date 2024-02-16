add_library(kitchen-linux STATIC
    kitchen.c
)
target_include_directories(kitchen-linux PRIVATE ../include)
target_link_libraries(kitchen-linux PUBLIC fridge oven ingredient platform)
