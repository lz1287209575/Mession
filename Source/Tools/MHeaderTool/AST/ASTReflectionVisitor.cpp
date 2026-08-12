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

    // raw-lexer token 扫描判定反射宏（MCLASS / MSTRUCT / MGENERATED_BODY）。
    // 注释是 tok::comment、字符串字面量是 tok::string_literal 单 token，
    // 其中的宏名文本不会产生 identifier token——无需字节级注释剥离。
    const auto MClassArgs     = ExtractMacroCallArgs(RD->getBeginLoc(), 512, "MCLASS");
    const auto MStructArgs    = ExtractMacroCallArgs(RD->getBeginLoc(), 512, "MSTRUCT");
    const auto MGeneratedArgs = ExtractMacroCallArgs(RD->getBeginLoc(), 512, "MGENERATED_BODY");

    if (!MClassArgs.has_value() && !MStructArgs.has_value() && !MGeneratedArgs.has_value()) return true;

    SParsedRecord Record;
    Record.Name          = RD->getNameAsString();
    Record.QualifiedName = RD->getQualifiedNameAsString();
    Record.HeaderPath    = GetFilePath(RD->getBeginLoc());
    Record.SourceLine    = SM.getSpellingLineNumber(RD->getBeginLoc());

    if (MClassArgs.has_value())
    {
        Record.Kind              = ERecordKind::Class;
        Record.bHasMClassMarker  = true;
        Record.ReflectionType    = ExtractMacroValue(MClassArgs->Args, "Type").value_or("Object");
        Record.Owner             = ExtractMacroValue(MClassArgs->Args, "Owner").value_or("");
        Record.InjectionClass    = ExtractMacroValue(MClassArgs->Args, "InjectionClass").value_or("");
    }
    else if (MStructArgs.has_value())
    {
        Record.Kind              = ERecordKind::Struct;
        Record.bHasMStructMarker = true;
        Record.ReflectionType    = "Struct";
    }

    if (MGeneratedArgs.has_value())
    {
        Record.bHasMGeneratedBody = true;
        TVector<MString> Parts = SplitMacroArgs(MGeneratedArgs->Args);
        if (Parts.size() >= 2) Record.ParentClass    = Parts[1];
        if (Parts.size() >= 3) Record.ClassFlagsExpr = Parts[2];
    }

    for (const auto& Base : RD->bases())
    {
        Record.AllParentClasses.push_back(Base.getType().getAsString());
    }

    // 同一头文件会被多个 TU include（compile_commands 有 160 个源文件），
    // 每个 TU 都会访问到同一个类型——按 (QualifiedName, HeaderPath) 去重，
    // 只保留第一个（内容应一致；重复条目会让生成端同名文件后写覆盖先写，
    // 覆盖成空 Record 的 .mgenerated 文件）。
    for (const auto& Existing : IR.Records)
    {
        if (Existing.QualifiedName == Record.QualifiedName
            && Existing.HeaderPath == Record.HeaderPath)
        {
            return true;
        }
    }
    IR.Records.push_back(std::move(Record));
    return true;
}

bool MASTReflectionVisitor::VisitFieldDecl(clang::FieldDecl* FD)
{
    if (FD->isImplicit()) return true;
    if (!FD->getBeginLoc().isValid()) return true;

    // 仅在源文本中带 MPROPERTY(...) 的字段算反射属性
    // （raw-lexer token 扫描：注释/字符串里的 MPROPERTY 不会命中）
    const auto MacroArgs = ExtractMacroCallArgs(FD->getBeginLoc(), 256, "MPROPERTY");
    if (!MacroArgs.has_value()) return true;

    // 找父类对应的 IR.Records 条目
    const auto* Parent = llvm::dyn_cast<clang::CXXRecordDecl>(FD->getParent());
    if (!Parent) return true;
    SParsedRecord* Record = FindRecordByDecl(Parent);
    if (!Record) return true;

    SParsedProperty Out;
    Out.Name              = FD->getNameAsString();
    Out.Type              = QualTypeToSParsedType(FD->getType(),
        FD->getTypeSourceInfo() ? FD->getTypeSourceInfo()->getTypeLoc().getSourceRange()
                                : clang::SourceRange());

    Out.FlagsExpr = MacroArgs->Args;
    if (MacroArgs->Args.find("Injection") != MString::npos)
    {
        Out.bInjection = true;
    }
    if (auto Owner = ExtractMacroValue(MacroArgs->Args, "Owner"))
    {
        Out.Owner = *Owner;
    }

    // 解析 MPROPERTY(Meta=(K=V,...)) — legacy 生成 SetMetadata("K", "V")
    // （服务 CLI 参数注册依赖，如 Meta=(Cli="--listen")）。
    {
        const MString& Args = MacroArgs->Args;
        const MString Needle = "Meta=(";
        const size_t MetaPos = Args.find(Needle);
        if (MetaPos != MString::npos)
        {
            size_t Open = MetaPos + Needle.size();
            int Depth = 1;
            size_t Close = Open;
            while (Close < Args.size() && Depth > 0)
            {
                if (Args[Close] == '(') ++Depth;
                else if (Args[Close] == ')') --Depth;
                ++Close;
            }
            if (Depth == 0)
            {
                const MString Body = Args.substr(Open, Close - Open - 1);
                auto TrimMeta = [](MString In)
                {
                    const size_t B = In.find_first_not_of(" \t\r\n");
                    if (B == MString::npos) return MString();
                    const size_t E = In.find_last_not_of(" \t\r\n");
                    return In.substr(B, E - B + 1);
                };
                size_t Start = 0;
                for (size_t I = 0; I <= Body.size(); ++I)
                {
                    if (I == Body.size() || Body[I] == ',')
                    {
                        const MString Item = TrimMeta(Body.substr(Start, I - Start));
                        Start = I + 1;
                        const size_t Eq = Item.find('=');
                        if (Eq == MString::npos)
                        {
                            // 裸标记（如 Meta=(NonZero, ...)）→ 值默认 "true"（对齐 legacy）
                            if (!Item.empty())
                            {
                                Out.Metadata.push_back({Item, "true"});
                            }
                        }
                        else
                        {
                            MString Key = TrimMeta(Item.substr(0, Eq));
                            MString Val = TrimMeta(Item.substr(Eq + 1));
                            if (Val.size() >= 2
                                && (Val.front() == '"' || Val.front() == '\'')
                                && Val.back() == Val.front())
                            {
                                Val = Val.substr(1, Val.size() - 2);
                            }
                            if (!Key.empty())
                            {
                                Out.Metadata.push_back({std::move(Key), std::move(Val)});
                            }
                        }
                    }
                }
            }
        }
    }

    // 多个 TU 会访问同一类型——同名字段去重
    for (const auto& P : Record->Properties)
    {
        if (P.Name == Out.Name)
        {
            return true;
        }
    }
    Record->Properties.push_back(std::move(Out));
    return true;
}

bool MASTReflectionVisitor::VisitTypeAliasDecl(clang::TypeAliasDecl* TAD)
{
    if (TAD->isImplicit()) return true;
    if (!TAD->getBeginLoc().isValid()) return true;

    // 类型别名：仅记录到顶层 IR.TypeAliases（不挂到 Record.TypeAliases，
    // 因为 CodeGenerator 走的是顶层 SParsedTypeAlias）
    SParsedTypeAlias Out;
    Out.Name          = TAD->getNameAsString();
    Out.UnderlyingType = TAD->getUnderlyingType().getAsString();
    Out.HeaderPath    = GetFilePath(TAD->getBeginLoc());

    IR.TypeAliases.push_back(std::move(Out));
    return true;
}

bool MASTReflectionVisitor::VisitFunctionDecl(clang::FunctionDecl* FD)
{
    if (FD->isImplicit()) return true;

    // raw-lexer token 扫描判定反射宏；注释/字符串里的宏名不会命中。
    static const TVector<MString> ReflectionFuncMacros = {
        "MFUNCTION",
        "MDECLARE_SERVICE_RPC",
        "MDECLARE_RPC_METHOD",
        "MDECLARE_RPC_METHOD_WITH_HANDLER",
        "MDECLARE_SERVER_HOSTED_RPC_METHOD",
    };
    // 宏-函数关联：宏调用的 ')' 必须与函数声明同处一行或紧邻（<= 2 行）。
    // 256 字节回看窗口会跨过类内相邻函数的 MFUNCTION——不加行距限制会把
    // 前一个函数的宏误配给当前函数（如 HandleClientPacket 被误当 ServerCall）。
    const uint32 FuncLine = SM.getSpellingLineNumber(FD->getBeginLoc());
    bool bIsReflectionFunc = false;
    for (const auto& Macro : ReflectionFuncMacros)
    {
        const auto Hit = ExtractMacroCallArgs(FD->getBeginLoc(), 256, Macro);
        if (Hit.has_value() && (FuncLine - Hit->CloseLine) <= 2)
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

    Func.ReturnType = QualTypeToSParsedType(FD->getReturnType(), FD->getReturnTypeSourceRange());
    for (const auto* Param : FD->parameters())
    {
        SParsedParameter P;
        P.Name = Param->getNameAsString();
        P.Type = QualTypeToSParsedType(Param->getType(),
            Param->getTypeSourceInfo() ? Param->getTypeSourceInfo()->getTypeLoc().getSourceRange()
                                       : clang::SourceRange());
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

    // MFUNCTION(...) arg extraction — Transport / RpcKind / Endpoint / MessageName / Route / Target / Auth / Wrap / ClientApi
    if (auto MacroArgs = ExtractMacroCallArgs(FD->getBeginLoc(), 256, "MFUNCTION"))
    {
        ApplyMFUNCTIONMacroArgs(MacroArgs->Args, Func);
    }

    Func.bIsAsync = Func.bHasAsyncMeta || IsSFutureResultType(Func.ReturnType);

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
                // 多个 TU 会访问同一类型——同一函数（Name + SourceLine）去重
                for (const auto& F : Found->Functions)
                {
                    if (F.Name == Func.Name && F.SourceLine == Func.SourceLine)
                    {
                        return true;
                    }
                }
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

    const fs::path EnumHeader = GetFilePath(ED->getBeginLoc());
    const MString  HeaderStr  = EnumHeader.generic_string();

    // 只反射项目 Source/ 内的 scoped enum：
    //  - 排除系统/工具链头（/usr/include、gcc 等）与 ThirdParty、Tools 自身；
    //  - 匿名 enum（如 `enum : uint64`）不反射；
    //  - 对齐旧字符串解析的"enum class 备用方案"（当前项目反射 enum 均为
    //    enum class；MENUM 标记的 plain enum 后续如需再补）。
    if (HeaderStr.find("/Source/") == MString::npos) return true;
    if (HeaderStr.find("/ThirdParty/") != MString::npos) return true;
    if (HeaderStr.find("/Tools/") != MString::npos) return true;
    if (!ED->isScoped()) return true;
    if (ED->getNameAsString().empty()) return true;

    SParsedEnum E;
    E.Name           = ED->getNameAsString();
    E.HeaderPath     = EnumHeader;
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

    for (const auto& Existing : IR.Enums)
    {
        if (Existing.Name == E.Name && Existing.HeaderPath == E.HeaderPath)
        {
            return true;
        }
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
    // 同一文件在不同 TU 里因 include 搜索路径不同会产生不同拼写
    // （如 .../Build/Generated/../../Source/... vs .../Source/...）——
    // 去重（Record.HeaderPath 比较）依赖路径一致，统一用 lexically_normal。
    return fs::path(PLoc.getFilename()).lexically_normal();
}

TOptional<MASTReflectionVisitor::SMacroCallHit> MASTReflectionVisitor::ExtractMacroCallArgs(
    clang::SourceLocation Loc, uint32 LookbackBytes, llvm::StringRef MacroName) const
{
    if (!Loc.isValid()) return {};
    const clang::FileID FID = SM.getFileID(Loc);
    if (FID.isInvalid()) return {};
    const unsigned Offset = SM.getFileOffset(Loc);
    const unsigned StartOffset = (Offset > LookbackBytes) ? Offset - LookbackBytes : 0;

    const llvm::StringRef Data = SM.getBufferData(FID);
    if (Data.size() < Offset) return {};

    // Raw lexer 只在 [StartOffset, Offset) 的 token 流里扫描：
    //  - 注释是 tok::comment 单 token，其文本不会产生 identifier token；
    //  - 字符串/字符字面量是 tok::string_literal / tok::char_constant 单 token，同样不会。
    // 因此注释或字符串里出现 `MFUNCTION(` 之类的文本不会误命中。
    // Buffer 以 '\0' 结尾（llvm::MemoryBuffer 保证），Lexer 不会越界读。
    clang::LangOptions LangOpts = Ctx.getLangOpts();
    const clang::SourceLocation StartLoc =
        SM.getLocForStartOfFile(FID).getLocWithOffset(StartOffset);
    const char* BufStart = Data.data() + StartOffset;
    const char* BufEnd   = Data.data() + Data.size();
    clang::Lexer RawLex(StartLoc, LangOpts, BufStart, BufStart, BufEnd);

    TOptional<SMacroCallHit> LastHit;
    clang::Token Tok;
    while (!RawLex.LexFromRawLexer(Tok))  // 返回 true = 到达 buffer 末尾
    {
        const unsigned TokOffset = SM.getFileOffset(Tok.getLocation());
        if (TokOffset >= Offset) break;          // 已越过目标声明位置
        // raw lexer 不填充 IdentifierInfo，标识符 token 的 kind 是
        // tok::raw_identifier——用 token 文本与宏名比较。
        if (!Tok.is(clang::tok::raw_identifier)) continue;
        const unsigned IdentOffset = SM.getFileOffset(Tok.getLocation());
        if (Data.substr(IdentOffset, Tok.getLength()) != MacroName) continue;

        clang::Token Next;
        if (RawLex.LexFromRawLexer(Next)) break;  // EOF
        if (!Next.is(clang::tok::l_paren)) continue;  // 宏名后必须紧跟 '(' 才算调用

        const unsigned OpenOffset = SM.getFileOffset(Next.getLocation());
        int Depth = 1;
        unsigned CloseOffset = 0;
        clang::Token T;
        while (!RawLex.LexFromRawLexer(T))
        {
            if (T.is(clang::tok::l_paren)) ++Depth;
            else if (T.is(clang::tok::r_paren))
            {
                --Depth;
                if (Depth == 0)
                {
                    CloseOffset = SM.getFileOffset(T.getLocation());
                    break;
                }
            }
        }
        if (CloseOffset == 0 || CloseOffset < OpenOffset) continue;  // 未配对，忽略
        SMacroCallHit Hit;
        Hit.Args = MString(Data.substr(OpenOffset + 1, CloseOffset - OpenOffset - 1));
        Hit.CloseLine = SM.getSpellingLineNumber(
            SM.getLocForStartOfFile(FID).getLocWithOffset(CloseOffset));
        LastHit = std::move(Hit);
    }

    return LastHit;
}

MString MASTReflectionVisitor::GetSourceText(clang::SourceRange Range) const
{
    const clang::CharSourceRange CR = clang::CharSourceRange::getTokenRange(Range);
    return clang::Lexer::getSourceText(CR, SM, clang::LangOptions()).str();
}

void MASTReflectionVisitor::ApplyMFUNCTIONMacroArgs(
    const MString& MacroArgs, SParsedFunction& OutFunc) const
{
    const TVector<MString> Parts = SplitMacroArgs(MacroArgs);
    for (const MString& Part : Parts)
    {
        const size_t EqPos = Part.find('=');
        if (EqPos != MString::npos)
        {
            const MString Key = Part.substr(0, EqPos);
            const MString Val = Part.substr(EqPos + 1);
            if      (Key == "Endpoint")  OutFunc.Endpoint   = Val;
            else if (Key == "Message")   OutFunc.MessageName = Val;
            else if (Key == "Route")     OutFunc.Route      = Val;
            else if (Key == "Target")    OutFunc.Target     = Val;
            else if (Key == "Auth")      OutFunc.Auth       = Val;
            else if (Key == "Wrap")      OutFunc.Wrap       = Val;
            else if (Key == "Api" || Key == "ClientApi") OutFunc.ClientApi = Val;
        }
        else
        {
            if      (Part == "ServerCall") { OutFunc.Transport = EFunctionTransport::ServerCall; OutFunc.bIsRpc = true; }
            else if (Part == "ClientCall") { OutFunc.Transport = EFunctionTransport::ClientCall; OutFunc.bIsRpc = true; }
            else if (Part == "Client")     { OutFunc.Transport = EFunctionTransport::Client; }
            else if (Part == "LuaBind")    { OutFunc.Transport = EFunctionTransport::LuaBind; }
            else if (Part == "NetServer")  { OutFunc.RpcKind   = ERpcKind::Server;          OutFunc.bIsRpc = true; }
            else if (Part == "NetClient")  { OutFunc.RpcKind   = ERpcKind::Client;          OutFunc.bIsRpc = true; }
            else if (Part == "RPC")        { OutFunc.bIsRpc    = true; }
            else if (Part == "Async")      { OutFunc.bHasAsyncMeta = true; }
            else if (Part == "PlayerRPC")  { OutFunc.bHasAsyncMeta = true; OutFunc.Transport = EFunctionTransport::ServerCall; OutFunc.bIsRpc = true; }
        }
    }
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

SParsedType MASTReflectionVisitor::QualTypeToSParsedType(
    clang::QualType QT, clang::SourceRange SpellingRange) const
{
    SParsedType T;
    T.bReference = QT->isReferenceType();
    T.bPointer   = QT->isPointerType();
    // const 检测：`const T&` 的 const 在被引用类型上（isConstQualified 对
    // 引用返回 false）；指针类似。先剥引用再看。
    T.bConst = QT.getNonReferenceType().isConstQualified();
    if (T.bPointer)
    {
        T.bConst = QT->getPointeeType().isConstQualified();
    }

    // 类型名优先用源码拼写（类型 token 文本）：`uint64` / `TVector<uint32>`
    // / `const FSampleEchoRequest &`。getAsString() 会 canonical 化
    // （uint64 → long unsigned int），导致生成的 include / 参数声明与
    // 源码不一致（A2 gap 3）。无源码范围（如模板实参）时回退 getAsString。
    if (SpellingRange.isValid())
    {
        const clang::CharSourceRange CR =
            clang::CharSourceRange::getTokenRange(SpellingRange);
        const MString Spelling = clang::Lexer::getSourceText(CR, SM, clang::LangOptions()).str();
        if (!Spelling.empty())
        {
            // TypeLoc 的源码范围不含外层 const qualifier（const 在 QualType
            // 上）——按 bConst 补回，保持与字符串解析版一致
            // （legacy 生成 `const FSampleEchoRequest & Request`）。
            if (T.bConst && Spelling.rfind("const ", 0) != 0)
            {
                T.CanonicalName = "const " + Spelling;
            }
            else
            {
                T.CanonicalName = Spelling;
            }
        }
    }
    if (T.CanonicalName.empty())
    {
        T.CanonicalName = QT.getAsString();
    }

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
