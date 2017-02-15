cmake_minimum_required (VERSION 2.6)
project (tinyshell C)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -g")
endif()

add_library(tinyshell tinyshell.c)
add_executable(tsh main.c)
target_link_libraries(tsh tinyshell)
