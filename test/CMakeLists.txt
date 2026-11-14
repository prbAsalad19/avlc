find_program(LLVM_LIT
    NAMES llvm-lit lit
    HINTS
        ${LLVM_TOOLS_BINARY_DIR}
        /usr/bin
        /usr/local/bin
        $ENV{HOME}/.local/bin
)

find_program(FILECHECK
    NAMES FileCheck
    HINTS
        ${LLVM_TOOLS_BINARY_DIR}
        /usr/bin
        /usr/lib/llvm21/bin
        /usr/lib/llvm18/bin
)

if(LLVM_LIT AND FILECHECK)
    add_custom_target(check
        COMMAND ${LLVM_LIT} -v
            --param avlc_bin=${CMAKE_BINARY_DIR}/bin/avlc
            --param filecheck_bin=${FILECHECK}
            ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS avlc
        COMMENT "Running FileCheck tests"
    )
    message(STATUS "Tests enabled: llvm-lit=${LLVM_LIT} FileCheck=${FILECHECK}")
else()
    message(STATUS "llvm-lit not found — run: pip3 install lit --break-system-packages")
endif()