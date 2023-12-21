cmake_minimum_required(VERSION 3.12)
project(hello-world C)

add_executable(hello-world main.c)
install(TARGETS hello-world)
