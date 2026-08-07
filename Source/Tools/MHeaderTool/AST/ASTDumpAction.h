#pragma once

#include "AST/IR.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"

namespace mession::headercodegen {

    class MASTDumpVisitor : public clang::RecursiveASTVisitor<MASTDumpVisitor> {
        public:
        explicit MASTDumpVisitor(clang::ASTContext& InCtx, SParseIR& InIR) : Ctx(InCtx), IR(InIR) {
        }

        bool VisitCXXRecordDecl(clang::CXXRecordDecl* RD);
        bool VisitFunctionDecl(clang::FunctionDecl* FD);
        bool VisitEnumDecl(clang::EnumDecl* ED);

        private:
        clang::ASTContext& Ctx;
        SParseIR&          IR;
    };

    class MASTDumpAction : public clang::ASTFrontendAction {
        public:
        static void                         SetIR(SParseIR* InIR);
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& CI, llvm::StringRef File) override;
    };

} // namespace mession::headercodegen