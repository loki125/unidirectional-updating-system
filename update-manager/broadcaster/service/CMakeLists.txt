cmake_minimum_required(VERSION 3.15)
project(FluteBroadcaster VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(deps/rt-libflute)  # builds the 'flute' target

add_executable(broadcaster Broadcaster.cpp main.cpp)

# Include httplib paths
target_include_directories(broadcaster PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/httplib
)

target_link_libraries(broadcaster PRIVATE flute)

# Optional: Add compiler warnings for better code quality
target_compile_options(broadcaster PRIVATE
    -Wall -Wextra
    -fdiagnostics-color=always
    -fno-elide-constructors
    -fconcepts-diagnostics-depth=1
)
