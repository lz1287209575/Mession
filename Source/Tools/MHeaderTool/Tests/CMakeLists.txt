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