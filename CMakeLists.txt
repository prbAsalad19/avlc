# ==========================================
# MAIN CMAKE CONFIGURATION
# ==========================================

cmake_minimum_required(VERSION 3.25)
project(avlc VERSION 1.0.0 LANGUAGES C CXX)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find LLVM and Clang packages (required >= 18)
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")

# Include directories and definitions
include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Subdirectories
add_subdirectory(lib/avl)
add_subdirectory(tools/avlc)