cmake_minimum_required(VERSION 3.15)
project(StoreManager VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(deps/rt-libflute)  # builds the 'flute' target
find_package(LibArchive REQUIRED)
find_package(mongocxx REQUIRED)
find_package(bsoncxx REQUIRED)

add_executable(StoreManager main.cpp
    src/Receiver.cpp 
    src/Algorithms.cpp 
    src/Graphs.cpp
    src/Store.cpp
    src/TarExtractor.cpp
)

# Include httplib paths
target_include_directories(StoreManager PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/httplib
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(StoreManager PRIVATE 
    flute
    LibArchive::LibArchive
    mongo::mongocxx_shared 
    mongo::bsoncxx_shared
)

# Optional: Add compiler warnings for better code quality
target_compile_options(StoreManager PRIVATE
    -Wall -Wextra
    -fdiagnostics-color=always
    -fno-elide-constructors
    -fconcepts-diagnostics-depth=1
)