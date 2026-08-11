add_executable(ASTDumpTest
    ASTDumpTest.cpp
    ../AST/IR.cpp
    ../AST/ClangToolRunner.cpp
    ../AST/ASTDumpAction.cpp
)
target_include_directories(ASTDumpTest
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/Source
)
target_link_libraries(ASTDumpTest PRIVATE mht_lib)
configure_mession_compile_options(ASTDumpTest)

# A2DiffTest — byte-equal regression gate: runs the AST-based codegen
# (MASTPipeline + CodeGenerator IR entry points) and compares every shared
# filename against Build/Generated/. mht_lib already carries the IR entry
# points + pipeline TU, so this target only needs the test source itself.
add_executable(A2DiffTest
    A2DiffTest.cpp
)
target_include_directories(A2DiffTest
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/Source
)
target_link_libraries(A2DiffTest PRIVATE mht_lib)
configure_mession_compile_options(A2DiffTest)