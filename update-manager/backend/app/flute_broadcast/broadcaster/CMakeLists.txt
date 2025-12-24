cmake_minimum_required(VERSION 3.15)
project(FluteBroadcaster VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add rt-libflute as a subproject
add_subdirectory(deps/rt-libflute)

add_executable(broadcaster Broadcaster.cpp)

target_link_libraries(broadcaster PRIVATE ${FLUTE_LIB})

# Optional: Add compiler warnings for better code quality
target_compile_options(broadcaster PRIVATE -Wall -Wextra)