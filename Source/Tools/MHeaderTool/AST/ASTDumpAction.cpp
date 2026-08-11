#include "AST/ASTDumpAction.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"

#include <cassert>

namespace mession::headercodegen {

    bool MASTDumpVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl* RD) {
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
        Record.HeaderPath    = RD->getBeginLoc().printToString(Ctx.getSourceManager());
        Record.SourceLine    = Ctx.getSourceManager().getSpellingLineNumber(RD->getBeginLoc());
        IR.Records.push_back(std::move(Record));
        return true;
    }

    bool MASTDumpVisitor::VisitFunctionDecl(clang::FunctionDecl* FD) {
        if (FD->isImplicit()) {
            return true;
        }

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
        // ClangTool 通过工厂创建 frontend action — 工厂不能携带外部 IR 引用。
        // 这里用 process-static 指针在 RunDump 调用前注入 IR；测试为单线程调用。
        SParseIR* GIR = nullptr;
    } // namespace

    void MASTDumpAction::SetIR(SParseIR* InIR) {
        GIR = InIR;
    }

    std::unique_ptr<clang::ASTConsumer> MASTDumpAction::CreateASTConsumer(clang::CompilerInstance& CI, llvm::StringRef /*File*/) {
        assert(GIR && "MASTDumpAction::SetIR must be called before Tool.run");
        SParseIR& IR = *GIR;

        class FConsumer : public clang::ASTConsumer {
            public:
            FConsumer(clang::ASTContext& InCtx, SParseIR& InIR) : Visitor(InCtx, InIR) {
            }

            void HandleTranslationUnit(clang::ASTContext& InCtx) override {
                Visitor.TraverseDecl(InCtx.getTranslationUnitDecl());
            }

            private:
            MASTDumpVisitor Visitor;
        };

        CI.getDiagnostics().setClient(new clang::IgnoringDiagConsumer());
        return std::make_unique<FConsumer>(CI.getASTContext(), IR);
    }

} // namespace mession::headercodegen
