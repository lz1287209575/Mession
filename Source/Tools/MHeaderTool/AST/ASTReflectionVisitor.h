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
    bool VisitFieldDecl(clang::FieldDecl* FD);
    bool VisitTypeAliasDecl(clang::TypeAliasDecl* TAD);
    bool VisitFunctionDecl(clang::FunctionDecl* FD);
    bool VisitEnumDecl(clang::EnumDecl* ED);

private:
    void CollectAwaitSites(clang::FunctionDecl* FD, SParsedFunction& OutFunc);
    void CollectLiveAcrossAwait(clang::FunctionDecl* FD, SParsedFunction& OutFunc);

    SParsedRecord* FindRecordByDecl(const clang::CXXRecordDecl* RD);
    fs::path       GetFilePath(clang::SourceLocation Loc) const;
    MString        GetSourceText(clang::SourceRange Range) const;

    void ApplyMFUNCTIONMacroArgs(const MString& MacroArgs, SParsedFunction& OutFunc) const;

    // raw-lexer 版宏调用参数提取：在 [Loc - LookbackBytes, Loc) 的 token 流里
    // 找最后一次出现的 `MacroName (` 调用，返回括号内参数原始文本。
    // 注释是 tok::comment、字符串字面量是 tok::string_literal 单 token，
    // 其中的宏名文本不会产生 identifier token——天然免疫注释/字符串误判，
    // 无需字节级注释剥离。
    struct SMacroCallHit
    {
        MString Args;
        uint32  CloseLine = 0;  // 宏调用 ')' 所在行（用于宏-声明关联判定）
    };
    TOptional<SMacroCallHit> ExtractMacroCallArgs(
        clang::SourceLocation Loc, uint32 LookbackBytes, llvm::StringRef MacroName) const;
    TOptional<MString> ExtractMacroValue(const MString& MacroArgs, const MString& Key) const;
    TVector<MString>   SplitMacroArgs(const MString& Args) const;

    SParsedType QualTypeToSParsedType(
        clang::QualType QT, clang::SourceRange SpellingRange = {}) const;

    bool IsSFutureResultType(const SParsedType& T) const;

    clang::ASTContext&    Ctx;
    SParseIR&             IR;
    clang::SourceManager& SM;
};

}  // namespace mession::headercodegen
