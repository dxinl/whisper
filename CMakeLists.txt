cmake_minimum_required(VERSION 3.10.0)
project(whisper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SOURCES "*.cpp" "*.hpp" "*.h")

add_executable(example example.cpp)
