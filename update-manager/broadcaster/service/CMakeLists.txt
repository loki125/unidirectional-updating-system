cmake_minimum_required(VERSION 3.15)
project(FluteBroadcaster VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)

# --- FIND PRE-COMPILED GCC FLUTE LIBRARY ---
# Search our pre-built directory for the compiled library (libflute.a or libflute.so)
find_library(FLUTE_LIB NAMES flute rt-libflute rt_libflute
             PATHS "${CMAKE_SOURCE_DIR}/deps/rt-libflute/build"
             PATH_SUFFIXES src lib .
             NO_DEFAULT_PATH)

if(NOT FLUTE_LIB)
    message(FATAL_ERROR "Could not find pre-compiled flute library! Build it via Docker first.")
endif()

# Recreate the "flute" target so we don't have to alter target_link_libraries heavily
add_library(flute UNKNOWN IMPORTED)
set_target_properties(flute PROPERTIES
    IMPORTED_LOCATION "${FLUTE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/deps/rt-libflute/include;${CMAKE_SOURCE_DIR}/deps/rt-libflute/src"
)

add_executable(broadcaster 
    Broadcaster.cpp 
    main.cpp
)

# Include httplib paths
target_include_directories(broadcaster PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/httplib
    ${CMAKE_CURRENT_SOURCE_DIR}  
)

target_link_libraries(broadcaster PRIVATE 
    flute
    ZLIB::ZLIB 
    boost_system
    spdlog
    fmt
    tinyxml2
    config++
    ssl
    crypto
    nl-3
    nl-genl-3
)

# Optional: Add compiler warnings for better code quality
target_compile_options(broadcaster PRIVATE
    -Wall -Wextra
    -fdiagnostics-color=always
    -fno-elide-constructors
    -fconcepts-diagnostics-depth=1
    # Use the guard so Clang/Clang-Tidy doesn't crash
    $<$<CXX_COMPILER_ID:GNU>:-fconcepts-diagnostics-depth=1>
)
