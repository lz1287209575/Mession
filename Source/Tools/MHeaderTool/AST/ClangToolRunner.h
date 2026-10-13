/**
 * @file ClangToolRunner.h
 * @brief MClangToolRunner —— 驱动 clang::tooling 跑一轮 AST dump 得到 SParseIR。
 *
 * @detail 契约:
 *   - RunDump 是**同步阻塞**调用:内部构造 ClangTool 并 Tool.run 到结束才返回。
 *   - IR 通过 MASTDumpAction 的进程级静态指针注入,因此**同一进程内不可并发**跑多轮;
 *     单线程命令行与单线程测试是唯一支持的使用方式。
 *   - 返回值按值返回(移动),调用方拥有结果。
 */
#pragma once

#include "AST/IR.h"
#include "Core/Types.h"

namespace mession::headercodegen {

    /**
     * @brief ClangTool 薄封装:加载 compile_commands.json → 遍历 TU → 汇总 IR。
     */
    class MClangToolRunner {
        public:
        /**
         * @brief 解析 InOptions.SourceRoot 下的源码,返回未经反射过滤的原始 IR。
         * @param InOptions 运行选项;只使用 SourceRoot(其它字段被忽略)
         * @return 解析出的 IR;若没有任何可解析的源文件则返回**空 IR**(而不是报错)
         *
         * @detail 源文件列表优先取 compile_commands.json;数据库缺失时回退为
         *         "递归扫描 SourceRoot 下的 .h/.hpp/.cpp/.cc + -fsyntax-only"。
         *         前置条件:调用前不得有其它在跑的 Tool.run。
         */
        SParseIR RunDump(const MHeaderTool::SOptions& InOptions);
    };

} // namespace mession::headercodegen