cmake_minimum_required(VERSION 3.16)
project(NexusHttpServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(nexus-server
    src/main.cpp
    src/HttpServer.cpp
)

target_include_directories(nexus-server PRIVATE include)

target_compile_options(nexus-server PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

target_link_libraries(nexus-server PRIVATE pthread)