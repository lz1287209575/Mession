#pragma once

#include "AST/IR.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace mession::headercodegen
{

// PPCallbacks::MacroExpands 记录的一条反射宏展开（MCLASS/MPROPERTY/
// MFUNCTION/MSTRUCT/MGENERATED_BODY）。参数文本来自宏调用源码范围，
// 无字节窗口限制——Meta=(...) 等长参数不会被截断。
struct SMacroExpand
{
    MString Name;      // 宏名
    MString Args;      // 括号内参数文本
    clang::SourceLocation EndLoc;  // 宏调用结束位置（TU 内比较，跨文件正确）
    uint32  EndLine = 0;           // 宏调用结束行（同文件行距关联用）
};

class MASTReflectionVisitor : public clang::RecursiveASTVisitor<MASTReflectionVisitor>
{
public:
    explicit MASTReflectionVisitor(
        clang::ASTContext& Ctx, SParseIR& IR,
        const TVector<SMacroExpand>& MacroExpands);

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

    // 从 PPCallbacks 记录的宏展开里查 `MacroName` 最近一次（在声明之前）的
    // 调用，返回括号内参数文本。注释/字符串不会产生宏展开事件，且没有
    // 字节窗口——Meta=(...) 等超长参数也能完整取到。
    struct SMacroCallHit
    {
        MString Args;
        uint32  CloseLine = 0;  // 宏调用 ')' 所在行（用于宏-声明关联判定）
    };
    TOptional<SMacroCallHit> ExtractMacroCallArgs(
        clang::SourceLocation Loc, llvm::StringRef MacroName) const;
    TOptional<MString> ExtractMacroValue(const MString& MacroArgs, const MString& Key) const;
    TVector<MString>   SplitMacroArgs(const MString& Args) const;

    SParsedType QualTypeToSParsedType(
        clang::QualType QT, clang::SourceRange SpellingRange = {}) const;

    bool IsSFutureResultType(const SParsedType& T) const;

    clang::ASTContext&       Ctx;
    SParseIR&                IR;
    clang::SourceManager&    SM;
    const TVector<SMacroExpand>& MacroExpandsRef;
};

}  // namespace mession::headercodegen
