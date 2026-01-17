cmake_minimum_required(VERSION 3.13)

include(cmake/pico_sdk_import.cmake)

project(dooya-cc1101-rf ASM C CXX)

set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_subdirectory(src)
