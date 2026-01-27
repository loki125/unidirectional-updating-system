cmake_minimum_required(VERSION 3.15)
project(FluteReceiver VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(deps/rt-libflute)  # builds the 'flute' target
find_package(LibArchive REQUIRED)

add_executable(FluteReceiver main.cpp
    src/Receiver.cpp 
    src/Algorithms.cpp 
    src/Graphs.cpp
    src/Store.cpp
    src/TarExtractor.cpp
)

# Include httplib paths
target_include_directories(FluteReceiver PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/httplib
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(FluteReceiver PRIVATE 
    flute
    LibArchive::LibArchive
)

# Optional: Add compiler warnings for better code quality
target_compile_options(FluteReceiver PRIVATE
    -Wall -Wextra
    -fdiagnostics-color=always
    -fno-elide-constructors
    -fconcepts-diagnostics-depth=1
)