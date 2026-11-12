set(AVL_SOURCES
    Lexer.cpp
    Parser.cpp
    Sema/NameResolution.cpp
    Sema/TypeCheck.cpp
    Sema/OwnershipCheck.cpp
    Sema/UnsafeCheck.cpp
    CodeGen/StmtGen.cpp
    CodeGen/ExprGen.cpp
    CodeGen/TypeLowering.cpp
    CodeGen/AVLPasses.cpp
)

add_library(avl STATIC ${AVL_SOURCES})

# On Arch (and most distros) LLVM ships as a single libLLVM.so.
# The imported target "LLVM" is always defined by LLVMConfig.cmake
# and resolves correctly regardless of monolithic vs split layout.
set(CLANG_LIBS
    clangAST
    clangBasic
    clangLex
    clangSema
    clangFrontend
    clangTooling
)

target_link_libraries(avl
    PUBLIC
        LLVM          # monolithic libLLVM.so — replaces llvm_map_components_to_libnames
        ${CLANG_LIBS}
)

target_include_directories(avl
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
)

target_compile_options(avl PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)