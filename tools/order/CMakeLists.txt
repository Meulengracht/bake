
# include sub projects for order
add_subdirectory(commands)

set (ORDER_SRCS
    main.c
)

add_executable(order ${ORDER_SRCS})
target_link_libraries(order order-commands wolfssl)
