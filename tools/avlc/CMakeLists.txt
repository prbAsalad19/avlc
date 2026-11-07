# ==========================================
# AVLC EXECUTABLE CMAKE CONFIGURATION
# ==========================================

add_executable(avlc
    main.cpp
)

# Link internal AVL library
target_link_libraries(avlc PRIVATE avl)

# Link LLVM components using LLVM's target library manager
llvm_config(avlc USE_SHARED Core IRReader Passes)

# Link Clang components using standard target names or imported libraries
target_link_libraries(avlc PRIVATE 
    clangAST
    clangBasic
    clangLex
    clangSema
    clangTooling
    clangFrontend
)