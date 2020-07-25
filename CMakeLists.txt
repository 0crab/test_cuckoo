cmake_minimum_required(VERSION 3.13)
project(test_cuckoo)

set(CMAKE_CXX_STANDARD 14)

link_libraries(pthread)

add_executable(test_cuckoo main.cpp)

add_executable(CH_HybridTest CH_HybridTest.cpp)

add_executable(hellohash hellohash.cpp)

add_executable(cpptest cpptest.cpp)

add_executable(ycsb ycsb.cpp)