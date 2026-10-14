# MHeaderTool 编译单元清单——单一事实来源。
#
# 同目录的 CMakeLists.txt(mht_lib + MHeaderTool 可执行文件)与
# Bootstrap/CMakeLists.txt(configure 期的宿主引导工具)都 include 本文件。
#
# 为什么必须共用一份清单:A2 重构把生成器实现从 CodeGenerator.h 等头文件搬到了
# Generation/*.cpp 之后,configure 期的引导编译命令仍只编 MHeaderTool.cpp,
# 于是链接期缺 MASTPipeline::Run / MCodeGenerator::GenerateHeaderFromIR /
# ScriptBindEmitter::Run 等符号 → 引导工具产不出来 → Build/Generated/ 为空
# → 顶层 MESSION_GENERATED_GROUPS 退化成占位值 → 各 add_executable 找不到
# *.mgenerated.cpp 而报错:全新检出直接 configure 失败。
# 把清单收敛到一处,以后新增/删除 TU 不会再漂移。
#
# 路径都用 ${CMAKE_CURRENT_LIST_DIR} 展开成绝对路径,这样从任意目录 include() 都成立。

set(MHT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# 进 mht_lib 静态库的实现 TU(MHeaderTool.cpp 之外的实现)。
set(MHT_LIB_SOURCES
    "${MHT_SOURCE_DIR}/MHeaderToolLib.cpp"
    "${MHT_SOURCE_DIR}/Generation/ScriptBindEmitter.cpp"
    "${MHT_SOURCE_DIR}/AST/ASTReflectionVisitor.cpp"
    "${MHT_SOURCE_DIR}/AST/ASTPipeline.cpp"
    "${MHT_SOURCE_DIR}/Generation/CodeGenerator.cpp"
    "${MHT_SOURCE_DIR}/Generation/LuaStdlibHintEmitter.cpp"
)

# 持有 main() 的 TU,只能出现在可执行文件里(mht_lib 要留给测试复用,不能带头部 main())。
set(MHT_EXE_SOURCES
    "${MHT_SOURCE_DIR}/MHeaderTool.cpp"
)

# 引导工具需要全部 TU(它一次性编成一个独立的可执行文件)。
set(MHT_ALL_SOURCES ${MHT_LIB_SOURCES} ${MHT_EXE_SOURCES})
