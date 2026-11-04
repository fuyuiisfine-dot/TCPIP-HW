cmake_minimum_required(VERSION 3.16)
project(ICMPPing LANGUAGES CXX)

add_executable(ping ping.cpp)
target_compile_features(ping PRIVATE cxx_std_17)
target_link_libraries(ping PRIVATE ws2_32)
