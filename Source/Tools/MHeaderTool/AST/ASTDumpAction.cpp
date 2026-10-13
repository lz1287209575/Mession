/**
 * @file ASTDumpAction.cpp
 * @brief MASTDumpVisitor / MASTDumpAction 的实现:遍历 TU 抄录 record、函数与 enum。
 *
 * @detail 只做记录、不做过滤与判重;判重由上层(ASTPipeline 的分片合并)负责。
 */
#include "AST/ASTDumpAction.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"

#include <cassert>

namespace mession::headercodegen {

    bool MASTDumpVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl* RD) {
        // 只收完整定义:前向声明(forward declaration)没有成员,抄进来只会污染 IR
        if (!RD->isCompleteDefinition()) {
            return true;
        }
        if (RD->isImplicit()) {
            return true;
        }
        if (!RD->getBeginLoc().isValid()) {
            return true;
        }

        SParsedRecord Record;
        Record.Name          = RD->getNameAsString();
        Record.QualifiedName = RD->getQualifiedNameAsString();
        // printToString 产出 "path:line:col" 形式,这里当作 header path 用
        Record.HeaderPath = RD->getBeginLoc().printToString(Ctx.getSourceManager());
        // SourceLine 取"拼写位置"(#include 展开后的真实文件行号),而非宏展开位置
        Record.SourceLine = Ctx.getSourceManager().getSpellingLineNumber(RD->getBeginLoc());
        IR.Records.push_back(std::move(Record));
        return true;
    }

    bool MASTDumpVisitor::VisitFunctionDecl(clang::FunctionDecl* FD) {
        if (FD->isImplicit()) {
            return true;
        }
        // 注意:这里不做任何"是否成员函数"的判断,所以类的成员函数也会出现在 FreeFunctions 里;
        // 消费方需自行按限定名过滤。

        SParsedFunction Func;
        Func.Name       = FD->getNameAsString();
        Func.HeaderPath = FD->getBeginLoc().printToString(Ctx.getSourceManager());
        Func.SourceLine = Ctx.getSourceManager().getSpellingLineNumber(FD->getBeginLoc());
        IR.FreeFunctions.push_back(std::move(Func));
        return true;
    }

    bool MASTDumpVisitor::VisitEnumDecl(clang::EnumDecl* ED) {
        if (!ED->isCompleteDefinition()) {
            return true;
        }

        SParsedEnum E;
        E.Name       = ED->getNameAsString();
        E.HeaderPath = ED->getBeginLoc().printToString(Ctx.getSourceManager());
        IR.Enums.push_back(std::move(E));
        return true;
    }

    namespace {
        // ClangTool 通过工厂创建 frontend action,工厂无法携带外部 IR 引用,因此这里用
        // 进程级静态指针在 Tool.run 之前注入 IR。
        // 约束:仅单线程使用 —— 同一进程内不可并发跑第二轮 dump。
        SParseIR* GIR = nullptr;
    } // namespace

    void MASTDumpAction::SetIR(SParseIR* InIR) {
        GIR = InIR;
    }

    TUniquePtr<clang::ASTConsumer> MASTDumpAction::CreateASTConsumer(clang::CompilerInstance& CI, llvm::StringRef /*File*/) {
        assert(GIR && "MASTDumpAction::SetIR must be called before Tool.run");
        SParseIR& IR = *GIR;

        class FConsumer : public clang::ASTConsumer {
            public:
            FConsumer(clang::ASTContext& InCtx, SParseIR& InIR) : Visitor(InCtx, InIR) {
            }

            /** @brief 从 TU 根开始遍历;这是 visitor 唯一的入口。 */
            void HandleTranslationUnit(clang::ASTContext& InCtx) override {
                Visitor.TraverseDecl(InCtx.getTranslationUnitDecl());
            }

            private:
            MASTDumpVisitor Visitor;
        };

        // 诊断被整体丢弃(不打印、不影响退出码):解析出错时这里不会有任何提示,
        // 只会表现为"某些类型/函数没进 IR"。排查解析问题时需临时换回默认消费者。
        CI.getDiagnostics().setClient(new clang::IgnoringDiagConsumer());
        return std::make_unique<FConsumer>(CI.getASTContext(), IR);
    }

} // namespace mession::headercodegen
