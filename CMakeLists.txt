cmake_minimum_required(VERSION 3.25)
project(avlc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── LLVM ────────────────────────────────────────────────────────────────────
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)

# Arch's LLVM cmake files inject a malformed -D that causes a redefinition
# warning on every TU. Remove the offending definition from our targets.
remove_definitions(-D_GLIBCXX_USE_CXX11_ABI=1 -D__STDC_CONSTANT_MACROS
                   -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)

add_definitions(${LLVM_DEFINITIONS})
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

# ── Clang ───────────────────────────────────────────────────────────────────
find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang at ${Clang_DIR}")

include_directories(SYSTEM ${CLANG_INCLUDE_DIRS})

# ── Project headers ─────────────────────────────────────────────────────────
include_directories(${PROJECT_SOURCE_DIR}/include)

# ── Subdirectories ──────────────────────────────────────────────────────────
add_subdirectory(lib/avl)
add_subdirectory(tools/avlc)