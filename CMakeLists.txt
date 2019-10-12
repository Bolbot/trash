cmake_minimum_required(VERSION 2.8)

project(Final CXX)

set(CMAKE_CXX_FLAGS "-std=c++11 -pthread -D_DEFAULT_SOURCE -DVERBOSE=1")

find_package(Boost 1.65 REQUIRED COMPONENTS program_options)

add_library(server server.cpp)
add_library(response response.cpp)
add_library(utils utils.cpp)
add_executable(final main.cpp)

target_link_libraries(utils ${Boost_LIBRARIES})
target_link_libraries(final server utils response)
