# MHeaderTool - 反射代码生成工具
# 模块化重构版本 (P4: split into library + executable + test)
#
# Test target (FreeAsyncFuncGenTest) lives in /root/Mession/CMakeLists.txt
# alongside AsyncFrameTest — that is the project's established pattern for
# test executable declarations.

# TU 清单收敛在 Sources.cmake——configure 期的引导工程(Bootstrap/)必须编同一份清单,
# 否则新增的 Generation/*.cpp 只进 mht_lib、不进引导工具,全新检出会在链接期缺符号。
include("${CMAKE_CURRENT_SOURCE_DIR}/Sources.cmake")

# Library TU — contains ProcessFreeFunctions (extracted from MHeaderTool.cpp
# so tests can link it without colliding with MHeaderTool.cpp's main()).
# Generation/CodeGenerator.cpp carries the IR-based generator entry points
# (A2 / Task 7: GenerateHeaderFromIR / GenerateSourceFromIR /
# EmitAsyncFramesHeaderFromIR). The legacy inline generators still live
# in CodeGenerator.h; A3 cleanup removes them.
add_library(mht_lib STATIC ${MHT_LIB_SOURCES})
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
add_executable(MHeaderTool ${MHT_EXE_SOURCES})
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
# 版本不写死：Clang 的 config 版本文件只对 major.minor 兼容，写死 19.1.7 会让装有
# clang-22 等其它版本的机器直接 configure 失败。本项目只用长期稳定的 libtooling API，
# 不需要版本分支；要指定版本时用 -DClang_DIR=/path/to/lib/cmake/clang 指向具体安装。
find_package(Clang REQUIRED CONFIG)

target_include_directories(mht_lib PUBLIC ${CLANG_INCLUDE_DIRS})
target_link_libraries(mht_lib PUBLIC ${CLANG_LIBRARIES})

# clang 资源头目录（stddef.h 等内置头所在）：libtooling 不经过 clang driver，会按
# 可执行文件路径推测 -resource-dir，非标准前缀下探测失败会让 await 生成物静默缺失。
# 探测逻辑见同目录 DetectClangResourceDir.cmake——各发行版布局并不统一：Debian/Ubuntu
# 在 ${LLVM_LIBRARY_DIRS}/clang/<major>，RHEL/Fedora 由 clang-resource-filesystem 包
# 提供、落在 /usr/lib/clang/<major>。
include("${CMAKE_CURRENT_LIST_DIR}/DetectClangResourceDir.cmake")

# 透传给顶层，再由顶层交给 configure 期的引导子工程——那是一次独立的 cmake 调用，
# 看不到本作用域的普通变量，而引导工具与正式构建必须指向同一个资源目录。
set(MESSION_CLANG_RESOURCE_DIR_RESOLVED
    "${MESSION_CLANG_RESOURCE_DIR_RESOLVED}"
    PARENT_SCOPE
)

target_compile_definitions(mht_lib PRIVATE
    CLANG_VERSION_MAJOR=${LLVM_VERSION_MAJOR}
    CLANG_VERSION_MINOR=${LLVM_VERSION_MINOR}
    MESSION_CLANG_RESOURCE_DIR="${MESSION_CLANG_RESOURCE_DIR_RESOLVED}"
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
