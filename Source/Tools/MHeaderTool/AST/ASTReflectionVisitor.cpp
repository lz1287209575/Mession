#include "AST/ASTReflectionVisitor.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Casting.h"

namespace mession::headercodegen
{

MASTReflectionVisitor::MASTReflectionVisitor(clang::ASTContext& Ctx, SParseIR& IR)
    : Ctx(Ctx), IR(IR), SM(Ctx.getSourceManager()) {}

bool MASTReflectionVisitor::TraverseDecl(clang::Decl* D)
{
    if (!D) return true;
    return RecursiveASTVisitor::TraverseDecl(D);
}

bool MASTReflectionVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl* RD)
{
    if (!RD->isCompleteDefinition()) return true;
    if (RD->isImplicit()) return true;
    if (!RD->getBeginLoc().isValid()) return true;

    const MString Prefix = GetSurroundingSourceText(RD->getBeginLoc(), 512);
    const bool bHasReflection =
        Prefix.find("MGENERATED_BODY(") != MString::npos
        || Prefix.find("MCLASS(") != MString::npos
        || Prefix.find("MSTRUCT(") != MString::npos;

    if (!bHasReflection) return true;

    SParsedRecord Record;
    Record.Name          = RD->getNameAsString();
    Record.QualifiedName = RD->getQualifiedNameAsString();
    Record.HeaderPath    = GetFilePath(RD->getBeginLoc());
    Record.SourceLine    = SM.getSpellingLineNumber(RD->getBeginLoc());

    if (auto Args = ExtractMacroArgs(Prefix, "MCLASS("))
    {
        Record.bHasMClassMarker = true;
        Record.ReflectionType   = ExtractMacroValue(*Args, "Type").value_or("Object");
        Record.Owner            = ExtractMacroValue(*Args, "Owner").value_or("");
        Record.InjectionClass   = ExtractMacroValue(*Args, "InjectionClass").value_or("");
    }
    else if (auto Args = ExtractMacroArgs(Prefix, "MSTRUCT("))
    {
        Record.bHasMStructMarker = true;
        Record.ReflectionType = "Struct";
    }

    if (auto Args = ExtractMacroArgs(Prefix, "MGENERATED_BODY("))
    {
        Record.bHasMGeneratedBody = true;
        TVector<MString> Parts = SplitMacroArgs(*Args);
        if (Parts.size() >= 2) Record.ParentClass    = Parts[1];
        if (Parts.size() >= 3) Record.ClassFlagsExpr = Parts[2];
    }

    for (const auto& Base : RD->bases())
    {
        Record.AllParentClasses.push_back(Base.getType().getAsString());
    }

    IR.Records.push_back(std::move(Record));
    return true;
}

bool MASTReflectionVisitor::VisitFunctionDecl(clang::FunctionDecl* FD)
{
    if (FD->isImplicit()) return true;

    const MString Prefix = GetSurroundingSourceText(FD->getBeginLoc(), 256);

    static const TVector<MString> ReflectionFuncMacros = {
        "MFUNCTION(",
        "MDECLARE_SERVICE_RPC(",
        "MDECLARE_RPC_METHOD(",
        "MDECLARE_RPC_METHOD_WITH_HANDLER(",
        "MDECLARE_SERVER_HOSTED_RPC_METHOD(",
    };
    bool bIsReflectionFunc = false;
    for (const auto& Macro : ReflectionFuncMacros)
    {
        if (Prefix.find(Macro) != MString::npos)
        {
            bIsReflectionFunc = true;
            break;
        }
    }
    if (!bIsReflectionFunc) return true;

    SParsedFunction Func;
    Func.Name          = FD->getNameAsString();
    Func.QualifiedName = FD->getQualifiedNameAsString();
    Func.HeaderPath    = GetFilePath(FD->getBeginLoc());
    Func.SourceLine    = SM.getSpellingLineNumber(FD->getBeginLoc());

    Func.ReturnType = QualTypeToSParsedType(FD->getReturnType());
    for (const auto* Param : FD->parameters())
    {
        SParsedParameter P;
        P.Name = Param->getNameAsString();
        P.Type = QualTypeToSParsedType(Param->getType());
        Func.Params.push_back(std::move(P));
    }

    if (const auto* Method = llvm::dyn_cast<clang::CXXMethodDecl>(FD))
    {
        Func.bConst = Method->isConst();
    }

    if (FD->hasBody())
    {
        Func.AsyncBody = GetSourceText(FD->getBody()->getSourceRange());
    }

    Func.bIsAsync = IsSFutureResultType(Func.ReturnType);

    if (Func.bIsAsync)
    {
        CollectAwaitSites(FD, Func);
    }

    if (const auto* Method = llvm::dyn_cast<clang::CXXMethodDecl>(FD))
    {
        if (auto* RD = Method->getParent())
        {
            if (auto* Found = FindRecordByDecl(RD))
            {
                Found->Functions.push_back(std::move(Func));
                Found->bHasAsyncFunctions = Found->bHasAsyncFunctions || Func.bIsAsync;
                return true;
            }
        }
    }
    IR.FreeFunctions.push_back(std::move(Func));
    return true;
}

bool MASTReflectionVisitor::VisitEnumDecl(clang::EnumDecl* ED)
{
    if (!ED->isCompleteDefinition()) return true;

    SParsedEnum E;
    E.Name           = ED->getNameAsString();
    E.HeaderPath     = GetFilePath(ED->getBeginLoc());
    E.UnderlyingType = ED->getIntegerType().getAsString();

    if (ED->isScoped())
    {
        E.EnumKind    = EEnumKind::Scoped;
        E.bScopedEnum = true;
    }

    for (const auto* Enumerator : ED->enumerators())
    {
        E.Values.push_back(Enumerator->getNameAsString());
        SParsedEnumValue V;
        V.Name  = Enumerator->getNameAsString();
        V.Value = Enumerator->getInitVal().getSExtValue();
        E.ValuesDetailed.push_back(std::move(V));
    }

    IR.Enums.push_back(std::move(E));
    return true;
}

void MASTReflectionVisitor::CollectAwaitSites(
    clang::FunctionDecl* FD, SParsedFunction& OutFunc)
{
    if (!FD->hasBody()) return;
    clang::Stmt* Body = FD->getBody();

    class FCollector : public clang::RecursiveASTVisitor<FCollector>
    {
    public:
        FCollector(TVector<SAwaitSite>& InSites, clang::SourceManager& InSM)
            : Sites(InSites), SM(InSM) {}

        bool VisitCallExpr(clang::CallExpr* CE)
        {
            const MString Text = GetSourceTextImpl(CE->getSourceRange(), SM);
            SAwaitSite Site;
            Site.SourceLine    = SM.getSpellingLineNumber(CE->getBeginLoc());
            Site.AwaitExprText = Text;
            if (Text.find("TAwaitable<") != MString::npos)
            {
                Site.Kind = EAwaitSiteKind::TAwaitableCall;
            }
            else if (Text.find("AWAIT_OK(") != MString::npos)
            {
                Site.Kind = EAwaitSiteKind::AwaitOkMacro;
            }
            else
            {
                return true;
            }
            Sites.push_back(std::move(Site));
            return true;
        }

    private:
        static MString GetSourceTextImpl(clang::SourceRange Range, clang::SourceManager& InSM)
        {
            const clang::CharSourceRange CR = clang::CharSourceRange::getTokenRange(Range);
            return clang::Lexer::getSourceText(CR, InSM, clang::LangOptions()).str();
        }

        TVector<SAwaitSite>&  Sites;
        clang::SourceManager& SM;
    };

    FCollector(OutFunc.AwaitSites, SM).TraverseStmt(Body);
}

void MASTReflectionVisitor::CollectLiveAcrossAwait(
    clang::FunctionDecl* /*FD*/, SParsedFunction& /*OutFunc*/)
{
    // A2 stub — 完整算法留 P5 工作包 4a 落地
}

SParsedRecord* MASTReflectionVisitor::FindRecordByDecl(const clang::CXXRecordDecl* RD)
{
    for (auto& Record : IR.Records)
    {
        if (Record.Name == RD->getNameAsString()
            && Record.SourceLine == SM.getSpellingLineNumber(RD->getBeginLoc()))
        {
            return &Record;
        }
    }
    return nullptr;
}

fs::path MASTReflectionVisitor::GetFilePath(clang::SourceLocation Loc) const
{
    if (!Loc.isValid()) return {};
    const clang::PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    return fs::path(PLoc.getFilename());
}

MString MASTReflectionVisitor::GetSurroundingSourceText(
    clang::SourceLocation Loc, uint32 LookbackBytes) const
{
    if (!Loc.isValid()) return {};
    const clang::SourceLocation Start = Loc.getLocWithOffset(-static_cast<int>(LookbackBytes));
    const clang::CharSourceRange Range = clang::CharSourceRange::getCharRange(Start, Loc);
    return clang::Lexer::getSourceText(Range, SM, clang::LangOptions()).str();
}

MString MASTReflectionVisitor::GetSourceText(clang::SourceRange Range) const
{
    const clang::CharSourceRange CR = clang::CharSourceRange::getTokenRange(Range);
    return clang::Lexer::getSourceText(CR, SM, clang::LangOptions()).str();
}

TOptional<MString> MASTReflectionVisitor::ExtractMacroArgs(
    const MString& SrcText, const MString& MacroName) const
{
    const size_t Pos = SrcText.find(MacroName);
    if (Pos == MString::npos) return {};

    const size_t OpenParen = SrcText.find('(', Pos);
    if (OpenParen == MString::npos) return {};

    int Depth = 1;
    size_t CloseParen = OpenParen + 1;
    while (CloseParen < SrcText.size() && Depth > 0)
    {
        if (SrcText[CloseParen] == '(') ++Depth;
        else if (SrcText[CloseParen] == ')') --Depth;
        ++CloseParen;
    }
    if (Depth != 0) return {};

    return SrcText.substr(OpenParen + 1, CloseParen - OpenParen - 2);
}

TOptional<MString> MASTReflectionVisitor::ExtractMacroValue(
    const MString& MacroArgs, const MString& Key) const
{
    const MString Needle = Key + "=";
    const size_t Pos = MacroArgs.find(Needle);
    if (Pos == MString::npos) return {};

    const size_t End = MacroArgs.find_first_of(",)", Pos + Needle.size());
    if (End == MString::npos) return MacroArgs.substr(Pos + Needle.size());

    return MacroArgs.substr(Pos + Needle.size(), End - Pos - Needle.size());
}

TVector<MString> MASTReflectionVisitor::SplitMacroArgs(const MString& Args) const
{
    TVector<MString> Result;
    size_t Pos = 0;
    int Depth = 0;
    size_t Start = 0;
    while (Pos < Args.size())
    {
        if (Args[Pos] == '(') ++Depth;
        else if (Args[Pos] == ')') --Depth;
        else if (Args[Pos] == ',' && Depth == 0)
        {
            Result.push_back(Args.substr(Start, Pos - Start));
            Start = Pos + 1;
        }
        ++Pos;
    }
    Result.push_back(Args.substr(Start));
    return Result;
}

SParsedType MASTReflectionVisitor::QualTypeToSParsedType(clang::QualType QT) const
{
    SParsedType T;
    T.bReference = QT->isReferenceType();
    T.bConst     = QT.isConstQualified();
    T.bPointer   = QT->isPointerType();
    T.CanonicalName = QT.getAsString();

    if (const auto* TST =
        QT.getNonReferenceType()->getAs<clang::TemplateSpecializationType>())
    {
        const clang::TemplateDecl* TD = TST->getTemplateName().getAsTemplateDecl();
        if (TD)
        {
            T.ResolvedClassName = TD->getNameAsString();
            for (const auto& Arg : TST->template_arguments())
            {
                if (Arg.getKind() == clang::TemplateArgument::Type)
                {
                    T.TemplateArgs.push_back(QualTypeToSParsedType(Arg.getAsType()));
                }
            }
        }
    }
    return T;
}

bool MASTReflectionVisitor::IsSFutureResultType(const SParsedType& T) const
{
    return T.CanonicalName.find("SFutureResult<") == 0
        || T.ResolvedClassName == "SFutureResult";
}

}  // namespace mession::headercodegen
