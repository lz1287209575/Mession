#pragma once

#include "AST/IR.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace mession::headercodegen
{

class MASTReflectionVisitor : public clang::RecursiveASTVisitor<MASTReflectionVisitor>
{
public:
    explicit MASTReflectionVisitor(clang::ASTContext& Ctx, SParseIR& IR);

    bool TraverseDecl(clang::Decl* D);
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* RD);
    bool VisitFunctionDecl(clang::FunctionDecl* FD);
    bool VisitEnumDecl(clang::EnumDecl* ED);

private:
    void CollectAwaitSites(clang::FunctionDecl* FD, SParsedFunction& OutFunc);
    void CollectLiveAcrossAwait(clang::FunctionDecl* FD, SParsedFunction& OutFunc);

    SParsedRecord* FindRecordByDecl(const clang::CXXRecordDecl* RD);
    fs::path       GetFilePath(clang::SourceLocation Loc) const;
    MString        GetSurroundingSourceText(clang::SourceLocation Loc, uint32 LookbackBytes) const;
    MString        GetSourceText(clang::SourceRange Range) const;

    TOptional<MString> ExtractMacroArgs(const MString& SrcText, const MString& MacroName) const;
    TOptional<MString> ExtractMacroValue(const MString& MacroArgs, const MString& Key) const;
    TVector<MString>   SplitMacroArgs(const MString& Args) const;

    SParsedType QualTypeToSParsedType(clang::QualType QT) const;

    bool IsSFutureResultType(const SParsedType& T) const;

    clang::ASTContext&    Ctx;
    SParseIR&             IR;
    clang::SourceManager& SM;
};

}  // namespace mession::headercodegen
