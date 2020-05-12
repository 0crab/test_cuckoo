cmake_minimum_required(VERSION 3.15)
project(test_cuckoo)

set(CMAKE_CXX_STANDARD 14)

add_executable(test_cuckoo main.cpp)

target_link_libraries(test_cuckoo pthread)