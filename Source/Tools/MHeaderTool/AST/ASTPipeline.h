/**
 * @file ASTPipeline.h
 * @brief MASTPipeline —— 从 SOptions 到 SParseIR 的一站式 AST 解析入口。
 *
 * @detail 是 MClangToolRunner(裸 dump)+ 反射过滤 的门面:调用方只需要给选项、拿 IR。
 *         无状态、无缓存:每次 Run 都重新解析一遍。
 */
#pragma once

#include "AST/IR.h"
#include "Core/Types.h"

namespace mession::headercodegen {

    // 把 MHeaderTool::SOptions 引入本命名空间:AST 层的签名统一写 SOptions,
    // 调用方不必到处写 MHeaderTool::SOptions。
    using MHeaderTool::SOptions;

    /**
     * @brief AST 解析管线门面:输入选项,输出解析后的 IR。
     */
    class MASTPipeline {
        public:
        /**
         * @brief 跑完整条解析链路并返回 IR。
         * @param InOptions 运行选项(实际只用到 SourceRoot)
         * @return 解析结果;无可用源文件时返回空 IR
         *
         * @detail 设计为静态方法:调用点固定写作 `MASTPipeline::Run(Options)`,不需要实例,
         *         管线本身也不持有任何状态。
         */
        static SParseIR Run(const SOptions& InOptions);
    };

} // namespace mession::headercodegen
