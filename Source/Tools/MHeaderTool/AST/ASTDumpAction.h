/**
 * @file ASTDumpAction.h
 * @brief 裸 AST dump 用的前端动作与访问器:把 TU 里的 record/function/enum 抄进 SParseIR。
 *
 * @detail 用途:测试与 Diagnose 场景需要"解析器到底看到了什么"的证据,不做任何反射过滤。
 *         生产路径的过滤版遍历器在 ASTReflectionVisitor.h。
 */
#pragma once

#include "AST/IR.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"

namespace mession::headercodegen {

    /**
     * @brief 递归访问 TU 并把 class/struct、自由函数、enum 记录到 IR。
     *
     * @detail 持有 IR 的**引用**且会写入,不拥有;调用方保证 IR 在 TraverseDecl 期间存活且独占。
     *         刻意不做反射宏过滤 —— 所有完整的 record 都会进 IR(包括模板实例化产物之外的普通类)。
     */
    class MASTDumpVisitor : public clang::RecursiveASTVisitor<MASTDumpVisitor> {
        public:
        explicit MASTDumpVisitor(clang::ASTContext& InCtx, SParseIR& InIR) : Ctx(InCtx), IR(InIR) {
        }

        /** @brief 记录一个完整、非隐式、有合法位置信息的 class/struct。 */
        bool VisitCXXRecordDecl(clang::CXXRecordDecl* RD);
        /** @brief 记录一个非隐式函数;不区分自由函数与成员函数 —— 成员函数也会落在 FreeFunctions 里。 */
        bool VisitFunctionDecl(clang::FunctionDecl* FD);
        /** @brief 记录一个完整 enum 定义(名字 + 声明位置)。 */
        bool VisitEnumDecl(clang::EnumDecl* ED);

        private:
        clang::ASTContext& Ctx; // 不拥有;由 frontend action 保证生命周期
        SParseIR&          IR;  // 不拥有;写入目标
    };

    /**
     * @brief ASTFrontendAction:用进程级静态指针注入 IR,再对每个 TU 跑一遍 MASTDumpVisitor。
     *
     * @detail 陷阱:IR 是通过 **进程级静态指针** 注入的(ClangTool 的工厂无法携带外部引用),
     *         因此 SetIR 必须在 Tool.run 之前调用,且同一进程内**不能并发**跑多轮 dump。
     */
    class MASTDumpAction : public clang::ASTFrontendAction {
        public:
        /**
         * @brief 注入本轮的输出 IR。
         * @param InIR 目标 IR;不能为 nullptr,且必须在 Tool.run 全程存活
         */
        static void SetIR(SParseIR* InIR);

        /**
         * @brief 为单个 TU 创建 consumer;同时把诊断输出换成静默消费者。
         * @param CI   Clang 编译器实例
         * @param File TU 文件名(未使用)
         * @return consumer;内部 debug 断言校验 SetIR 已被调用
         */
        TUniquePtr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& CI, llvm::StringRef File) override;
    };

} // namespace mession::headercodegen