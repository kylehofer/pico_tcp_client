cmake_minimum_required(VERSION 3.13)

include(FetchContent)

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)

project(pico_tcp_client C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Finding all of our source
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cpp")

add_library(pico_tcp_client STATIC ${SOURCES})

# pull in common dependencies
target_link_libraries(pico_tcp_client
    pico_stdlib
    pico_cyw43_arch_lwip_poll
)

target_include_directories(pico_tcp_client PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
