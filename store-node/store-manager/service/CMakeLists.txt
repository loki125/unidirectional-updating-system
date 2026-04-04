cmake_minimum_required(VERSION 3.15)
project(StoreManager VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(LibArchive REQUIRED)
find_package(mongocxx REQUIRED)
find_package(bsoncxx REQUIRED)

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

add_executable(StoreManager 
    main.cpp
    src/Receiver.cpp 
    src/Algorithms.cpp 
    src/Graphs.cpp
    src/Store.cpp
    src/TarExtractor.cpp
    src/PackageReader.cpp
    src/Recipe.cpp
    src/Reporter.cpp
)

target_include_directories(StoreManager PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/httplib
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# We link standard lib dependencies (spdlog, tinyxml2, etc.) directly into StoreManager. 
# We need to do this explicitly now because CMake's `add_subdirectory` isn't there to auto-propagate them.
target_link_libraries(StoreManager PRIVATE 
    flute
    boost_system
    spdlog
    fmt
    tinyxml2
    config++
    ssl
    crypto
    nl-3
    nl-genl-3
    LibArchive::LibArchive
    mongo::mongocxx_shared 
    mongo::bsoncxx_shared
)

# --- 1. THE FATAL ERROR FIX ---
target_compile_options(StoreManager PRIVATE
    -Wall -Wextra
    -fdiagnostics-color=always
    -fno-elide-constructors
    # Guard this flag so Clang-Tidy doesn't see it
    $<$<CXX_COMPILER_ID:GNU>:-fconcepts-diagnostics-depth=1>
)

# --- 2. THE LINTER CLEANUP ---
if(NOT WIN32)
    # 1. Removed --driver-mode=g++ (No longer needed since we compile with Clang)
    # 2. Fixed the check order: -* must come FIRST to disable defaults, then enable the ones you want.
    # 3. Removed 'modernize*' to prevent the massive flood of trailing-return-type warnings.
    set(MY_LINT_CHECKS 
        "clang-tidy;"
        "--format-style=google;"
        "--header-filter=src/|include/;"
        "--checks=-*,clang-diagnostic-*,clang-analyzer-*,bugprone*,performance*"
    )
    
    # set_target_properties(StoreManager PROPERTIES CXX_CLANG_TIDY "${MY_LINT_CHECKS}")
endif()