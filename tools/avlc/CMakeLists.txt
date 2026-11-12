add_executable(avlc main.cpp)

target_link_libraries(avlc PRIVATE avl)

# Ensure the executable lands in <build>/bin for convenience
set_target_properties(avlc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS avlc RUNTIME DESTINATION bin)