cmake_minimum_required(VERSION 3.10)
project(test_cuckoo)


set(CMAKE_CXX_STANDARD 14)

set(CMAKE_C_FLAGS -fheinous-gnu-extensions)

link_libraries(pthread)

add_executable(locktest locktest.cpp)

add_executable(insttest insttest.cpp)
