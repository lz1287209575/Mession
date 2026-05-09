# MHeaderTool - 反射代码生成工具
# 模块化重构版本

set(MHEADERTOOL_SOURCES
    # Core - 核心数据类型和接口
    Core/Types.h

    # Util - 工具函数
    Util/StringUtil.h
    Util/FileUtil.h

    # Parsing - 解析模块
    Parsing/HeaderScanner.h
    Parsing/ClassParser.h
    Parsing/FunctionParser.h
    Parsing/PropertyParser.h
    Parsing/EnumParser.h
    Parsing/MacroExpander.h

    # Cache - 增量编译缓存
    Cache/BuildCache.h
    Cache/CacheReader.h
    Cache/CacheWriter.h
    Cache/IncrementalDriver.h

    # Generation - 代码生成
    Generation/CodeGenerator.h
    Generation/HeaderGenerator.h
    Generation/SourceGenerator.h
    Generation/ManifestGenerators.h

    # Parallel - 并行处理
    Parallel/ParallelExecutor.h

    # Main entry
    MHeaderTool.cpp
)

add_executable(MHeaderTool
    ${MHEADERTOOL_SOURCES}
)

target_link_libraries(MHeaderTool PRIVATE
    mession_core
)

target_include_directories(MHeaderTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Source
)

configure_mession_compile_options(MHeaderTool)

# Disable RTTI
target_compile_options(MHeaderTool PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)
