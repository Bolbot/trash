cmake_minimum_required(VERSION 2.8)
project(Final C)

add_library(server server.c)
add_library(response response.c)
add_library(utils utils.c)
add_executable(final main.c)

set(CMAKE_C_FLAGS "-std=c99 -pthread -D_DEFAULT_SOURCE -DVERBOSE=1")
target_link_libraries(final server utils response)
