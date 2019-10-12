cmake_minimum_required(VERSION 2.8)

project(Final CXX)

set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -Werror -Wextra -pthread -D_DEFAULT_SOURCE -DVERBOSE=1")
ENABLE_LANGUAGE(C)

find_package(Threads)
find_package(Boost 1.54 REQUIRED COMPONENTS program_options)

add_library(server server.cpp)
add_library(response response.cpp)
add_library(utils utils.cpp)
add_executable(final main.cpp)

target_link_libraries(server ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(utils ${Boost_LIBRARIES})
target_link_libraries(final server utils response)
