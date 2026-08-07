# MHeaderTool - 反射代码生成工具
# 模块化重构版本 (P4: split into library + executable + test)
#
# Test target (FreeAsyncFuncGenTest) lives in /root/Mession/CMakeLists.txt
# alongside AsyncFrameTest — that is the project's established pattern for
# test executable declarations.

# Library TU — contains ProcessFreeFunctions (extracted from MHeaderTool.cpp
# so tests can link it without colliding with MHeaderTool.cpp's main()).
add_library(mht_lib STATIC
    MHeaderToolLib.cpp
    AST/ASTReflectionVisitor.cpp
)
target_link_libraries(mht_lib PUBLIC
    mession_core
)
target_include_directories(mht_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Source
)
configure_mession_compile_options(mht_lib)
target_compile_options(mht_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)

# Executable: thin wrapper holding main()
add_executable(MHeaderTool
    MHeaderTool.cpp
)
target_link_libraries(MHeaderTool PRIVATE
    mht_lib
)
target_include_directories(MHeaderTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Source
)
configure_mession_compile_options(MHeaderTool)
target_compile_options(MHeaderTool PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)

# A1 — libtooling 集成
find_package(Clang 17 REQUIRED CONFIG)

target_include_directories(mht_lib PUBLIC ${CLANG_INCLUDE_DIRS})
target_link_libraries(mht_lib PUBLIC ${CLANG_LIBRARIES})

target_compile_definitions(mht_lib PRIVATE
    CLANG_VERSION_MAJOR=${LLVM_VERSION_MAJOR}
    CLANG_VERSION_MINOR=${LLVM_VERSION_MINOR}
)

target_compile_options(mht_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wno-unused-parameter>
)

# Provide individual clang library targets as PUBLIC so transitive consumers
# (ASTDumpTest, future libtooling executables) link against them. The
# CLANG_LIBRARIES variable is not populated by the Clang CMake config; instead
# we enumerate the targets that libtooling-based TUs actually use.
if(NOT TARGET mht_clang_libs)
    add_library(mht_clang_libs INTERFACE)
    target_link_libraries(mht_clang_libs INTERFACE
        clangTooling
        clangFrontend
        clangFrontendTool
        clangSerialization
        clangParse
        clangAST
        clangASTMatchers
        clangBasic
        clangSema
        clangLex
        clangEdit
        clangDriver
        clangRewrite
        clangAnalysis
        clangDynamicASTMatchers
    )
endif()
target_link_libraries(mht_lib PUBLIC mht_clang_libs)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CMakeLists.txt")
    add_subdirectory(Tests)
endif()
