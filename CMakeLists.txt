cmake_minimum_required(VERSION 2.8)
project(Final CXX)

add_library(server server.cpp)
add_library(response response.cpp)
add_library(utils utils.cpp)
add_executable(final main.cpp)

set(CMAKE_CXX_FLAGS "-std=c++11 -pthread -D_DEFAULT_SOURCE -DVERBOSE=1")
target_link_libraries(final server utils response)
