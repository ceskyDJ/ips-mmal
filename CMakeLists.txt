cmake_minimum_required(VERSION 3.20)
project(IPS\ |\ Simple\ malloc\ implementation C)
set(CMAKE_SOURCE_DIR src/)
set(PROJECT_BINARY_DIR ../bin/)
set(CMAKE_BINARY_DIR ../bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../bin/)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_COMPILER gcc)
#set(CMAKE_C_FLAGS "-std=gnu99 -Wall -Wextra -g -DNDEBUG")
set(CMAKE_C_FLAGS "-std=gnu99 -Wall -Wextra -g")

add_executable(test_mmal src/mmal.c test/test_mmal.c)
