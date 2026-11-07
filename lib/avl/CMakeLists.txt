# ==========================================
# AVL LIBRARY CMAKE CONFIGURATION
# ==========================================

add_library(avl
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

target_include_directories(avl PUBLIC 
    ../../include
)