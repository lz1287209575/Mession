#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <cctype>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;

struct SParsedFunction
{
    std::string MacroArgs;
    std::string ReturnType;
    std::string Name;
    std::string Signature;
    bool bConst = false;
    bool bHasValidate = false;
    bool bIsRpc = false;
    bool bReliable = true;
    std::string RpcKind;
    std::string Endpoint;
};

struct SParsedProperty
{
    std::string MacroArgs;
    std::string Type;
    std::string Name;
    std::string PropertyKind;
    std::string FlagsExpr;
};

enum class EParsedTypeKind : uint8_t
{
    Class,
    Struct,
    Enum
};

struct SParsedClass
{
    EParsedTypeKind Kind = EParsedTypeKind::Class;
    std::string Name;
    fs::path HeaderPath;
    std::string ParentClass = "MReflectObject";
    std::string ClassFlagsExpr = "0";
    std::vector<SParsedProperty> Properties;
    std::vector<SParsedFunction> Functions;
    std::vector<std::string> EnumValues;
};

struct SClassRegion
{
    std::string Keyword;
    std::string Name;
    size_t BodyOpen = std::string::npos;
    size_t BodyClose = std::string::npos;
};

struct SOptions
{
    fs::path SourceRoot = "Source";
    fs::path OutputDir = "Build/Generated";
    fs::path CMakeManifestPath = "Build/Generated/MHeaderToolTargets.cmake";
    bool bVerbose = false;
};

using TRpcListMacroMap = std::map<std::string, std::vector<SParsedFunction>>;

std::vector<std::string> SplitTopLevelArgs(const std::string& Text);

bool StartsWith(std::string_view Text, std::string_view Prefix)
{
    return Text.substr(0, Prefix.size()) == Prefix;
}

std::string Trim(std::string_view Text)
{
    size_t Start = 0;
    while (Start < Text.size() && std::isspace(static_cast<unsigned char>(Text[Start])))
    {
        ++Start;
    }

    size_t End = Text.size();
    while (End > Start && std::isspace(static_cast<unsigned char>(Text[End - 1])))
    {
        --End;
    }

    return std::string(Text.substr(Start, End - Start));
}

std::string SanitizeIdentifier(std::string_view Text)
{
    std::string Result;
    Result.reserve(Text.size());
    for (char Ch : Text)
    {
        if (std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_')
        {
            Result.push_back(Ch);
        }
        else
        {
            Result.push_back('_');
        }
    }
    return Result;
}

std::string ReplaceAll(std::string Text, std::string_view From, std::string_view To)
{
    if (From.empty())
    {
        return Text;
    }

    size_t Pos = 0;
    while ((Pos = Text.find(From.data(), Pos, From.size())) != std::string::npos)
    {
        Text.replace(Pos, From.size(), To.data(), To.size());
        Pos += To.size();
    }
    return Text;
}

std::string MakeMaskedCopy(const std::string& Text)
{
    std::string Result = Text;
    enum class EState : uint8_t
    {
        Normal,
        LineComment,
        BlockComment,
        StringLiteral,
        CharLiteral
    };

    EState State = EState::Normal;
    for (size_t Index = 0; Index < Result.size(); ++Index)
    {
        const char Current = Result[Index];
        const char Next = (Index + 1 < Result.size()) ? Result[Index + 1] : '\0';

        switch (State)
        {
        case EState::Normal:
            if (Current == '/' && Next == '/')
            {
                Result[Index] = ' ';
                Result[Index + 1] = ' ';
                ++Index;
                State = EState::LineComment;
            }
            else if (Current == '/' && Next == '*')
            {
                Result[Index] = ' ';
                Result[Index + 1] = ' ';
                ++Index;
                State = EState::BlockComment;
            }
            else if (Current == '"')
            {
                Result[Index] = ' ';
                State = EState::StringLiteral;
            }
            else if (Current == '\'')
            {
                Result[Index] = ' ';
                State = EState::CharLiteral;
            }
            break;

        case EState::LineComment:
            if (Current != '\n')
            {
                Result[Index] = ' ';
            }
            else
            {
                State = EState::Normal;
            }
            break;

        case EState::BlockComment:
            if (Current == '*' && Next == '/')
            {
                Result[Index] = ' ';
                Result[Index + 1] = ' ';
                ++Index;
                State = EState::Normal;
            }
            else if (Current != '\n')
            {
                Result[Index] = ' ';
            }
            break;

        case EState::StringLiteral:
            if (Current == '\\' && Next != '\0')
            {
                Result[Index] = ' ';
                Result[Index + 1] = ' ';
                ++Index;
            }
            else if (Current == '"')
            {
                Result[Index] = ' ';
                State = EState::Normal;
            }
            else if (Current != '\n')
            {
                Result[Index] = ' ';
            }
            break;

        case EState::CharLiteral:
            if (Current == '\\' && Next != '\0')
            {
                Result[Index] = ' ';
                Result[Index + 1] = ' ';
                ++Index;
            }
            else if (Current == '\'')
            {
                Result[Index] = ' ';
                State = EState::Normal;
            }
            else if (Current != '\n')
            {
                Result[Index] = ' ';
            }
            break;
        }
    }

    return Result;
}

bool ParseBoolLiteral(std::string_view Text, bool DefaultValue)
{
    const std::string Value = Trim(Text);
    if (Value == "true")
    {
        return true;
    }
    if (Value == "false")
    {
        return false;
    }
    return DefaultValue;
}

bool IsIdentifierChar(char Ch)
{
    return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_';
}

bool IsKeywordAt(const std::string& Text, size_t Pos, std::string_view Keyword)
{
    if (Pos + Keyword.size() > Text.size())
    {
        return false;
    }

    if (Text.compare(Pos, Keyword.size(), Keyword) != 0)
    {
        return false;
    }

    const bool bLeftOk = (Pos == 0) || !IsIdentifierChar(Text[Pos - 1]);
    const bool bRightOk = (Pos + Keyword.size() >= Text.size()) || !IsIdentifierChar(Text[Pos + Keyword.size()]);
    return bLeftOk && bRightOk;
}

size_t SkipWhitespace(const std::string& Text, size_t Pos)
{
    while (Pos < Text.size() && std::isspace(static_cast<unsigned char>(Text[Pos])))
    {
        ++Pos;
    }
    return Pos;
}

std::optional<std::string> ReadIdentifier(const std::string& Text, size_t& InOutPos)
{
    const size_t Start = InOutPos;
    if (Start >= Text.size() || (!std::isalpha(static_cast<unsigned char>(Text[Start])) && Text[Start] != '_'))
    {
        return std::nullopt;
    }

    size_t End = Start + 1;
    while (End < Text.size() && IsIdentifierChar(Text[End]))
    {
        ++End;
    }

    InOutPos = End;
    return Text.substr(Start, End - Start);
}

size_t FindMatching(const std::string& Text, size_t OpenPos, char OpenChar, char CloseChar)
{
    int Depth = 0;
    for (size_t Index = OpenPos; Index < Text.size(); ++Index)
    {
        if (Text[Index] == OpenChar)
        {
            ++Depth;
        }
        else if (Text[Index] == CloseChar)
        {
            --Depth;
            if (Depth == 0)
            {
                return Index;
            }
        }
    }

    return std::string::npos;
}

std::optional<SParsedFunction> ParseFunctionDeclaration(
    const std::string& MacroArgs,
    const std::string& Declaration)
{
    const std::string Clean = Trim(Declaration);
    const size_t OpenParen = Clean.find('(');
    const size_t CloseParen = (OpenParen == std::string::npos)
        ? std::string::npos
        : FindMatching(Clean, OpenParen, '(', ')');
    const size_t Semicolon = Clean.rfind(';');
    if (OpenParen == std::string::npos || CloseParen == std::string::npos || Semicolon == std::string::npos)
    {
        return std::nullopt;
    }

    const std::string Head = Trim(Clean.substr(0, OpenParen));
    const size_t NameSplit = Head.find_last_of(" \t");
    if (NameSplit == std::string::npos)
    {
        return std::nullopt;
    }

    SParsedFunction Parsed;
    Parsed.MacroArgs = Trim(MacroArgs);
    Parsed.ReturnType = Trim(Head.substr(0, NameSplit));
    Parsed.Name = Trim(Head.substr(NameSplit + 1));
    Parsed.Signature = Trim(Clean.substr(OpenParen, CloseParen - OpenParen + 1));

    const std::string Tail = Trim(Clean.substr(CloseParen + 1, Semicolon - CloseParen - 1));
    Parsed.bConst = (Tail == "const");
    return Parsed;
}

std::optional<SParsedProperty> ParsePropertyDeclaration(
    const std::string& MacroArgs,
    const std::string& Declaration)
{
    std::string Clean = Trim(Declaration);
    if (Clean.empty())
    {
        return std::nullopt;
    }

    const size_t Semicolon = Clean.rfind(';');
    if (Semicolon == std::string::npos)
    {
        return std::nullopt;
    }
    Clean = Trim(Clean.substr(0, Semicolon));

    const size_t EqualsPos = Clean.find('=');
    const std::string Left = Trim(Clean.substr(0, EqualsPos));
    if (Left.empty() || Left.find('(') != std::string::npos)
    {
        return std::nullopt;
    }

    size_t NameEnd = Left.size();
    while (NameEnd > 0 && std::isspace(static_cast<unsigned char>(Left[NameEnd - 1])))
    {
        --NameEnd;
    }

    size_t NameStart = NameEnd;
    while (NameStart > 0 && IsIdentifierChar(Left[NameStart - 1]))
    {
        --NameStart;
    }

    if (NameStart == NameEnd)
    {
        return std::nullopt;
    }

    SParsedProperty Parsed;
    Parsed.MacroArgs = Trim(MacroArgs);
    Parsed.Name = Left.substr(NameStart, NameEnd - NameStart);
    Parsed.Type = Trim(Left.substr(0, NameStart));
    return Parsed;
}

void ApplyFunctionMetadataFromMacroArgs(SParsedFunction& Parsed)
{
    const std::vector<std::string> Parts = SplitTopLevelArgs(Parsed.MacroArgs);
    for (const std::string& Part : Parts)
    {
        if (Part.empty())
        {
            continue;
        }

        if (Part.find('=') == std::string::npos)
        {
            if (Part == "NetServer" || Part == "NetClient")
            {
                Parsed.bIsRpc = true;
            }
            continue;
        }

        const size_t EqualsPos = Part.find('=');
        const std::string Key = Trim(Part.substr(0, EqualsPos));
        const std::string Value = Trim(Part.substr(EqualsPos + 1));
        if (Key == "Rpc")
        {
            Parsed.bIsRpc = true;
            Parsed.RpcKind = Value;
        }
        else if (Key == "Reliable")
        {
            Parsed.bReliable = ParseBoolLiteral(Value, true);
        }
        else if (Key == "Endpoint")
        {
            Parsed.Endpoint = Value;
            Parsed.bIsRpc = true;
        }
    }
}

std::vector<std::string> SplitTopLevelArgs(const std::string& Text)
{
    std::vector<std::string> Parts;
    size_t PartStart = 0;
    int ParenDepth = 0;
    int AngleDepth = 0;
    int BraceDepth = 0;
    int BracketDepth = 0;

    for (size_t Index = 0; Index < Text.size(); ++Index)
    {
        const char Ch = Text[Index];
        switch (Ch)
        {
        case '(':
            ++ParenDepth;
            break;
        case ')':
            --ParenDepth;
            break;
        case '<':
            ++AngleDepth;
            break;
        case '>':
            if (AngleDepth > 0)
            {
                --AngleDepth;
            }
            break;
        case '{':
            ++BraceDepth;
            break;
        case '}':
            --BraceDepth;
            break;
        case '[':
            ++BracketDepth;
            break;
        case ']':
            --BracketDepth;
            break;
        case ',':
            if (ParenDepth == 0 && AngleDepth == 0 && BraceDepth == 0 && BracketDepth == 0)
            {
                Parts.push_back(Trim(Text.substr(PartStart, Index - PartStart)));
                PartStart = Index + 1;
            }
            break;
        default:
            break;
        }
    }

    if (PartStart <= Text.size())
    {
        Parts.push_back(Trim(Text.substr(PartStart)));
    }

    return Parts;
}

std::vector<std::string> SplitTopLevelPipes(const std::string& Text)
{
    std::vector<std::string> Parts;
    size_t PartStart = 0;
    int ParenDepth = 0;
    int AngleDepth = 0;
    int BraceDepth = 0;
    int BracketDepth = 0;

    for (size_t Index = 0; Index < Text.size(); ++Index)
    {
        const char Ch = Text[Index];
        switch (Ch)
        {
        case '(':
            ++ParenDepth;
            break;
        case ')':
            --ParenDepth;
            break;
        case '<':
            ++AngleDepth;
            break;
        case '>':
            if (AngleDepth > 0)
            {
                --AngleDepth;
            }
            break;
        case '{':
            ++BraceDepth;
            break;
        case '}':
            --BraceDepth;
            break;
        case '[':
            ++BracketDepth;
            break;
        case ']':
            --BracketDepth;
            break;
        case '|':
            if (ParenDepth == 0 && AngleDepth == 0 && BraceDepth == 0 && BracketDepth == 0)
            {
                Parts.push_back(Trim(Text.substr(PartStart, Index - PartStart)));
                PartStart = Index + 1;
            }
            break;
        default:
            break;
        }
    }

    if (PartStart <= Text.size())
    {
        Parts.push_back(Trim(Text.substr(PartStart)));
    }

    return Parts;
}

std::string BuildPropertyFlagsExpr(const std::string& MacroArgs)
{
    const std::vector<std::string> Tokens = SplitTopLevelPipes(MacroArgs);
    std::vector<std::string> Parts;
    for (const std::string& Token : Tokens)
    {
        if (Token.empty())
        {
            continue;
        }
        Parts.push_back("static_cast<uint64>(EPropertyFlags::" + Token + ")");
    }

    if (Parts.empty())
    {
        return "EPropertyFlags::None";
    }

    std::string Expr = "static_cast<EPropertyFlags>(";
    for (size_t Index = 0; Index < Parts.size(); ++Index)
    {
        if (Index > 0)
        {
            Expr += " | ";
        }
        Expr += Parts[Index];
    }
    Expr += ")";
    return Expr;
}

std::string InferPropertyKind(const std::string& TypeName)
{
    const std::string Compact = ReplaceAll(Trim(TypeName), " ", "");
    if (Compact == "int8")
    {
        return "Int8";
    }
    if (Compact == "int16")
    {
        return "Int16";
    }
    if (Compact == "int32")
    {
        return "Int32";
    }
    if (Compact == "int64")
    {
        return "Int64";
    }
    if (Compact == "uint8")
    {
        return "UInt8";
    }
    if (Compact == "uint16")
    {
        return "UInt16";
    }
    if (Compact == "uint32")
    {
        return "UInt32";
    }
    if (Compact == "uint64")
    {
        return "UInt64";
    }
    if (Compact == "float")
    {
        return "Float";
    }
    if (Compact == "double")
    {
        return "Double";
    }
    if (Compact == "bool")
    {
        return "Bool";
    }
    if (Compact == "FString")
    {
        return "String";
    }
    if (Compact == "FName")
    {
        return "Name";
    }
    if (Compact == "SVector")
    {
        return "Vector";
    }
    if (Compact == "SRotator")
    {
        return "Rotator";
    }
    if (StartsWith(Compact, "TVector<") || StartsWith(Compact, "TArray<") ||
        StartsWith(Compact, "TMap<") || StartsWith(Compact, "TSet<"))
    {
        return "Array";
    }
    return "Struct";
}

std::optional<SParsedFunction> ParseWrappedFunctionMacro(
    const std::string& MacroName,
    const std::string& MacroArgs)
{
    const std::vector<std::string> Parts = SplitTopLevelArgs(MacroArgs);
    if (MacroName == "MDECLARE_SERVICE_RPC")
    {
        if (Parts.size() < 5)
        {
            return std::nullopt;
        }

        SParsedFunction Parsed;
        Parsed.ReturnType = "void";
        Parsed.Name = Parts[0];
        Parsed.Signature = Parts[1];
        Parsed.MacroArgs = Parts[2] + ", Rpc=" + Parts[3] + ", Reliable=" + Parts[4] + ", Handler=true";
        Parsed.bIsRpc = true;
        Parsed.RpcKind = Parts[3];
        Parsed.bReliable = ParseBoolLiteral(Parts[4], true);
        return Parsed;
    }

    if (MacroName == "MDECLARE_RPC_METHOD" || MacroName == "MDECLARE_RPC_METHOD_WITH_HANDLER")
    {
        if (Parts.size() < 5)
        {
            return std::nullopt;
        }

        const size_t MethodIndex = 0;
        const size_t SignatureIndex = (MacroName == "MDECLARE_RPC_METHOD") ? 1 : 2;
        const size_t FlagsIndex = (MacroName == "MDECLARE_RPC_METHOD") ? 2 : 3;
        const size_t RpcIndex = (MacroName == "MDECLARE_RPC_METHOD") ? 3 : 4;
        const size_t ReliableIndex = (MacroName == "MDECLARE_RPC_METHOD") ? 4 : 5;
        if (ReliableIndex >= Parts.size())
        {
            return std::nullopt;
        }

        SParsedFunction Parsed;
        Parsed.ReturnType = "void";
        Parsed.Name = Parts[MethodIndex];
        Parsed.Signature = Parts[SignatureIndex];
        Parsed.MacroArgs = Parts[FlagsIndex] + ", Rpc=" + Parts[RpcIndex] + ", Reliable=" + Parts[ReliableIndex];
        Parsed.bIsRpc = true;
        Parsed.RpcKind = Parts[RpcIndex];
        Parsed.bReliable = ParseBoolLiteral(Parts[ReliableIndex], true);
        return Parsed;
    }

    if (MacroName == "MDECLARE_SERVER_HOSTED_RPC_METHOD")
    {
        if (Parts.size() < 7)
        {
            return std::nullopt;
        }

        SParsedFunction Parsed;
        Parsed.ReturnType = "void";
        Parsed.Name = Parts[2];
        Parsed.Signature = Parts[3];
        Parsed.MacroArgs =
            Parts[4] + ", Rpc=" + Parts[5] + ", Reliable=" + Parts[6] + ", Endpoint=" + Parts[1];
        Parsed.bIsRpc = true;
        Parsed.RpcKind = Parts[5];
        Parsed.bReliable = ParseBoolLiteral(Parts[6], true);
        Parsed.Endpoint = Parts[1];
        return Parsed;
    }

    return std::nullopt;
}

std::optional<SParsedFunction> ParseRpcListEntry(
    const std::string& ExpanderMacro,
    const std::string& EntryArgs)
{
    return ParseWrappedFunctionMacro(ExpanderMacro, EntryArgs);
}

TRpcListMacroMap ParseRpcListMacros(const std::string& Contents)
{
    TRpcListMacroMap MacroMap;
    std::istringstream Input(Contents);
    std::string PhysicalLine;
    std::string LogicalLine;

    auto FlushLogicalLine = [&MacroMap](const std::string& Line)
    {
        const std::string Trimmed = Trim(Line);
        if (!StartsWith(Trimmed, "#define "))
        {
            return;
        }

        size_t Cursor = std::string("#define ").size();
        while (Cursor < Trimmed.size() && std::isspace(static_cast<unsigned char>(Trimmed[Cursor])))
        {
            ++Cursor;
        }

        const size_t NameStart = Cursor;
        while (Cursor < Trimmed.size() && IsIdentifierChar(Trimmed[Cursor]))
        {
            ++Cursor;
        }

        if (Cursor <= NameStart)
        {
            return;
        }

        const std::string MacroName = Trimmed.substr(NameStart, Cursor - NameStart);
        if (MacroName.find("_RPC_LIST") == std::string::npos)
        {
            return;
        }

        Cursor = SkipWhitespace(Trimmed, Cursor);
        if (Cursor >= Trimmed.size() || Trimmed[Cursor] != '(')
        {
            return;
        }

        const size_t ParamsClose = FindMatching(Trimmed, Cursor, '(', ')');
        if (ParamsClose == std::string::npos)
        {
            return;
        }

        const std::string Params = Trim(Trimmed.substr(Cursor + 1, ParamsClose - Cursor - 1));
        if (Params != "OP")
        {
            return;
        }

        const std::string Body = Trim(Trimmed.substr(ParamsClose + 1));
        if (!StartsWith(Body, "OP("))
        {
            return;
        }

        const size_t CallOpen = Body.find('(');
        const size_t CallClose = (CallOpen == std::string::npos) ? std::string::npos : FindMatching(Body, CallOpen, '(', ')');
        if (CallClose == std::string::npos)
        {
            return;
        }

        const std::string EntryArgs = Body.substr(CallOpen + 1, CallClose - CallOpen - 1);
        std::vector<SParsedFunction> Functions;
        if (auto Parsed = ParseRpcListEntry("MDECLARE_SERVER_HOSTED_RPC_METHOD", EntryArgs))
        {
            Functions.push_back(std::move(*Parsed));
        }

        if (!Functions.empty())
        {
            MacroMap[MacroName] = std::move(Functions);
        }
    };

    while (std::getline(Input, PhysicalLine))
    {
        std::string Line = PhysicalLine;
        bool bContinued = false;
        if (!Line.empty() && Line.back() == '\\')
        {
            Line.pop_back();
            bContinued = true;
        }

        LogicalLine += Line;
        if (bContinued)
        {
            LogicalLine.push_back(' ');
            continue;
        }

        FlushLogicalLine(LogicalLine);
        LogicalLine.clear();
    }

    if (!LogicalLine.empty())
    {
        FlushLogicalLine(LogicalLine);
    }

    return MacroMap;
}

std::vector<SClassRegion> ParseClassRegions(const std::string& Contents)
{
    std::vector<SClassRegion> Regions;
    const std::string Masked = MakeMaskedCopy(Contents);
    size_t SearchPos = 0;
    while (SearchPos < Masked.size())
    {
        size_t BestPos = std::string::npos;
        std::string Keyword;
        for (const char* Candidate : {"class", "struct"})
        {
            const size_t CandidatePos = Masked.find(Candidate, SearchPos);
            if (CandidatePos != std::string::npos && (BestPos == std::string::npos || CandidatePos < BestPos))
            {
                BestPos = CandidatePos;
                Keyword = Candidate;
            }
        }

        if (BestPos == std::string::npos)
        {
            break;
        }

        if (!IsKeywordAt(Masked, BestPos, Keyword))
        {
            SearchPos = BestPos + 1;
            continue;
        }

        size_t Cursor = SkipWhitespace(Masked, BestPos + Keyword.size());
        if (Cursor < Masked.size() && Masked[Cursor] == '[')
        {
            const size_t AttrClose = FindMatching(Masked, Cursor, '[', ']');
            if (AttrClose == std::string::npos)
            {
                SearchPos = BestPos + 1;
                continue;
            }
            Cursor = SkipWhitespace(Masked, AttrClose + 1);
        }

        const std::optional<std::string> ClassName = ReadIdentifier(Masked, Cursor);
        if (!ClassName)
        {
            SearchPos = BestPos + 1;
            continue;
        }

        size_t BracePos = Cursor;
        while (BracePos < Masked.size() && Masked[BracePos] != '{' && Masked[BracePos] != ';')
        {
            ++BracePos;
        }

        if (BracePos >= Masked.size() || Masked[BracePos] != '{')
        {
            SearchPos = Cursor;
            continue;
        }

        const size_t CloseBrace = FindMatching(Masked, BracePos, '{', '}');
        if (CloseBrace == std::string::npos)
        {
            SearchPos = BracePos + 1;
            continue;
        }

        Regions.push_back(SClassRegion{Keyword, *ClassName, BracePos, CloseBrace});
        SearchPos = CloseBrace + 1;
    }

    return Regions;
}

std::vector<SParsedProperty> ParsePropertiesInTypeBody(const std::string& TypeBody)
{
    std::vector<SParsedProperty> Properties;
    size_t SearchPos = 0;
    while (true)
    {
        const size_t MacroPos = TypeBody.find("MPROPERTY(", SearchPos);
        if (MacroPos == std::string::npos)
        {
            break;
        }

        const size_t MacroOpen = TypeBody.find('(', MacroPos);
        const size_t MacroClose = (MacroOpen == std::string::npos)
            ? std::string::npos
            : FindMatching(TypeBody, MacroOpen, '(', ')');
        if (MacroOpen == std::string::npos || MacroClose == std::string::npos)
        {
            break;
        }

        const std::string MacroArgs = TypeBody.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);
        const size_t DeclStart = MacroClose + 1;
        const size_t DeclEnd = TypeBody.find(';', DeclStart);
        if (DeclEnd == std::string::npos)
        {
            break;
        }

        const std::string Declaration = TypeBody.substr(DeclStart, DeclEnd - DeclStart + 1);
        if (auto Parsed = ParsePropertyDeclaration(MacroArgs, Declaration))
        {
            Parsed->PropertyKind = InferPropertyKind(Parsed->Type);
            Parsed->FlagsExpr = BuildPropertyFlagsExpr(Parsed->MacroArgs);
            Properties.push_back(std::move(*Parsed));
        }

        SearchPos = DeclEnd + 1;
    }

    return Properties;
}

void ParseGeneratedBodyMetadata(const std::string& TypeBody, SParsedClass& Parsed)
{
    const size_t MacroPos = TypeBody.find("MGENERATED_BODY(");
    if (MacroPos == std::string::npos)
    {
        return;
    }

    const size_t MacroOpen = TypeBody.find('(', MacroPos);
    const size_t MacroClose = (MacroOpen == std::string::npos)
        ? std::string::npos
        : FindMatching(TypeBody, MacroOpen, '(', ')');
    if (MacroOpen == std::string::npos || MacroClose == std::string::npos)
    {
        return;
    }

    const std::vector<std::string> Parts = SplitTopLevelArgs(TypeBody.substr(MacroOpen + 1, MacroClose - MacroOpen - 1));
    if (Parts.size() >= 2)
    {
        Parsed.ParentClass = Parts[1];
    }
    if (Parts.size() >= 3)
    {
        Parsed.ClassFlagsExpr = Parts[2];
    }
}

std::vector<SParsedClass> ParseReflectedEnumsInHeader(const fs::path& Header, const std::string& Contents)
{
    std::vector<SParsedClass> Enums;
    const std::string Masked = MakeMaskedCopy(Contents);
    size_t SearchPos = 0;
    while (true)
    {
        const size_t MarkerPos = Masked.find("MENUM(", SearchPos);
        if (MarkerPos == std::string::npos)
        {
            break;
        }

        size_t EnumPos = Masked.find("enum", MarkerPos);
        if (EnumPos == std::string::npos)
        {
            break;
        }

        size_t Cursor = SkipWhitespace(Masked, EnumPos + 4);
        if (StartsWith(Masked.substr(Cursor), "class"))
        {
            Cursor = SkipWhitespace(Masked, Cursor + 5);
        }

        const std::optional<std::string> EnumName = ReadIdentifier(Masked, Cursor);
        if (!EnumName)
        {
            SearchPos = EnumPos + 1;
            continue;
        }

        const size_t BraceOpen = Masked.find('{', Cursor);
        const size_t BraceClose = (BraceOpen == std::string::npos) ? std::string::npos : FindMatching(Masked, BraceOpen, '{', '}');
        if (BraceOpen == std::string::npos || BraceClose == std::string::npos)
        {
            SearchPos = Cursor;
            continue;
        }

        SParsedClass Parsed;
        Parsed.Kind = EParsedTypeKind::Enum;
        Parsed.Name = *EnumName;
        Parsed.HeaderPath = Header;

        const std::string EnumBody = Contents.substr(BraceOpen + 1, BraceClose - BraceOpen - 1);
        for (const std::string& Entry : SplitTopLevelArgs(EnumBody))
        {
            std::string Clean = Trim(Entry);
            if (Clean.empty())
            {
                continue;
            }
            const size_t EqualsPos = Clean.find('=');
            if (EqualsPos != std::string::npos)
            {
                Clean = Trim(Clean.substr(0, EqualsPos));
            }
            if (!Clean.empty())
            {
                Parsed.EnumValues.push_back(Clean);
            }
        }

        Enums.push_back(std::move(Parsed));
        SearchPos = BraceClose + 1;
    }

    return Enums;
}

std::vector<SParsedFunction> ParseFunctionsInClassBody(const std::string& ClassBody, const TRpcListMacroMap& RpcListMacros)
{
    std::vector<SParsedFunction> Functions;
    const std::string MaskedClassBody = MakeMaskedCopy(ClassBody);
    const std::vector<std::string> MacroNames = {
        "MFUNCTION(",
        "MDECLARE_SERVICE_RPC(",
        "MDECLARE_RPC_METHOD(",
        "MDECLARE_RPC_METHOD_WITH_HANDLER(",
        "MDECLARE_SERVER_HOSTED_RPC_METHOD("
    };

    size_t SearchPos = 0;
    while (SearchPos < ClassBody.size())
    {
        size_t MacroPos = std::string::npos;
        std::string MatchedMacro;
        for (const std::string& Candidate : MacroNames)
        {
            const size_t CandidatePos = ClassBody.find(Candidate, SearchPos);
            if (CandidatePos == std::string::npos)
            {
                continue;
            }

            if (MacroPos == std::string::npos || CandidatePos < MacroPos)
            {
                MacroPos = CandidatePos;
                MatchedMacro = Candidate.substr(0, Candidate.size() - 1);
            }
        }

        if (MacroPos == std::string::npos)
        {
            break;
        }

        const size_t MacroOpen = ClassBody.find('(', MacroPos);
        const size_t MacroClose = (MacroOpen == std::string::npos)
            ? std::string::npos
            : FindMatching(ClassBody, MacroOpen, '(', ')');
        if (MacroOpen == std::string::npos || MacroClose == std::string::npos)
        {
            break;
        }

        const std::string MacroArgs = ClassBody.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);
        if (MatchedMacro == "MFUNCTION")
        {
            const size_t DeclStart = MacroClose + 1;
            const size_t DeclEnd = ClassBody.find(';', DeclStart);
            if (DeclEnd == std::string::npos)
            {
                break;
            }

            const std::string Declaration = ClassBody.substr(DeclStart, DeclEnd - DeclStart + 1);
            if (Declaration.find("MethodName") != std::string::npos || Declaration.find("Signature") != std::string::npos)
            {
                SearchPos = DeclEnd + 1;
                continue;
            }

            if (auto Parsed = ParseFunctionDeclaration(MacroArgs, Declaration))
            {
                ApplyFunctionMetadataFromMacroArgs(*Parsed);
                const std::string ValidateNeedle = Parsed->Name + "_Validate(";
                Parsed->bHasValidate = (MaskedClassBody.find(ValidateNeedle) != std::string::npos);
                Functions.push_back(std::move(*Parsed));
            }

            SearchPos = DeclEnd + 1;
            continue;
        }

        if (auto Parsed = ParseWrappedFunctionMacro(MatchedMacro, MacroArgs))
        {
            Functions.push_back(std::move(*Parsed));
        }

        SearchPos = MacroClose + 1;
    }

    for (const auto& [ListMacroName, ExpandedFunctions] : RpcListMacros)
    {
        size_t MacroPos = 0;
        const std::string Invocation = ListMacroName + "(";
        while (true)
        {
            MacroPos = ClassBody.find(Invocation, MacroPos);
            if (MacroPos == std::string::npos)
            {
                break;
            }

            const size_t OpenPos = ClassBody.find('(', MacroPos);
            const size_t ClosePos = (OpenPos == std::string::npos) ? std::string::npos : FindMatching(ClassBody, OpenPos, '(', ')');
            if (ClosePos == std::string::npos)
            {
                break;
            }

            const std::string ExpanderArg = Trim(ClassBody.substr(OpenPos + 1, ClosePos - OpenPos - 1));
            if (ExpanderArg == "MDECLARE_SERVER_HOSTED_RPC_METHOD")
            {
                Functions.insert(Functions.end(), ExpandedFunctions.begin(), ExpandedFunctions.end());
            }

            MacroPos = ClosePos + 1;
        }
    }

    return Functions;
}

std::string BuildFunctionRegistrationLine(const SParsedFunction& Function)
{
    const std::string ReliableValue = Function.bReliable ? "true" : "false";
    if (Function.bIsRpc)
    {
        const std::string RpcKind = Function.RpcKind.empty() ? "Server" : Function.RpcKind;
        if (!Function.Endpoint.empty())
        {
            return "    MREGISTER_RPC_METHOD_FOR_SERVER(" + Function.Name + ", NetServer, " + RpcKind + ", " + ReliableValue + ", " + Function.Endpoint + ");";
        }

        if (Function.bHasValidate)
        {
            return "    MREGISTER_RPC_METHOD_WITH_VALIDATE(" + Function.Name + ", " + Function.Name + "_Validate, NetServer, " + RpcKind + ", " + ReliableValue + ");";
        }

        return "    MREGISTER_RPC_METHOD(" + Function.Name + ", NetServer, " + RpcKind + ", " + ReliableValue + ");";
    }

    if (Function.ReturnType == "void" && Function.Signature == "()" && !Function.bConst)
    {
        return "    MREGISTER_NATIVE_METHOD_0(" + Function.Name + ", None);";
    }

    return "    /* TODO: generate native reflection registration for " + Function.Name + " */";
}

std::vector<SParsedClass> ParseReflectedClassesInHeader(
    const fs::path& Header,
    const std::string& Contents,
    const TRpcListMacroMap& RpcListMacros)
{
    std::vector<SParsedClass> Classes;
    const std::vector<SClassRegion> Regions = ParseClassRegions(Contents);
    for (const SClassRegion& Region : Regions)
    {
        if (Region.BodyOpen == std::string::npos || Region.BodyClose == std::string::npos)
        {
            continue;
        }

        const std::string ClassBody = Contents.substr(Region.BodyOpen + 1, Region.BodyClose - Region.BodyOpen - 1);
        if (ClassBody.find("MGENERATED_BODY(") == std::string::npos)
        {
            continue;
        }

        SParsedClass Parsed;
        Parsed.Kind = (Region.Keyword == "struct") ? EParsedTypeKind::Struct : EParsedTypeKind::Class;
        Parsed.Name = Region.Name;
        Parsed.HeaderPath = Header;
        ParseGeneratedBodyMetadata(ClassBody, Parsed);
        Parsed.Properties = ParsePropertiesInTypeBody(ClassBody);
        Parsed.Functions = ParseFunctionsInClassBody(ClassBody, RpcListMacros);
        Classes.push_back(std::move(Parsed));
    }

    return Classes;
}

bool ParseArgs(int Argc, char** Argv, SOptions& OutOptions)
{
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string Arg = Argv[Index];
        if (Arg == "--verbose")
        {
            OutOptions.bVerbose = true;
            continue;
        }

        if (StartsWith(Arg, "--source-root="))
        {
            OutOptions.SourceRoot = Arg.substr(std::string("--source-root=").size());
            continue;
        }

        if (StartsWith(Arg, "--output-dir="))
        {
            OutOptions.OutputDir = Arg.substr(std::string("--output-dir=").size());
            continue;
        }

        if (StartsWith(Arg, "--cmake-manifest="))
        {
            OutOptions.CMakeManifestPath = Arg.substr(std::string("--cmake-manifest=").size());
            continue;
        }

        std::cerr << "Unknown argument: " << Arg << "\n";
        return false;
    }

    return true;
}

std::vector<fs::path> DiscoverHeaders(const fs::path& SourceRoot)
{
    std::vector<fs::path> Headers;
    if (!fs::exists(SourceRoot))
    {
        return Headers;
    }

    for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(SourceRoot))
    {
        if (!Entry.is_regular_file())
        {
            continue;
        }

        const fs::path Path = Entry.path();
        if (Path.extension() == ".h")
        {
            Headers.push_back(Path);
        }
    }

    return Headers;
}

std::string ReadFile(const fs::path& Path)
{
    std::ifstream Input(Path);
    if (!Input)
    {
        return {};
    }

    return std::string(
        std::istreambuf_iterator<char>(Input),
        std::istreambuf_iterator<char>());
}

std::vector<SParsedClass> DiscoverReflectedClasses(const std::vector<fs::path>& Headers)
{
    std::vector<SParsedClass> Classes;
    TRpcListMacroMap RpcListMacros;

    for (const fs::path& Header : Headers)
    {
        const std::string Contents = ReadFile(Header);
        TRpcListMacroMap HeaderMacros = ParseRpcListMacros(Contents);
        for (auto& [Name, Functions] : HeaderMacros)
        {
            RpcListMacros[Name] = std::move(Functions);
        }
    }

    for (const fs::path& Header : Headers)
    {
        const std::string Contents = ReadFile(Header);
        if (Contents.find("MGENERATED_BODY(") == std::string::npos &&
            Contents.find("MENUM(") == std::string::npos)
        {
            continue;
        }
        std::vector<SParsedClass> HeaderClasses = ParseReflectedClassesInHeader(Header, Contents, RpcListMacros);
        std::vector<SParsedClass> HeaderEnums = ParseReflectedEnumsInHeader(Header, Contents);
        Classes.insert(
            Classes.end(),
            std::make_move_iterator(HeaderClasses.begin()),
            std::make_move_iterator(HeaderClasses.end()));
        Classes.insert(
            Classes.end(),
            std::make_move_iterator(HeaderEnums.begin()),
            std::make_move_iterator(HeaderEnums.end()));
    }

    return Classes;
}

std::string BuildPropertyRegistrationLine(const SParsedProperty& Property)
{
    return "    MREGISTER_PROPERTY(" + Property.Type + ", " + Property.PropertyKind + ", " + Property.Name + ", " + Property.FlagsExpr + ");";
}

const char* GetTypeKindName(EParsedTypeKind Kind)
{
    switch (Kind)
    {
    case EParsedTypeKind::Class:
        return "class";
    case EParsedTypeKind::Struct:
        return "struct";
    case EParsedTypeKind::Enum:
        return "enum";
    }
    return "unknown";
}

std::string MakeIncludePathFromHeader(const fs::path& HeaderPath)
{
    fs::path IncludePath = HeaderPath;
    if (IncludePath.is_absolute())
    {
        std::error_code Error;
        IncludePath = fs::relative(IncludePath, fs::current_path(), Error);
        if (Error)
        {
            IncludePath = HeaderPath.filename();
        }
    }

    if (!IncludePath.empty() && IncludePath.begin()->string() == "Source")
    {
        IncludePath = fs::relative(IncludePath, "Source");
    }

    return IncludePath.generic_string();
}

std::string EscapeCMakePath(const fs::path& Path)
{
    return ReplaceAll(Path.generic_string(), "\\", "/");
}

std::string DetermineGeneratedGroup(const SParsedClass& ParsedClass)
{
    const std::string Header = EscapeCMakePath(ParsedClass.HeaderPath);
    if (Header.find("Servers/Gateway/") != std::string::npos)
    {
        return "gateway";
    }
    if (Header.find("Servers/Login/") != std::string::npos)
    {
        return "login";
    }
    if (Header.find("Servers/World/") != std::string::npos)
    {
        return "world";
    }
    if (Header.find("Servers/Router/") != std::string::npos)
    {
        return "router";
    }
    if (Header.find("Servers/Scene/") != std::string::npos)
    {
        return "scene";
    }
    if (Header.find("Servers/Mono/") != std::string::npos)
    {
        return "mono";
    }
    return "shared";
}

void WriteGeneratedHeader(std::ofstream& Out, const SParsedClass& ParsedClass)
{
    Out << "#pragma once\n";
    Out << "// Generated by MHeaderTool\n";
    Out << "// Source: " << ParsedClass.HeaderPath.string() << "\n";
    Out << "// Reflected " << GetTypeKindName(ParsedClass.Kind) << ": " << ParsedClass.Name << "\n";
    Out << "\n";
    if (ParsedClass.Kind == EParsedTypeKind::Enum)
    {
        Out << "inline void MHeaderTool_Generated_RegisterEnum_" << ParsedClass.Name << "()\n";
        Out << "{\n";
        Out << "    // TODO: emit MENUM registration glue.\n";
        Out << "}\n";
        return;
    }

    Out << "#define MHEADERTOOL_REGISTER_PROPERTIES_" << ParsedClass.Name << "() \\\n";
    if (ParsedClass.Properties.empty())
    {
        Out << "    do { \\\n";
        Out << "        (void)InClass; \\\n";
        Out << "    } while (0)\n";
    }
    else
    {
        Out << "    do { \\\n";
        for (const SParsedProperty& Property : ParsedClass.Properties)
        {
            Out << ReplaceAll(BuildPropertyRegistrationLine(Property), "    ", "        ") << " \\\n";
        }
        Out << "    } while (0)\n";
    }
    Out << "\n";
    Out << "#define MHEADERTOOL_REGISTER_FUNCTIONS_" << ParsedClass.Name << "() \\\n";
    if (ParsedClass.Functions.empty())
    {
        Out << "    do { \\\n";
        Out << "        (void)InClass; \\\n";
        Out << "    } while (0)\n";
    }
    else
    {
        Out << "    do { \\\n";
        for (const SParsedFunction& Function : ParsedClass.Functions)
        {
            Out << ReplaceAll(BuildFunctionRegistrationLine(Function), "    ", "        ") << " \\\n";
        }
        Out << "    } while (0)\n";
    }
}

void WriteGeneratedSource(std::ofstream& Out, const SParsedClass& ParsedClass)
{
    const std::string IncludeName = MakeIncludePathFromHeader(ParsedClass.HeaderPath);
    const std::string GeneratedHeaderInclude = "Build/Generated/" + SanitizeIdentifier(ParsedClass.Name) + ".mgenerated.h";
    Out << "// Generated by MHeaderTool\n";
    Out << "// Source: " << ParsedClass.HeaderPath.string() << "\n";
    Out << "#include \"" << IncludeName << "\"\n";
    Out << "#include \"" << GeneratedHeaderInclude << "\"\n";
    Out << "\n";
    Out << "\n";

    if (ParsedClass.Kind == EParsedTypeKind::Enum)
    {
        Out << "void MHeaderTool_Generated_TouchEnum_" << ParsedClass.Name << "()\n";
        Out << "{\n";
        Out << "    MHeaderTool_Generated_RegisterEnum_" << ParsedClass.Name << "();\n";
        Out << "}\n";
        return;
    }

    Out << "void " << ParsedClass.Name << "::RegisterAllProperties(MClass* InClass)\n";
    Out << "{\n";
    Out << "    MHEADERTOOL_REGISTER_PROPERTIES_" << ParsedClass.Name << "();\n";
    Out << "}\n";
    Out << "\n";
    Out << "void " << ParsedClass.Name << "::RegisterAllFunctions(MClass* InClass)\n";
    Out << "{\n";
    Out << "    MHEADERTOOL_REGISTER_FUNCTIONS_" << ParsedClass.Name << "();\n";
    Out << "}\n";
    Out << "\n";
    Out << "MClass* " << ParsedClass.Name << "::StaticClass()\n";
    Out << "{\n";
    Out << "    static MClass* Class = nullptr;\n";
    Out << "    if (!Class)\n";
    Out << "    {\n";
    Out << "        Class = new MClass();\n";
    Out << "        Class->SetMeta(\"" << ParsedClass.Name << "\", \"" << ParsedClass.HeaderPath.generic_string() << "\", nullptr, " << ParsedClass.ClassFlagsExpr << ");\n";
    Out << "        Class->SetConstructor<" << ParsedClass.Name << ">();\n";
    Out << "        " << ParsedClass.Name << "::RegisterAllProperties(Class);\n";
    Out << "        " << ParsedClass.Name << "::RegisterAllFunctions(Class);\n";
    Out << "        MReflectObject::RegisterClass(Class);\n";
    Out << "    }\n";
    Out << "    return Class;\n";
    Out << "}\n";
}

bool WriteGeneratedFiles(const fs::path& OutputDir, const std::vector<SParsedClass>& Classes)
{
    std::error_code Error;
    fs::create_directories(OutputDir, Error);
    if (Error)
    {
        std::cerr << "Failed to create output directory: " << OutputDir << "\n";
        return false;
    }

    for (const fs::directory_entry& Entry : fs::directory_iterator(OutputDir))
    {
        if (!Entry.is_regular_file())
        {
            continue;
        }

        const fs::path Path = Entry.path();
        const std::string Filename = Path.filename().string();
        if (Filename.find(".mgenerated.") == std::string::npos)
        {
            continue;
        }

        std::error_code RemoveError;
        fs::remove(Path, RemoveError);
        if (RemoveError)
        {
            std::cerr << "Failed to remove stale generated file: " << Path << "\n";
            return false;
        }
    }

    for (const SParsedClass& ParsedClass : Classes)
    {
        const fs::path BasePath = OutputDir / SanitizeIdentifier(ParsedClass.Name);
        const fs::path GeneratedHeader = BasePath.string() + ".mgenerated.h";
        const fs::path GeneratedSource = BasePath.string() + ".mgenerated.cpp";

        {
            std::ofstream Out(GeneratedHeader);
            if (!Out)
            {
                std::cerr << "Failed to write generated header: " << GeneratedHeader << "\n";
                return false;
            }

            WriteGeneratedHeader(Out, ParsedClass);
        }

        {
            std::ofstream Out(GeneratedSource);
            if (!Out)
            {
                std::cerr << "Failed to write generated source: " << GeneratedSource << "\n";
                return false;
            }

            WriteGeneratedSource(Out, ParsedClass);
        }
    }

    return true;
}

bool WriteCMakeManifest(const fs::path& ManifestPath, const fs::path& OutputDir, const std::vector<SParsedClass>& Classes)
{
    std::error_code Error;
    if (ManifestPath.has_parent_path())
    {
        fs::create_directories(ManifestPath.parent_path(), Error);
        if (Error)
        {
            std::cerr << "Failed to create manifest directory: " << ManifestPath.parent_path() << "\n";
            return false;
        }
    }

    std::map<std::string, std::vector<fs::path>> GroupedSources;
    for (const SParsedClass& ParsedClass : Classes)
    {
        const std::string Group = DetermineGeneratedGroup(ParsedClass);
        GroupedSources[Group].push_back(OutputDir / (SanitizeIdentifier(ParsedClass.Name) + ".mgenerated.cpp"));
    }

    std::ofstream Out(ManifestPath);
    if (!Out)
    {
        std::cerr << "Failed to write CMake manifest: " << ManifestPath << "\n";
        return false;
    }

    Out << "# Generated by MHeaderTool. Do not edit manually.\n";
    const std::vector<std::string> GroupNames = {"shared", "gateway", "login", "world", "router", "scene", "mono"};
    for (const std::string& Group : GroupNames)
    {
        const std::string UpperGroup = ReplaceAll(Group, "-", "_");
        std::string VarName = "MESSION_GENERATED_" + UpperGroup + "_SOURCES";
        for (char& Ch : VarName)
        {
            Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
        }

        Out << "set(" << VarName << "\n";
        auto It = GroupedSources.find(Group);
        if (It != GroupedSources.end())
        {
            for (const fs::path& SourcePath : It->second)
            {
                Out << "    \"" << EscapeCMakePath(SourcePath) << "\"\n";
            }
        }
        Out << ")\n";
    }

    return true;
}
} // namespace

int main(int Argc, char** Argv)
{
    SOptions Options;
    if (!ParseArgs(Argc, Argv, Options))
    {
        std::cerr << "Usage: MHeaderTool [--source-root=Source] [--output-dir=Build/Generated] [--verbose]\n";
        return 1;
    }

    const std::vector<fs::path> Headers = DiscoverHeaders(Options.SourceRoot);
    const std::vector<SParsedClass> Classes = DiscoverReflectedClasses(Headers);

    if (Options.bVerbose)
    {
        std::cout << "MHeaderTool source root: " << Options.SourceRoot << "\n";
        std::cout << "MHeaderTool output dir: " << Options.OutputDir << "\n";
        std::cout << "Headers discovered: " << Headers.size() << "\n";
        std::cout << "Reflected types discovered: " << Classes.size() << "\n";
        for (const SParsedClass& ParsedClass : Classes)
        {
            std::cout << "  " << ParsedClass.Name
                      << " (" << ParsedClass.HeaderPath << ")"
                      << " kind=" << GetTypeKindName(ParsedClass.Kind)
                      << " properties=" << ParsedClass.Properties.size()
                      << " functions=" << ParsedClass.Functions.size() << "\n";
        }
    }

    if (!WriteGeneratedFiles(Options.OutputDir, Classes))
    {
        return 1;
    }

    if (!WriteCMakeManifest(Options.CMakeManifestPath, Options.OutputDir, Classes))
    {
        return 1;
    }

    std::cout << "MHeaderTool discovered " << Classes.size() << " reflected classes.\n";
    return 0;
}
