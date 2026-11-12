find_program(LLVM_LIT llvm-lit HINTS ${LLVM_TOOLS_BINARY_DIR})
find_program(FILECHECK FileCheck HINTS ${LLVM_TOOLS_BINARY_DIR})

if(LLVM_LIT AND FILECHECK)
    add_custom_target(check
        COMMAND ${LLVM_LIT} -v ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS avlc
        COMMENT "Running FileCheck tests"
    )
    message(STATUS "Tests enabled: run 'ninja check'")
else()
    message(STATUS "llvm-lit or FileCheck not found — tests disabled")
endif()