#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
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
    struct SParsedParameter
    {
        std::string Type;
        std::string StorageType;
        std::string Name;
        std::string PropertyKind;
    };

    std::string MacroArgs;
    std::string ReturnType;
    std::string ReturnStorageType;
    std::string ReturnPropertyKind;
    std::string Name;
    std::string Signature;
    std::string Owner;
    bool bConst = false;
    bool bHasValidate = false;
    bool bIsRpc = false;
    bool bReliable = true;
    std::string Transport;
    std::string RpcKind;
    std::string Endpoint;
    std::string MessageName;
    std::string Route;
    std::string Target;
    std::string Auth;
    std::string Wrap;
    std::string ClientApi;
    std::vector<SParsedParameter> Params;
};

struct SParsedProperty
{
    struct SMetadataEntry
    {
        std::string Key;
        std::string Value;
    };

    std::string MacroArgs;
    std::string Type;
    std::string Name;
    std::string PropertyKind;
    std::string FlagsExpr;
    std::string Owner;
    std::vector<SMetadataEntry> Metadata;
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
    std::string ParentClass = "MObject";
    std::string ClassFlagsExpr = "0";
    std::string ReflectionType = "Object";
    std::string Owner;
    bool bScopedEnum = false;
    std::map<std::string, std::string> TypeAliases;
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
std::optional<std::string> ExtractMacroValue(const std::string& MacroArgs, std::string_view Key);
std::string DetermineOwnerFromHeaderPath(const fs::path& HeaderPath);
std::string NormalizeReflectionType(std::string TypeName);
std::string InferPropertyKind(const std::string& TypeName);
std::optional<std::string> ExtractServerCallResponseType(const SParsedFunction& Function);
std::optional<std::string> ExtractRpcClassTarget(const SParsedClass& ParsedClass);
std::vector<SParsedFunction::SParsedParameter> ParseFunctionParameters(const std::string& Signature);
std::map<std::string, std::string> ParseTypeAliasesInBody(const std::string& ClassBody);
std::string BuildClassKindExpr(const SParsedClass& ParsedClass);
std::string Trim(std::string_view Text);

std::string StripEnclosingPair(std::string Value, char Open, char Close)
{
    Value = Trim(Value);
    if (Value.size() >= 2 && Value.front() == Open && Value.back() == Close)
    {
        return Trim(Value.substr(1, Value.size() - 2));
    }
    return Value;
}

std::string UnquoteStringLiteral(std::string Value)
{
    Value = Trim(Value);
    if (Value.size() >= 2 && Value.front() == '"' && Value.back() == '"')
    {
        return Value.substr(1, Value.size() - 2);
    }
    return Value;
}

std::vector<SParsedProperty::SMetadataEntry> ParsePropertyMetadataEntries(const std::string& MacroArgs)
{
    const std::optional<std::string> MetaValue = ExtractMacroValue(MacroArgs, "Meta");
    if (!MetaValue.has_value())
    {
        return {};
    }

    const std::string Inner = StripEnclosingPair(*MetaValue, '(', ')');
    std::vector<SParsedProperty::SMetadataEntry> Entries;
    for (const std::string& Part : SplitTopLevelArgs(Inner))
    {
        if (Part.empty())
        {
            continue;
        }

        const size_t EqualsPos = Part.find('=');
        SParsedProperty::SMetadataEntry Entry;
        if (EqualsPos == std::string::npos)
        {
            Entry.Key = Trim(Part);
            Entry.Value = "true";
        }
        else
        {
            Entry.Key = Trim(Part.substr(0, EqualsPos));
            Entry.Value = UnquoteStringLiteral(Trim(Part.substr(EqualsPos + 1)));
        }

        if (!Entry.Key.empty())
        {
            Entries.push_back(std::move(Entry));
        }
    }

    return Entries;
}

bool StartsWith(std::string_view Text, std::string_view Prefix)
{
    return Text.substr(0, Prefix.size()) == Prefix;
}

bool IsMacroDefinitionAt(const std::string& Text, size_t Pos)
{
    const size_t LineStart = Text.rfind('\n', Pos);
    const size_t PrefixStart = (LineStart == std::string::npos) ? 0 : (LineStart + 1);
    size_t Cursor = PrefixStart;
    while (Cursor < Pos && std::isspace(static_cast<unsigned char>(Text[Cursor])))
    {
        ++Cursor;
    }
    return StartsWith(Text.substr(Cursor, Pos - Cursor), "#define");
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

std::string EscapeCppStringLiteral(std::string Value)
{
    Value = ReplaceAll(std::move(Value), "\\", "\\\\");
    Value = ReplaceAll(std::move(Value), "\"", "\\\"");
    return Value;
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
    Parsed.ReturnStorageType = NormalizeReflectionType(Parsed.ReturnType);
    Parsed.ReturnPropertyKind = (Parsed.ReturnStorageType == "void")
        ? "None"
        : InferPropertyKind(Parsed.ReturnStorageType);
    Parsed.Params = ParseFunctionParameters(Parsed.Signature);

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
    if (const auto Owner = ExtractMacroValue(Parsed.MacroArgs, "Owner"))
    {
        Parsed.Owner = *Owner;
    }
    Parsed.Metadata = ParsePropertyMetadataEntries(Parsed.MacroArgs);
    return Parsed;
}

void ApplyFunctionMetadataFromMacroArgs(SParsedFunction& Parsed)
{
    if (const auto Owner = ExtractMacroValue(Parsed.MacroArgs, "Owner"))
    {
        Parsed.Owner = *Owner;
    }

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
                Parsed.Transport = Part;
            }
            else if (Part == "Client")
            {
                Parsed.Transport = "Client";
            }
            else if (Part == "ClientCall")
            {
                Parsed.Transport = "ClientCall";
            }
            else if (Part == "ServerCall")
            {
                Parsed.Transport = "ServerCall";
            }
            else if (Part == "RPC")
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
        else if (Key == "Message")
        {
            Parsed.MessageName = Value;
            if (Parsed.Transport.empty())
            {
                Parsed.Transport = "Client";
            }
        }
        else if (Key == "Channel")
        {
            Parsed.Transport = Value;
        }
        else if (Key == "Route")
        {
            Parsed.Route = Value;
        }
        else if (Key == "Auth")
        {
            Parsed.Auth = Value;
        }
        else if (Key == "Target")
        {
            Parsed.Target = Value;
        }
        else if (Key == "Wrap")
        {
            Parsed.Wrap = Value;
        }
        else if (Key == "Api" || Key == "ClientApi")
        {
            Parsed.ClientApi = Value;
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

std::string NormalizeReflectionType(std::string TypeName)
{
    TypeName = Trim(TypeName);

    while (StartsWith(TypeName, "const "))
    {
        TypeName = Trim(TypeName.substr(6));
    }

    while (!TypeName.empty() && (TypeName.back() == '&' || TypeName.back() == '*'))
    {
        TypeName.pop_back();
        TypeName = Trim(TypeName);
    }

    return TypeName;
}

std::string BuildPropertyFlagsExpr(const std::string& MacroArgs)
{
    const std::vector<std::string> Args = SplitTopLevelArgs(MacroArgs);
    std::vector<std::string> Parts;
    for (const std::string& Arg : Args)
    {
        if (Arg.empty() || Arg.find('=') != std::string::npos)
        {
            continue;
        }

        const std::vector<std::string> Tokens = SplitTopLevelPipes(Arg);
        for (const std::string& Token : Tokens)
        {
            if (Token.empty())
            {
                continue;
            }
            Parts.push_back("static_cast<uint64>(EPropertyFlags::" + Token + ")");
        }
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

std::string BuildFunctionFlagsExpr(const SParsedFunction& Function)
{
    std::vector<std::string> Parts;
    const std::vector<std::string> Tokens = SplitTopLevelArgs(Function.MacroArgs);
    for (const std::string& Token : Tokens)
    {
        if (Token.empty() || Token.find('=') != std::string::npos)
        {
            continue;
        }
        const std::vector<std::string> FlagTokens = SplitTopLevelPipes(Token);
        for (const std::string& FlagToken : FlagTokens)
        {
            if (FlagToken.empty() || FlagToken == "NetServer" || FlagToken == "NetClient" ||
                FlagToken == "Client" || FlagToken == "ClientCall" || FlagToken == "ServerCall" || FlagToken == "RPC")
            {
                continue;
            }
            Parts.push_back("static_cast<uint32>(EFunctionFlags::" + FlagToken + ")");
        }
    }

    if (Function.bConst)
    {
        Parts.push_back("static_cast<uint32>(EFunctionFlags::Const)");
    }

    if (Parts.empty())
    {
        return "EFunctionFlags::None";
    }

    std::string Expr = "static_cast<EFunctionFlags>(";
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

std::optional<std::string> ExtractMacroValue(const std::string& MacroArgs, std::string_view Key)
{
    const std::vector<std::string> Parts = SplitTopLevelArgs(MacroArgs);
    for (const std::string& Part : Parts)
    {
        const size_t EqualsPos = Part.find('=');
        if (EqualsPos == std::string::npos)
        {
            continue;
        }

        const std::string CandidateKey = Trim(Part.substr(0, EqualsPos));
        if (CandidateKey != Key)
        {
            continue;
        }

        return Trim(Part.substr(EqualsPos + 1));
    }

    return std::nullopt;
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
    if (Compact == "MString")
    {
        return "String";
    }
    if (Compact == "MName")
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
    if (StartsWith(Compact, "TVector<") || StartsWith(Compact, "TByteArray<") ||
        StartsWith(Compact, "TMap<") || StartsWith(Compact, "TSet<"))
    {
        return "Array";
    }
    return "Struct";
}

std::string ResolveAliasedType(const std::map<std::string, std::string>& TypeAliases, const std::string& TypeName)
{
    std::string Resolved = NormalizeReflectionType(TypeName);
    std::set<std::string> Visited;
    while (true)
    {
        if (!Visited.insert(Resolved).second)
        {
            break;
        }

        auto It = TypeAliases.find(Resolved);
        if (It == TypeAliases.end())
        {
            break;
        }
        Resolved = NormalizeReflectionType(It->second);
    }
    return Resolved;
}

bool IsNonConstLValueReferenceParameter(const SParsedFunction::SParsedParameter& Param)
{
    const std::string Type = Trim(Param.Type);
    return Type.find('&') != std::string::npos && !StartsWith(Type, "const ");
}

bool IsReflectedStructLikeType(const std::vector<SParsedClass>& Classes, const std::string& TypeName)
{
    const std::string NormalizedType = NormalizeReflectionType(TypeName);
    for (const SParsedClass& ParsedClass : Classes)
    {
        if (ParsedClass.Name == NormalizedType)
        {
            return ParsedClass.Kind == EParsedTypeKind::Struct || ParsedClass.Kind == EParsedTypeKind::Class;
        }
    }

    return InferPropertyKind(NormalizedType) == "Struct";
}

void ValidateClientCallFunction(const std::vector<SParsedClass>& Classes, const SParsedClass& ParsedClass, const SParsedFunction& Function)
{
    if (ParsedClass.ReflectionType != "Server" && ParsedClass.ReflectionType != "Service")
    {
        throw std::runtime_error("ClientCall function must belong to Type=Server or Type=Service: " +
                                 ParsedClass.Name + "::" + Function.Name);
    }

    if (Function.ReturnStorageType != "void")
    {
        throw std::runtime_error("ClientCall function must return void: " + ParsedClass.Name + "::" + Function.Name);
    }

    if (Function.Target.empty())
    {
        throw std::runtime_error("ClientCall function must declare Target=...: " + ParsedClass.Name + "::" + Function.Name);
    }

    if (Function.Params.size() != 2)
    {
        throw std::runtime_error("ClientCall function must declare exactly request/response params: " + ParsedClass.Name + "::" + Function.Name);
    }

    const auto& RequestParam = Function.Params[0];
    const auto& ResponseParam = Function.Params[1];
    if (!IsNonConstLValueReferenceParameter(RequestParam) || !IsNonConstLValueReferenceParameter(ResponseParam))
    {
        throw std::runtime_error("ClientCall params must be non-const lvalue refs: " + ParsedClass.Name + "::" + Function.Name);
    }

    if (!IsReflectedStructLikeType(Classes, RequestParam.StorageType) || !IsReflectedStructLikeType(Classes, ResponseParam.StorageType))
    {
        throw std::runtime_error("ClientCall request/response must use reflected struct types: " + ParsedClass.Name + "::" + Function.Name);
    }
}

void ValidateServerCallFunction(const std::vector<SParsedClass>& Classes, const SParsedClass& ParsedClass, const SParsedFunction& Function)
{
    if (Function.Params.size() != 1)
    {
        throw std::runtime_error("ServerCall function must declare exactly one request param: " + ParsedClass.Name + "::" + Function.Name);
    }

    const bool bIsRpcOwner = ParsedClass.ReflectionType == "Rpc";
    const bool bIsServiceOwner = ParsedClass.ReflectionType == "Service" || ParsedClass.ReflectionType == "Server";
    const bool bIsObjectOwner = ParsedClass.ReflectionType == "Object";
    if (bIsRpcOwner && Function.Target.empty())
    {
        throw std::runtime_error("Rpc ServerCall function must declare Target=...: " + ParsedClass.Name + "::" + Function.Name);
    }

    if ((bIsServiceOwner || bIsObjectOwner) && !Function.Target.empty())
    {
        throw std::runtime_error("Object/Service/Server ServerCall function must not declare Target=...: " + ParsedClass.Name + "::" + Function.Name);
    }

    if (!bIsRpcOwner && !bIsServiceOwner && !bIsObjectOwner)
    {
        throw std::runtime_error("ServerCall function must belong to Type=Object, Type=Server, Type=Service, or Type=Rpc: " + ParsedClass.Name + "::" + Function.Name);
    }

    if (!IsReflectedStructLikeType(Classes, Function.Params[0].StorageType))
    {
        throw std::runtime_error("ServerCall request must use reflected struct type: " + ParsedClass.Name + "::" + Function.Name);
    }

    const std::optional<std::string> ResponseType = ExtractServerCallResponseType(Function);
    if (!ResponseType.has_value())
    {
        throw std::runtime_error("ServerCall function must return MFuture<TResult<Response, FAppError>>: " + ParsedClass.Name + "::" + Function.Name);
    }

    if (!IsReflectedStructLikeType(Classes, *ResponseType))
    {
        throw std::runtime_error("ServerCall response must use reflected struct type: " + ParsedClass.Name + "::" + Function.Name);
    }
}

std::vector<SParsedFunction::SParsedParameter> ParseFunctionParameters(const std::string& Signature)
{
    std::vector<SParsedFunction::SParsedParameter> Params;
    const size_t OpenParen = Signature.find('(');
    const size_t CloseParen = (OpenParen == std::string::npos)
        ? std::string::npos
        : FindMatching(Signature, OpenParen, '(', ')');
    if (OpenParen == std::string::npos || CloseParen == std::string::npos)
    {
        return Params;
    }

    const std::string ParamList = Trim(Signature.substr(OpenParen + 1, CloseParen - OpenParen - 1));
    if (ParamList.empty() || ParamList == "void")
    {
        return Params;
    }

    for (std::string Entry : SplitTopLevelArgs(ParamList))
    {
        Entry = Trim(Entry);
        if (Entry.empty())
        {
            continue;
        }

        const size_t EqualsPos = Entry.find('=');
        if (EqualsPos != std::string::npos)
        {
            Entry = Trim(Entry.substr(0, EqualsPos));
        }

        size_t NameEnd = Entry.size();
        while (NameEnd > 0 && std::isspace(static_cast<unsigned char>(Entry[NameEnd - 1])))
        {
            --NameEnd;
        }

        size_t NameStart = NameEnd;
        while (NameStart > 0 && IsIdentifierChar(Entry[NameStart - 1]))
        {
            --NameStart;
        }

        if (NameStart == NameEnd)
        {
            continue;
        }

        SParsedFunction::SParsedParameter ParsedParam;
        ParsedParam.Name = Entry.substr(NameStart, NameEnd - NameStart);
        ParsedParam.Type = Trim(Entry.substr(0, NameStart));
        ParsedParam.StorageType = NormalizeReflectionType(ParsedParam.Type);
        ParsedParam.PropertyKind = InferPropertyKind(ParsedParam.StorageType);
        Params.push_back(std::move(ParsedParam));
    }

    return Params;
}

std::map<std::string, std::string> ParseTypeAliasesInBody(const std::string& ClassBody)
{
    std::map<std::string, std::string> Aliases;
    size_t SearchPos = 0;
    while (true)
    {
        SearchPos = ClassBody.find("using ", SearchPos);
        if (SearchPos == std::string::npos)
        {
            break;
        }

        size_t NameCursor = SearchPos + 6;
        const std::optional<std::string> AliasName = ReadIdentifier(ClassBody, NameCursor);
        if (!AliasName)
        {
            SearchPos += 6;
            continue;
        }

        size_t Cursor = NameCursor;
        Cursor = SkipWhitespace(ClassBody, Cursor);
        if (Cursor >= ClassBody.size() || ClassBody[Cursor] != '=')
        {
            SearchPos = Cursor;
            continue;
        }

        const size_t SemiPos = ClassBody.find(';', Cursor);
        if (SemiPos == std::string::npos)
        {
            break;
        }

        Aliases[*AliasName] = Trim(ClassBody.substr(Cursor + 1, SemiPos - Cursor - 1));
        SearchPos = SemiPos + 1;
    }

    return Aliases;
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
        Parsed.ReturnStorageType = "void";
        Parsed.ReturnPropertyKind = "None";
        Parsed.Name = Parts[0];
        Parsed.Signature = Parts[1];
        Parsed.MacroArgs = Parts[2] + ", Rpc=" + Parts[3] + ", Reliable=" + Parts[4] + ", Handler=true";
        Parsed.bIsRpc = true;
        Parsed.RpcKind = Parts[3];
        Parsed.bReliable = ParseBoolLiteral(Parts[4], true);
        Parsed.Params = ParseFunctionParameters(Parsed.Signature);
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
        Parsed.ReturnStorageType = "void";
        Parsed.ReturnPropertyKind = "None";
        Parsed.Name = Parts[MethodIndex];
        Parsed.Signature = Parts[SignatureIndex];
        Parsed.MacroArgs = Parts[FlagsIndex] + ", Rpc=" + Parts[RpcIndex] + ", Reliable=" + Parts[ReliableIndex];
        Parsed.bIsRpc = true;
        Parsed.RpcKind = Parts[RpcIndex];
        Parsed.bReliable = ParseBoolLiteral(Parts[ReliableIndex], true);
        Parsed.Params = ParseFunctionParameters(Parsed.Signature);
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
        Parsed.ReturnStorageType = "void";
        Parsed.ReturnPropertyKind = "None";
        Parsed.Name = Parts[2];
        Parsed.Signature = Parts[3];
        Parsed.MacroArgs =
            Parts[4] + ", Rpc=" + Parts[5] + ", Reliable=" + Parts[6] + ", Endpoint=" + Parts[1];
        Parsed.bIsRpc = true;
        Parsed.RpcKind = Parts[5];
        Parsed.bReliable = ParseBoolLiteral(Parts[6], true);
        Parsed.Endpoint = Parts[1];
        Parsed.Params = ParseFunctionParameters(Parsed.Signature);
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

        const size_t NameEnd = Cursor;
        const size_t SuffixCursor = SkipWhitespace(Masked, NameEnd);
        if (SuffixCursor < Masked.size() && Masked[SuffixCursor] == '<')
        {
            SearchPos = SuffixCursor + 1;
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

void ParseTypeMarkerMetadata(const std::string& Contents, const SClassRegion& Region, SParsedClass& Parsed)
{
    const char* Marker = (Parsed.Kind == EParsedTypeKind::Struct) ? "MSTRUCT(" : "MCLASS(";
    const size_t SearchStart = (Region.BodyOpen > 512) ? (Region.BodyOpen - 512) : 0;
    const size_t MarkerPos = Contents.rfind(Marker, Region.BodyOpen);
    if (MarkerPos == std::string::npos || MarkerPos < SearchStart)
    {
        return;
    }

    const size_t MacroOpen = Contents.find('(', MarkerPos);
    const size_t MacroClose = (MacroOpen == std::string::npos)
        ? std::string::npos
        : FindMatching(Contents, MacroOpen, '(', ')');
    if (MacroOpen == std::string::npos || MacroClose == std::string::npos || MacroClose > Region.BodyOpen)
    {
        return;
    }

    const std::string MacroArgs = Contents.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);
    if (const auto Owner = ExtractMacroValue(MacroArgs, "Owner"))
    {
        Parsed.Owner = *Owner;
    }
    if (const auto Type = ExtractMacroValue(MacroArgs, "Type"))
    {
        Parsed.ReflectionType = *Type;
    }
    else if (Parsed.Kind == EParsedTypeKind::Struct)
    {
        Parsed.ReflectionType = "Struct";
    }
}

bool HasNearbyTypeMarker(const std::string& Contents, const SClassRegion& Region, EParsedTypeKind Kind)
{
    const char* Marker = (Kind == EParsedTypeKind::Struct) ? "MSTRUCT(" : "MCLASS(";
    const size_t SearchStart = (Region.BodyOpen > 512) ? (Region.BodyOpen - 512) : 0;
    const size_t MarkerPos = Contents.rfind(Marker, Region.BodyOpen);
    if (MarkerPos == std::string::npos || MarkerPos < SearchStart)
    {
        return false;
    }

    return !IsMacroDefinitionAt(Contents, MarkerPos);
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
        if (IsMacroDefinitionAt(Contents, MarkerPos))
        {
            SearchPos = MarkerPos + 1;
            continue;
        }

        size_t EnumPos = Masked.find("enum", MarkerPos);
        if (EnumPos == std::string::npos)
        {
            break;
        }

        size_t Cursor = SkipWhitespace(Masked, EnumPos + 4);
        bool bScopedEnum = false;
        if (StartsWith(Masked.substr(Cursor), "class"))
        {
            bScopedEnum = true;
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
        Parsed.Owner = DetermineOwnerFromHeaderPath(Header);
        Parsed.bScopedEnum = bScopedEnum;
        const size_t MacroOpen = Masked.find('(', MarkerPos);
        const size_t MacroClose = (MacroOpen == std::string::npos) ? std::string::npos : FindMatching(Masked, MacroOpen, '(', ')');
        if (MacroOpen != std::string::npos && MacroClose != std::string::npos)
        {
            const std::string MacroArgs = Contents.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);
            if (const auto Owner = ExtractMacroValue(MacroArgs, "Owner"))
            {
                Parsed.Owner = *Owner;
            }
        }

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

std::string BuildFunctionParamStructName(const SParsedClass& ParsedClass, const SParsedFunction& Function)
{
    return "MHeaderTool_Params_" + SanitizeIdentifier(ParsedClass.Name) + "_" + SanitizeIdentifier(Function.Name);
}

std::string BuildClientBinderFunctionName(const SParsedClass& ParsedClass, const SParsedFunction& Function)
{
    return "MHeaderTool_BindClientParams_" + SanitizeIdentifier(ParsedClass.Name) + "_" + SanitizeIdentifier(Function.Name);
}

std::string BuildClientCallHandlerFunctionName(const SParsedClass& ParsedClass, const SParsedFunction& Function)
{
    return "MHeaderTool_HandleClientCall_" + SanitizeIdentifier(ParsedClass.Name) + "_" + SanitizeIdentifier(Function.Name);
}

std::string BuildServerCallHandlerFunctionName(const SParsedClass& ParsedClass, const SParsedFunction& Function)
{
    return "MHeaderTool_HandleServerCall_" + SanitizeIdentifier(ParsedClass.Name) + "_" + SanitizeIdentifier(Function.Name);
}

std::optional<std::string> ExtractServerCallResponseType(const SParsedFunction& Function)
{
    const std::string ReturnType = NormalizeReflectionType(Function.ReturnStorageType);
    if (!StartsWith(ReturnType, "MFuture<") || ReturnType.back() != '>')
    {
        return std::nullopt;
    }

    const std::string FutureInner = Trim(ReturnType.substr(8, ReturnType.size() - 9));
    if (!StartsWith(FutureInner, "TResult<") || FutureInner.back() != '>')
    {
        return std::nullopt;
    }

    const std::string ResultInner = Trim(FutureInner.substr(8, FutureInner.size() - 9));
    const std::vector<std::string> ResultArgs = SplitTopLevelArgs(ResultInner);
    if (ResultArgs.size() != 2)
    {
        return std::nullopt;
    }

    if (NormalizeReflectionType(ResultArgs[1]) != "FAppError")
    {
        return std::nullopt;
    }

    return NormalizeReflectionType(ResultArgs[0]);
}

std::optional<std::string> ExtractRpcClassTarget(const SParsedClass& ParsedClass)
{
    if (ParsedClass.ReflectionType != "Rpc")
    {
        return std::nullopt;
    }

    std::optional<std::string> ClassTarget;
    for (const SParsedFunction& Function : ParsedClass.Functions)
    {
        if (Function.Transport != "ServerCall")
        {
            continue;
        }

        if (!ClassTarget.has_value())
        {
            ClassTarget = Function.Target;
            continue;
        }

        if (*ClassTarget != Function.Target)
        {
            throw std::runtime_error(
                "Rpc class must use a single Target across all ServerCall methods: " +
                ParsedClass.Name + " (" + *ClassTarget + " vs " + Function.Target + ")");
        }
    }

    return ClassTarget;
}

std::string BuildClassKindExpr(const SParsedClass& ParsedClass)
{
    if (ParsedClass.Kind == EParsedTypeKind::Struct)
    {
        return "EClassKind::Struct";
    }
    if (ParsedClass.ReflectionType == "Server")
    {
        return "EClassKind::Server";
    }
    if (ParsedClass.ReflectionType == "Service")
    {
        return "EClassKind::Service";
    }
    if (ParsedClass.ReflectionType == "Rpc")
    {
        return "EClassKind::Rpc";
    }
    return "EClassKind::Object";
}

std::string BuildFunctionRegistrationBlock(const SParsedClass& ParsedClass, const SParsedFunction& Function)
{
    const std::string ReliableValue = Function.bReliable ? "true" : "false";
    const std::string ParamStructName = BuildFunctionParamStructName(ParsedClass, Function);
    const std::string BinderFunctionName = BuildClientBinderFunctionName(ParsedClass, Function);
    const std::string ClientCallHandlerFunctionName = BuildClientCallHandlerFunctionName(ParsedClass, Function);
    const std::string ServerCallHandlerFunctionName = BuildServerCallHandlerFunctionName(ParsedClass, Function);
    const std::string ClientApiName = Function.ClientApi.empty() ? Function.Name : Function.ClientApi;
    std::ostringstream Out;
    if (Function.Transport == "Client" && !Function.Route.empty())
    {
        Out << "    do {\n";
        Out << "        auto* Func = new MFunction();\n";
        Out << "        Func->Name = \"" << Function.Name << "\";\n";
        Out << "        Func->Flags = " << BuildFunctionFlagsExpr(Function) << ";\n";
        Out << "        Func->ParamSize = sizeof(" << ParamStructName << ");\n";
        for (const auto& Param : Function.Params)
        {
            Out << "        Func->Params.push_back(CreateOffsetProperty<" << Param.StorageType << ">(\""
                << Param.Name << "\", EPropertyType::" << Param.PropertyKind << ", offsetof("
                << ParamStructName << ", " << Param.Name << ")));\n";
        }
        if (Function.ReturnStorageType != "void")
        {
            Out << "        Func->ReturnProperty = CreateOffsetProperty<" << Function.ReturnStorageType
                << ">(\"ReturnValue\", EPropertyType::" << Function.ReturnPropertyKind << ", offsetof("
                << ParamStructName << ", ReturnValue));\n";
        }
        Out << "        Func->Transport = \"" << EscapeCppStringLiteral(Function.Transport) << "\";\n";
        Out << "        Func->MessageName = \"" << EscapeCppStringLiteral(Function.MessageName) << "\";\n";
        Out << "        Func->RouteName = \"" << EscapeCppStringLiteral(Function.Route) << "\";\n";
        Out << "        Func->TargetName = \"" << EscapeCppStringLiteral(Function.Target) << "\";\n";
        Out << "        Func->AuthMode = \"" << EscapeCppStringLiteral(Function.Auth) << "\";\n";
        Out << "        Func->WrapMode = \"" << EscapeCppStringLiteral(Function.Wrap) << "\";\n";
        Out << "        Func->ClientParamBinder = &" << BinderFunctionName << ";\n";
        Out << "        InClass->RegisterFunction(Func);\n";
        Out << "    } while (0);";
        return Out.str();
    }

    Out << "    do {\n";
    if (Function.Transport == "ClientCall" || Function.Transport == "ServerCall")
    {
        Out << "        auto* Func = new MFunction();\n";
        Out << "        Func->Name = \"" << Function.Name << "\";\n";
        Out << "        Func->Flags = " << BuildFunctionFlagsExpr(Function) << ";\n";
    }
    else if (Function.bIsRpc)
    {
        const std::string RpcKind = Function.RpcKind.empty() ? "Server" : Function.RpcKind;
        Out << "        auto* Func = ";
        if (Function.bHasValidate)
        {
            Out << "CreateRpcFunction<&ThisClass::" << Function.Name << ", &ThisClass::" << Function.Name
                << "_Validate>(\"" << Function.Name << "\", "
                << BuildFunctionFlagsExpr(Function) << ", ERpcType::" << RpcKind << ", " << ReliableValue;
        }
        else
        {
            Out << "CreateRpcFunction<&ThisClass::" << Function.Name << ">(\"" << Function.Name << "\", "
                << BuildFunctionFlagsExpr(Function) << ", ERpcType::" << RpcKind << ", " << ReliableValue;
        }

        if (!Function.Endpoint.empty())
        {
            Out << ", EServerType::" << Function.Endpoint;
        }
        Out << ");\n";
    }
    else
    {
        Out << "        auto* Func = CreateNativeFunction<&ThisClass::" << Function.Name << ">(\"" << Function.Name << "\", "
            << BuildFunctionFlagsExpr(Function) << ");\n";
    }
    Out << "        Func->ParamSize = sizeof(" << ParamStructName << ");\n";
    for (const auto& Param : Function.Params)
    {
        Out << "        Func->Params.push_back(CreateOffsetProperty<" << Param.StorageType << ">(\""
            << Param.Name << "\", EPropertyType::" << Param.PropertyKind << ", offsetof("
            << ParamStructName << ", " << Param.Name << ")));\n";
    }
    if (Function.ReturnStorageType != "void" && Function.Transport != "ServerCall")
    {
        Out << "        Func->ReturnProperty = CreateOffsetProperty<" << Function.ReturnStorageType
            << ">(\"ReturnValue\", EPropertyType::" << Function.ReturnPropertyKind << ", offsetof("
            << ParamStructName << ", ReturnValue));\n";
    }
    if (Function.Transport == "Client" || Function.Transport == "ClientCall" || Function.Transport == "ServerCall" || !Function.MessageName.empty())
    {
        Out << "        Func->Transport = \"" << EscapeCppStringLiteral(Function.Transport) << "\";\n";
        Out << "        Func->MessageName = \"" << EscapeCppStringLiteral(Function.MessageName) << "\";\n";
        Out << "        Func->RouteName = \"" << EscapeCppStringLiteral(Function.Route) << "\";\n";
        Out << "        Func->TargetName = \"" << EscapeCppStringLiteral(Function.Target) << "\";\n";
        Out << "        Func->AuthMode = \"" << EscapeCppStringLiteral(Function.Auth) << "\";\n";
        Out << "        Func->WrapMode = \"" << EscapeCppStringLiteral(Function.Wrap) << "\";\n";
        if (Function.Transport == "ClientCall")
        {
            Out << "        Func->ClientApiName = \"" << EscapeCppStringLiteral(ClientApiName) << "\";\n";
        }
        if (Function.Transport == "ClientCall")
        {
            Out << "        Func->ClientCallHandler = &" << ClientCallHandlerFunctionName << ";\n";
        }
        else if (Function.Transport == "ServerCall" && ParsedClass.ReflectionType != "Rpc")
        {
            Out << "        Func->ServerCallHandler = &" << ServerCallHandlerFunctionName << ";\n";
        }
        else if (Function.Transport != "ServerCall")
        {
            Out << "        Func->ClientParamBinder = &" << BinderFunctionName << ";\n";
        }
    }
    Out << "        InClass->RegisterFunction(Func);\n";
    Out << "    } while (0);";
    return Out.str();
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
        const bool bHasGeneratedBody = (ClassBody.find("MGENERATED_BODY(") != std::string::npos);

        SParsedClass Parsed;
        Parsed.Kind = (Region.Keyword == "struct") ? EParsedTypeKind::Struct : EParsedTypeKind::Class;
        Parsed.Name = Region.Name;
        Parsed.HeaderPath = Header;
        ParseTypeMarkerMetadata(Contents, Region, Parsed);
        const bool bHasTypeMarker = HasNearbyTypeMarker(Contents, Region, Parsed.Kind);

        if (Parsed.Kind == EParsedTypeKind::Class && !bHasGeneratedBody)
        {
            continue;
        }

        if (Parsed.Kind == EParsedTypeKind::Struct && !bHasTypeMarker)
        {
            continue;
        }
        if (Parsed.Owner.empty())
        {
            Parsed.Owner = DetermineOwnerFromHeaderPath(Header);
        }
        if (bHasGeneratedBody)
        {
            ParseGeneratedBodyMetadata(ClassBody, Parsed);
        }
        Parsed.TypeAliases = ParseTypeAliasesInBody(ClassBody);
        Parsed.Properties = ParsePropertiesInTypeBody(ClassBody);
        Parsed.Functions = ParseFunctionsInClassBody(ClassBody, RpcListMacros);
        for (SParsedProperty& Property : Parsed.Properties)
        {
            const std::string ResolvedType = ResolveAliasedType(Parsed.TypeAliases, Property.Type);
            Property.PropertyKind = InferPropertyKind(ResolvedType);
        }
        for (SParsedFunction& Function : Parsed.Functions)
        {
            Function.ReturnStorageType = ResolveAliasedType(Parsed.TypeAliases, Function.ReturnStorageType);
            Function.ReturnPropertyKind = (Function.ReturnStorageType == "void")
                ? "None"
                : InferPropertyKind(Function.ReturnStorageType);
            for (auto& Param : Function.Params)
            {
                Param.StorageType = ResolveAliasedType(Parsed.TypeAliases, Param.StorageType);
                Param.PropertyKind = InferPropertyKind(Param.StorageType);
            }
        }
        for (SParsedProperty& Property : Parsed.Properties)
        {
            if (Property.Owner.empty())
            {
                Property.Owner = Parsed.Owner;
            }
        }
        for (SParsedFunction& Function : Parsed.Functions)
        {
            if (Function.Owner.empty())
            {
                Function.Owner = Parsed.Owner;
            }
        }
        Classes.push_back(std::move(Parsed));
    }

    for (const SParsedClass& Parsed : Classes)
    {
        ExtractRpcClassTarget(Parsed);
        for (const SParsedFunction& Function : Parsed.Functions)
        {
            if (Function.Transport == "ClientCall")
            {
                ValidateClientCallFunction(Classes, Parsed, Function);
            }
            if (Function.Transport == "ServerCall")
            {
                ValidateServerCallFunction(Classes, Parsed, Function);
            }
        }
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
            Contents.find("MSTRUCT(") == std::string::npos &&
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

    std::set<std::string> EnumNames;
    for (const SParsedClass& ParsedClass : Classes)
    {
        if (ParsedClass.Kind == EParsedTypeKind::Enum)
        {
            EnumNames.insert(ParsedClass.Name);
        }
    }

    for (SParsedClass& ParsedClass : Classes)
    {
        for (SParsedProperty& Property : ParsedClass.Properties)
        {
            if (Property.PropertyKind == "Struct" && EnumNames.count(Trim(Property.Type)) > 0)
            {
                Property.PropertyKind = "Enum";
            }
        }
    }

    return Classes;
}

std::string BuildPropertyRegistrationLine(const SParsedProperty& Property)
{
    std::string Line;
    Line += "    do { auto* Prop = new TMemberProperty<ThisClass, " + Property.Type + ", &ThisClass::" + Property.Name + ">";
    Line += "(\"" + Property.Name + "\", EPropertyType::" + Property.PropertyKind + ", " + Property.FlagsExpr + ");";
    for (const SParsedProperty::SMetadataEntry& Entry : Property.Metadata)
    {
        Line += " Prop->SetMetadata(\"" + EscapeCppStringLiteral(Entry.Key) + "\", \"" +
                EscapeCppStringLiteral(Entry.Value) + "\");";
    }
    Line += " InClass->RegisterProperty(Prop); } while(0);";
    return Line;
}

std::string BuildPropertyAccessorFunctionName(const SParsedClass& ParsedClass, const SParsedProperty& Property)
{
    return "Prop_" + SanitizeIdentifier(ParsedClass.Name) + "_" + SanitizeIdentifier(Property.Name);
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

std::string DetermineOwnerFromHeaderPath(const fs::path& HeaderPath)
{
    fs::path RelativePath = HeaderPath;
    if (RelativePath.is_absolute())
    {
        std::error_code Error;
        RelativePath = fs::relative(RelativePath, fs::current_path(), Error);
        if (Error)
        {
            RelativePath = HeaderPath;
        }
    }

    std::vector<std::string> Parts;
    for (const fs::path& Part : RelativePath)
    {
        Parts.push_back(Part.generic_string());
    }

    for (size_t Index = 0; Index < Parts.size(); ++Index)
    {
        if (Parts[Index] != "Source" || Index + 1 >= Parts.size())
        {
            continue;
        }

        if (Parts[Index + 1] == "Servers" && Index + 2 < Parts.size())
        {
            return Parts[Index + 2];
        }

        return "Shared";
    }

    return "Shared";
}

std::string NormalizeGeneratedGroupName(std::string Owner)
{
    Owner = Trim(Owner);
    if (Owner.empty())
    {
        return "shared";
    }

    std::string Result;
    Result.reserve(Owner.size());
    for (char Ch : Owner)
    {
        if (std::isalnum(static_cast<unsigned char>(Ch)))
        {
            Result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Ch))));
        }
        else if (Ch == '_' || Ch == '-')
        {
            Result.push_back('_');
        }
    }

    return Result.empty() ? "shared" : Result;
}

std::string DetermineGeneratedGroup(const SParsedClass& ParsedClass)
{
    return NormalizeGeneratedGroupName(ParsedClass.Owner);
}

std::string EscapeCMakeListValue(std::string Value)
{
    Value = ReplaceAll(std::move(Value), "\\", "\\\\");
    Value = ReplaceAll(std::move(Value), "\"", "\\\"");
    Value = ReplaceAll(std::move(Value), ";", "\\;");
    return Value;
}

void WriteGeneratedHeader(std::ofstream& Out, const SParsedClass& ParsedClass)
{
    Out << "#pragma once\n";
    Out << "// Generated by MHeaderTool\n";
    Out << "// Source: " << ParsedClass.HeaderPath.string() << "\n";
    Out << "// Reflected " << GetTypeKindName(ParsedClass.Kind) << ": " << ParsedClass.Name << "\n";
    Out << "\n";
    Out << "#include \"Common/Net/Rpc/RpcClientCall.h\"\n";
    Out << "#include \"Common/Net/Rpc/RpcServerCall.h\"\n";
    Out << "#include \"Servers/App/ServerCallRequestValidation.h\"\n";
    Out << "\n";
    if (ParsedClass.Kind == EParsedTypeKind::Enum)
    {
        Out << "inline MEnum* MHeaderTool_Generated_RegisterEnum_" << ParsedClass.Name << "()\n";
        Out << "{\n";
        Out << "    static MEnum* Enum = nullptr;\n";
        Out << "    if (!Enum)\n";
        Out << "    {\n";
        Out << "        Enum = new MEnum(\"" << ParsedClass.Name << "\", \"" << ParsedClass.HeaderPath.generic_string() << "\", std::type_index(typeid(" << ParsedClass.Name << ")));\n";
        for (const std::string& Value : ParsedClass.EnumValues)
        {
            const std::string QualifiedValue = ParsedClass.bScopedEnum
                ? (ParsedClass.Name + "::" + Value)
                : Value;
            Out << "        Enum->AddValue(\"" << Value << "\", static_cast<int64>(" << QualifiedValue << "));\n";
        }
        Out << "        MObject::RegisterEnum(Enum);\n";
        Out << "    }\n";
        Out << "    return Enum;\n";
        Out << "}\n";
        return;
    }

    if (ParsedClass.Kind == EParsedTypeKind::Struct)
    {
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
        Out << "inline MClass* MHeaderTool_Generated_RegisterStruct_" << ParsedClass.Name << "()\n";
        Out << "{\n";
        Out << "    static MClass* Struct = nullptr;\n";
        Out << "    if (!Struct)\n";
        Out << "    {\n";
        Out << "        Struct = new MClass();\n";
        Out << "        Struct->SetMeta(\"" << ParsedClass.Name << "\", \"" << ParsedClass.HeaderPath.generic_string() << "\", nullptr, " << ParsedClass.ClassFlagsExpr << ");\n";
        Out << "        Struct->SetKind(" << BuildClassKindExpr(ParsedClass) << ");\n";
        Out << "        Struct->SetCppTypeIndex(std::type_index(typeid(" << ParsedClass.Name << ")));\n";
        Out << "        using ThisClass = " << ParsedClass.Name << ";\n";
        Out << "        MClass* InClass = Struct;\n";
        Out << "        MHEADERTOOL_REGISTER_PROPERTIES_" << ParsedClass.Name << "();\n";
        Out << "        MObject::RegisterStruct(Struct);\n";
        Out << "    }\n";
        Out << "    return Struct;\n";
        Out << "}\n";
        return;
    }

    if (!ParsedClass.Properties.empty())
    {
        for (const SParsedProperty& Property : ParsedClass.Properties)
        {
            const std::string AccessorName = BuildPropertyAccessorFunctionName(ParsedClass, Property);
            Out << "inline const MProperty* " << AccessorName << "()\n";
            Out << "{\n";
            Out << "    static const MProperty* Prop = []() -> const MProperty*\n";
            Out << "    {\n";
            Out << "        MClass* ClassMeta = " << ParsedClass.Name << "::StaticClass();\n";
            Out << "        return ClassMeta ? ClassMeta->FindProperty(\"" << Property.Name << "\") : nullptr;\n";
            Out << "    }();\n";
            Out << "    return Prop;\n";
            Out << "}\n";
            Out << "\n";
        }
    }

    for (const SParsedFunction& Function : ParsedClass.Functions)
    {
        Out << "struct " << BuildFunctionParamStructName(ParsedClass, Function) << "\n";
        Out << "{\n";
        for (const auto& Param : Function.Params)
        {
            Out << "    " << Param.StorageType << " " << Param.Name << " {};\n";
        }
        if (Function.ReturnStorageType != "void" && Function.Transport != "ServerCall")
        {
            Out << "    " << Function.ReturnStorageType << " ReturnValue {};\n";
        }
        Out << "};\n";
        Out << "\n";

        if (Function.Transport == "Client" || !Function.MessageName.empty())
        {
            const std::string ParamStructName = BuildFunctionParamStructName(ParsedClass, Function);
            const std::string BinderFunctionName = BuildClientBinderFunctionName(ParsedClass, Function);
            size_t InjectedParamCount = 0;
            for (const auto& Param : Function.Params)
            {
                if (Param.StorageType == "uint64" &&
                    (Param.Name == "ClientConnectionId" || Param.Name == "ConnectionId"))
                {
                    ++InjectedParamCount;
                }
            }
            const size_t PayloadParamCount = Function.Params.size() - InjectedParamCount;

            Out << "inline bool " << BinderFunctionName << "(uint64 ConnectionId, const TByteArray& Payload, TByteArray& OutParamStorage)\n";
            Out << "{\n";
            Out << "    OutParamStorage.assign(sizeof(" << ParamStructName << "), 0);\n";
            Out << "    auto* Params = reinterpret_cast<" << ParamStructName << "*>(OutParamStorage.data());\n";
            for (const auto& Param : Function.Params)
            {
                if (Param.StorageType == "uint64" &&
                    (Param.Name == "ClientConnectionId" || Param.Name == "ConnectionId"))
                {
                    Out << "    Params->" << Param.Name << " = ConnectionId;\n";
                }
            }
            if (PayloadParamCount == 0)
            {
                Out << "    if (!Payload.empty())\n";
                Out << "    {\n";
                Out << "        LOG_WARN(\"Generated client binder expected empty payload: function=%s size=%llu\", \"" << Function.Name << "\", static_cast<unsigned long long>(Payload.size()));\n";
                Out << "        return false;\n";
                Out << "    }\n";
            }
            else if (PayloadParamCount == 1)
            {
                for (const auto& Param : Function.Params)
                {
                    if (Param.StorageType == "uint64" &&
                        (Param.Name == "ClientConnectionId" || Param.Name == "ConnectionId"))
                    {
                        continue;
                    }

                    Out << "    " << Param.StorageType << " PayloadValue {};\n";
                    Out << "    auto ParseResult = ParsePayload(Payload, PayloadValue, \"" << Function.Name << "\");\n";
                    Out << "    if (!ParseResult.IsOk())\n";
                    Out << "    {\n";
                    Out << "        LOG_WARN(\"ParsePayload failed: %s\", ParseResult.GetError().c_str());\n";
                    Out << "        return false;\n";
                    Out << "    }\n";
                    Out << "    Params->" << Param.Name << " = std::move(PayloadValue);\n";
                    break;
                }
            }
            else
            {
                Out << "    LOG_WARN(\"Generated client binder unsupported multi-param payload: function=%s count=%llu\", \"" << Function.Name << "\", static_cast<unsigned long long>(" << PayloadParamCount << "));\n";
                Out << "    return false;\n";
            }
            Out << "    return true;\n";
            Out << "}\n";
            Out << "\n";
        }

        if (Function.Transport == "ClientCall")
        {
            const std::string ClientCallHandlerFunctionName = BuildClientCallHandlerFunctionName(ParsedClass, Function);
            const auto& RequestParam = Function.Params.at(0);
            const auto& ResponseParam = Function.Params.at(1);
            Out << "inline EGeneratedClientCallHandlerResult " << ClientCallHandlerFunctionName
                << "(MObject* Object, uint64 /*ConnectionId*/, const TByteArray& Payload, TByteArray& OutResponsePayload)\n";
            Out << "{\n";
            Out << "    auto* TypedObject = static_cast<" << ParsedClass.Name << "*>(Object);\n";
            Out << "    if (!TypedObject)\n";
            Out << "    {\n";
            Out << "        return EGeneratedClientCallHandlerResult::Failed;\n";
            Out << "    }\n";
            Out << "    " << RequestParam.StorageType << " RequestValue {};\n";
            Out << "    auto ParseResult = ParsePayload(Payload, RequestValue, \"" << Function.Name << "\");\n";
            Out << "    if (!ParseResult.IsOk())\n";
            Out << "    {\n";
            Out << "        LOG_WARN(\"ParsePayload failed: %s\", ParseResult.GetError().c_str());\n";
            Out << "        return EGeneratedClientCallHandlerResult::ParamBindingFailed;\n";
            Out << "    }\n";
            Out << "    " << ResponseParam.StorageType << " ResponseValue {};\n";
            Out << "    TypedObject->" << Function.Name << "(RequestValue, ResponseValue);\n";
            Out << "    if (IsCurrentClientCallDeferred())\n";
            Out << "    {\n";
            Out << "        return EGeneratedClientCallHandlerResult::Deferred;\n";
            Out << "    }\n";
            Out << "    OutResponsePayload = BuildPayload(ResponseValue);\n";
            Out << "    return EGeneratedClientCallHandlerResult::Responded;\n";
            Out << "}\n";
            Out << "\n";
        }

        if (Function.Transport == "ServerCall")
        {
            const std::string ServerCallHandlerFunctionName = BuildServerCallHandlerFunctionName(ParsedClass, Function);
            const auto& RequestParam = Function.Params.at(0);
            const std::optional<std::string> ResponseType = ExtractServerCallResponseType(Function);
            if (!ResponseType.has_value())
            {
                throw std::runtime_error("Codegen failed to extract ServerCall response type: " + ParsedClass.Name + "::" + Function.Name);
            }

            Out << "inline bool " << ServerCallHandlerFunctionName
                << "(MObject* Object, const TByteArray& Payload)\n";
            Out << "{\n";
            Out << "    auto* TypedObject = static_cast<" << ParsedClass.Name << "*>(Object);\n";
            Out << "    if (!TypedObject)\n";
            Out << "    {\n";
            Out << "        return false;\n";
            Out << "    }\n";
            Out << "    " << RequestParam.StorageType << " RequestValue {};\n";
            Out << "    auto ParseResult = ParsePayload(Payload, RequestValue, \"" << Function.Name << "\");\n";
            Out << "    if (!ParseResult.IsOk())\n";
            Out << "    {\n";
            Out << "        LOG_WARN(\"ParsePayload failed: %s\", ParseResult.GetError().c_str());\n";
            Out << "        return false;\n";
            Out << "    }\n";
            Out << "    const SServerCallContext Context = CaptureCurrentServerCallContext();\n";
            Out << "    if (!Context.IsValid())\n";
            Out << "    {\n";
            Out << "        return false;\n";
            Out << "    }\n";
            Out << "    if (auto ValidationError = MServerCallRequestValidation::ValidateRequest(RequestValue); ValidationError.has_value())\n";
            Out << "    {\n";
            Out << "        return MServerCallAsyncSupport::StartDeferredServerCall(Context, "
                << "MServerCallAsyncSupport::MakeErrorFuture<" << *ResponseType << ">("
                << "ValidationError->Code.c_str(), ValidationError->Message.c_str()), \"" << Function.Name << "\");\n";
            Out << "    }\n";
            Out << "    return MServerCallAsyncSupport::StartDeferredServerCall(Context, TypedObject->" << Function.Name
                << "(RequestValue), \"" << Function.Name << "\");\n";
            Out << "}\n";
            Out << "\n";
        }

        if (Function.bIsRpc)
        {
            Out << "namespace MRpc\n";
            Out << "{\n";
            Out << "namespace " << SanitizeIdentifier(ParsedClass.Name) << "\n";
            Out << "{\n";
            Out << "inline bool " << Function.Name << "(TByteArray& OutData";
            for (const auto& Param : Function.Params)
            {
                Out << ", " << Param.Type << " " << Param.Name;
            }
            Out << ")\n";
            Out << "{\n";
            Out << "    return BuildRpcPayloadForRemoteCall(\"" << ParsedClass.Name << "\", \"" << Function.Name << "\", OutData";
            for (const auto& Param : Function.Params)
            {
                Out << ", " << Param.Name;
            }
            Out << ");\n";
            Out << "}\n";
            Out << "template<typename TConnection>\n";
            Out << "inline bool " << Function.Name << "(TConnection&& Connection";
            for (const auto& Param : Function.Params)
            {
                Out << ", " << Param.Type << " " << Param.Name;
            }
            Out << ")\n";
            Out << "{\n";
            Out << "    TByteArray RpcPayload;\n";
            Out << "    if (!" << Function.Name << "(RpcPayload";
            for (const auto& Param : Function.Params)
            {
                Out << ", " << Param.Name;
            }
            Out << "))\n";
            Out << "    {\n";
            Out << "        return false;\n";
            Out << "    }\n";
            Out << "    return SendServerRpcMessage(std::forward<TConnection>(Connection), RpcPayload);\n";
            Out << "}\n";
            Out << "} // namespace " << SanitizeIdentifier(ParsedClass.Name) << "\n";
            Out << "} // namespace MRpc\n";
            Out << "\n";
        }
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
            Out << ReplaceAll(BuildFunctionRegistrationBlock(ParsedClass, Function), "\n", " \\\n") << " \\\n";
        }
        Out << "    } while (0)\n";
    }
}

void WriteGeneratedSource(std::ofstream& Out, const SParsedClass& ParsedClass)
{
    const std::string IncludeName = MakeIncludePathFromHeader(ParsedClass.HeaderPath);
    const std::string GeneratedHeaderInclude = SanitizeIdentifier(ParsedClass.Name) + ".mgenerated.h";
    Out << "// Generated by MHeaderTool\n";
    Out << "// Source: " << ParsedClass.HeaderPath.string() << "\n";
    Out << "#include \"" << IncludeName << "\"\n";
    Out << "#include \"" << GeneratedHeaderInclude << "\"\n";
    Out << "\n";
    Out << "\n";

    if (ParsedClass.Kind == EParsedTypeKind::Enum)
    {
        Out << "namespace\n";
        Out << "{\n";
        Out << "struct SAutoRegisterEnum_" << ParsedClass.Name << "\n";
        Out << "{\n";
        Out << "    SAutoRegisterEnum_" << ParsedClass.Name << "()\n";
        Out << "    {\n";
        Out << "        MHeaderTool_Generated_RegisterEnum_" << ParsedClass.Name << "();\n";
        Out << "    }\n";
        Out << "};\n";
        Out << "\n";
        Out << "SAutoRegisterEnum_" << ParsedClass.Name << " GAutoRegisterEnum_" << ParsedClass.Name << ";\n";
        Out << "} // namespace\n";
        return;
    }

    if (ParsedClass.Kind == EParsedTypeKind::Struct)
    {
        Out << "namespace\n";
        Out << "{\n";
        Out << "struct SAutoRegisterStruct_" << ParsedClass.Name << "\n";
        Out << "{\n";
        Out << "    SAutoRegisterStruct_" << ParsedClass.Name << "()\n";
        Out << "    {\n";
        Out << "        MHeaderTool_Generated_RegisterStruct_" << ParsedClass.Name << "();\n";
        Out << "    }\n";
        Out << "};\n\n";
        Out << "SAutoRegisterStruct_" << ParsedClass.Name << " GAutoRegisterStruct_" << ParsedClass.Name << ";\n";
        Out << "} // namespace\n";
        return;
    }

    if (ParsedClass.ReflectionType == "Rpc")
    {
        for (const SParsedFunction& Function : ParsedClass.Functions)
        {
            if (Function.Transport != "ServerCall" || Function.Params.size() != 1)
            {
                continue;
            }

            const std::optional<std::string> ResponseType = ExtractServerCallResponseType(Function);
            if (!ResponseType.has_value())
            {
                throw std::runtime_error(
                    "Codegen failed to extract Rpc response type: " + ParsedClass.Name + "::" + Function.Name);
            }

            const auto& RequestParam = Function.Params.front();
            Out << Function.ReturnType << " " << ParsedClass.Name << "::" << Function.Name
                << "(" << RequestParam.Type << " " << RequestParam.Name << ")\n";
            Out << "{\n";
            Out << "    return CallRemoteByName<" << *ResponseType << ">(\"" << Function.Name << "\", " << RequestParam.Name << ");\n";
            Out << "}\n";
            Out << "\n";
        }

        const std::optional<std::string> RpcTarget = ExtractRpcClassTarget(ParsedClass);
        Out << "EServerType " << ParsedClass.Name << "::GetTargetServerType() const\n";
        Out << "{\n";
        if (RpcTarget.has_value())
        {
            Out << "    return EServerType::" << *RpcTarget << ";\n";
        }
        else
        {
            Out << "    return EServerType::Unknown;\n";
        }
        Out << "}\n";
        Out << "\n";
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
    Out << "        Class->SetKind(" << BuildClassKindExpr(ParsedClass) << ");\n";
    Out << "        Class->SetConstructor<" << ParsedClass.Name << ">();\n";
    Out << "        " << ParsedClass.Name << "::RegisterAllProperties(Class);\n";
    Out << "        " << ParsedClass.Name << "::RegisterAllFunctions(Class);\n";
    Out << "        MObject::RegisterClass(Class);\n";
    Out << "    }\n";
    Out << "    return Class;\n";
    Out << "}\n";
    Out << "\n";
    Out << "namespace\n";
    Out << "{\n";
    Out << "struct SAutoRegisterClass_" << ParsedClass.Name << "\n";
    Out << "{\n";
    Out << "    SAutoRegisterClass_" << ParsedClass.Name << "()\n";
    Out << "    {\n";
    Out << "        " << ParsedClass.Name << "::StaticClass();\n";
    Out << "    }\n";
    Out << "};\n\n";
    Out << "SAutoRegisterClass_" << ParsedClass.Name << " GAutoRegisterClass_" << ParsedClass.Name << ";\n";
    Out << "} // namespace\n";
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
        const bool bIsTypeGenerated = (Filename.find(".mgenerated.") != std::string::npos);
        const bool bIsLegacyManifestGenerated =
            (Filename == "MRpcManifest.generated.h" ||
             Filename == "MClientManifest.generated.h" ||
             Filename == "MReflectionManifest.generated.h");
        if (!bIsTypeGenerated && !bIsLegacyManifestGenerated)
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

bool WriteGeneratedRpcManifest(const fs::path& OutputDir, const std::vector<SParsedClass>& Classes)
{
    struct SEndpointEntry
    {
        std::string ClassName;
        std::string FunctionName;
        std::string Endpoint;
    };

    std::map<std::string, std::vector<SEndpointEntry>> EndpointGroups;
    std::set<std::string> IncludeClasses;
    for (const SParsedClass& ParsedClass : Classes)
    {
        for (const SParsedFunction& Function : ParsedClass.Functions)
        {
            if (Function.Endpoint.empty())
            {
                continue;
            }

            EndpointGroups[Function.Name].push_back(SEndpointEntry{ParsedClass.Name, Function.Name, Function.Endpoint});
            IncludeClasses.insert(ParsedClass.Name);
        }
    }

    const fs::path ManifestPath = OutputDir / "MRpcManifest.generated.h";
    std::ofstream Out(ManifestPath);
    if (!Out)
    {
        std::cerr << "Failed to write generated RPC manifest: " << ManifestPath << "\n";
        return false;
    }

    Out << "#pragma once\n";
    Out << "// Generated by MHeaderTool\n\n";
    Out << "#include \"Common/Net/Rpc/RpcManifest.h\"\n";
    for (const std::string& ClassName : IncludeClasses)
    {
        Out << "#include \"" << SanitizeIdentifier(ClassName) << ".mgenerated.h\"\n";
    }
    Out << "\n";
    Out << "namespace MRpcManifest\n";
    Out << "{\n";
    Out << "enum class EFunction : uint16\n";
    Out << "{\n";
    for (const auto& [FunctionName, Entries] : EndpointGroups)
    {
        if (Entries.empty())
        {
            continue;
        }
        Out << "    " << SanitizeIdentifier(FunctionName) << ",\n";
    }
    Out << "};\n\n";
    Out << "struct SEntry\n";
    Out << "{\n";
    Out << "    EFunction Function;\n";
    Out << "    EServerType ServerType;\n";
    Out << "    const char* ClassName;\n";
    Out << "    const char* FunctionName;\n";
    Out << "};\n\n";
    Out << "inline const SEntry* GetEntries()\n";
    Out << "{\n";
    Out << "    static const SEntry Entries[] = {\n";
    for (const auto& [FunctionName, Entries] : EndpointGroups)
    {
        for (const SEndpointEntry& Entry : Entries)
        {
            Out << "        {EFunction::" << SanitizeIdentifier(FunctionName)
                << ", EServerType::" << Entry.Endpoint
                << ", \"" << Entry.ClassName
                << "\", \"" << Entry.FunctionName << "\"},\n";
        }
    }
    Out << "    };\n";
    Out << "    return Entries;\n";
    Out << "}\n\n";
    Out << "inline size_t GetEntryCount()\n";
    Out << "{\n";
    size_t EntryCount = 0;
    for (const auto& [FunctionName, Entries] : EndpointGroups)
    {
        (void)FunctionName;
        EntryCount += Entries.size();
    }
    Out << "    return " << EntryCount << ";\n";
    Out << "}\n\n";
    Out << "inline const SEntry* FindEntry(EFunction Function, EServerType ServerType)\n";
    Out << "{\n";
    Out << "    const SEntry* Entries = GetEntries();\n";
    Out << "    for (size_t Index = 0; Index < GetEntryCount(); ++Index)\n";
    Out << "    {\n";
    Out << "        const SEntry& Entry = Entries[Index];\n";
    Out << "        if (Entry.Function == Function && Entry.ServerType == ServerType)\n";
    Out << "        {\n";
    Out << "            return &Entry;\n";
    Out << "        }\n";
    Out << "    }\n";
    Out << "    return nullptr;\n";
    Out << "}\n\n";
    Out << "inline bool Supports(EFunction Function, EServerType ServerType)\n";
    Out << "{\n";
    Out << "    return FindEntry(Function, ServerType) != nullptr;\n";
    Out << "}\n\n";
    Out << "inline size_t GetSupportedFunctionCount(EServerType ServerType)\n";
    Out << "{\n";
    Out << "    size_t Count = 0;\n";
    Out << "    const SEntry* Entries = GetEntries();\n";
    Out << "    for (size_t Index = 0; Index < GetEntryCount(); ++Index)\n";
    Out << "    {\n";
    Out << "        if (Entries[Index].ServerType == ServerType)\n";
    Out << "        {\n";
    Out << "            ++Count;\n";
    Out << "        }\n";
    Out << "    }\n";
    Out << "    return Count;\n";
    Out << "}\n\n";
    Out << "template<typename TFunc>\n";
    Out << "inline void ForEachSupportedFunction(EServerType ServerType, TFunc&& Func)\n";
    Out << "{\n";
    Out << "    const SEntry* Entries = GetEntries();\n";
    Out << "    for (size_t Index = 0; Index < GetEntryCount(); ++Index)\n";
    Out << "    {\n";
    Out << "        if (Entries[Index].ServerType == ServerType)\n";
    Out << "        {\n";
    Out << "            Func(Entries[Index]);\n";
    Out << "        }\n";
    Out << "    }\n";
    Out << "}\n\n";
    Out << "inline const char* GetFunctionName(EFunction Function)\n";
    Out << "{\n";
    Out << "    switch (Function)\n";
    Out << "    {\n";
    for (const auto& [FunctionName, Entries] : EndpointGroups)
    {
        if (Entries.empty())
        {
            continue;
        }
        Out << "    case EFunction::" << SanitizeIdentifier(FunctionName) << ":\n";
        Out << "        return \"" << FunctionName << "\";\n";
    }
    Out << "    default:\n";
    Out << "        return \"Unknown\";\n";
    Out << "    }\n";
    Out << "}\n\n";
    for (const auto& [FunctionName, Entries] : EndpointGroups)
    {
        if (Entries.empty())
        {
            continue;
        }

        const std::string HelperName = SanitizeIdentifier(FunctionName);
        Out << "template<EFunction Function>\n";
        Out << "struct TBuilder;\n\n";
        Out << "template<>\n";
        Out << "struct TBuilder<EFunction::" << HelperName << ">\n";
        Out << "{\n";
        Out << "    template<typename... TArgs>\n";
        Out << "    static bool Build(EServerType ServerType, TByteArray& OutData, TArgs&&... Args)\n";
        Out << "{\n";
        Out << "    switch (ServerType)\n";
        Out << "    {\n";
        for (const SEndpointEntry& Entry : Entries)
        {
        Out << "    case EServerType::" << Entry.Endpoint << ":\n";
        Out << "        return MRpc::" << SanitizeIdentifier(Entry.ClassName) << "::" << Entry.FunctionName
            << "(OutData, std::forward<TArgs>(Args)...);\n";
        }
        Out << "    default:\n";
        Out << "        return false;\n";
        Out << "    }\n";
        Out << "    }\n";
        Out << "};\n\n";
    }
    Out << "template<EFunction Function, typename... TArgs>\n";
    Out << "inline bool Build(EServerType ServerType, TByteArray& OutData, TArgs&&... Args)\n";
    Out << "{\n";
    Out << "    return TBuilder<Function>::Build(ServerType, OutData, std::forward<TArgs>(Args)...);\n";
    Out << "}\n";
    Out << "} // namespace MRpcManifest\n";
    Out << "\n";
    Out << "namespace MRpc\n";
    Out << "{\n";
    Out << "template<MRpcManifest::EFunction Function, typename TConnection, typename... TArgs>\n";
    Out << "inline bool Call(TConnection&& Connection, EServerType ServerType, TArgs&&... Args)\n";
    Out << "{\n";
    Out << "    TByteArray RpcPayload;\n";
    Out << "    if (!MRpcManifest::Build<Function>(ServerType, RpcPayload, std::forward<TArgs>(Args)...))\n";
    Out << "    {\n";
    Out << "        ReportUnsupportedRpcEndpoint(ServerType, MRpcManifest::GetFunctionName(Function));\n";
    Out << "        return false;\n";
    Out << "    }\n";
    Out << "    return SendServerRpcMessage(std::forward<TConnection>(Connection), RpcPayload);\n";
    Out << "}\n\n";
    Out << "template<MRpcManifest::EFunction Function, typename TConnection, typename... TArgs>\n";
    Out << "inline bool TryCall(TConnection&& Connection, EServerType ServerType, TArgs&&... Args)\n";
    Out << "{\n";
    Out << "    return Call<Function>(std::forward<TConnection>(Connection), ServerType, std::forward<TArgs>(Args)...);\n";
    Out << "}\n";
    Out << "} // namespace MRpc\n";
    return true;
}

bool WriteGeneratedReflectionManifest(const fs::path& OutputDir, const std::vector<SParsedClass>& Classes)
{
    const fs::path ManifestPath = OutputDir / "MReflectionManifest.generated.h";
    std::ofstream Out(ManifestPath);
    if (!Out)
    {
        std::cerr << "Failed to write generated reflection manifest: " << ManifestPath << "\n";
        return false;
    }

    Out << "#pragma once\n";
    Out << "// Generated by MHeaderTool\n\n";
    Out << "#include \"Common/Runtime/MLib.h\"\n\n";
    Out << "namespace MReflectionManifest\n";
    Out << "{\n";
    Out << "struct STypeEntry\n";
    Out << "{\n";
    Out << "    const char* Kind;\n";
    Out << "    const char* Name;\n";
    Out << "    const char* Owner;\n";
    Out << "    const char* HeaderPath;\n";
    Out << "};\n\n";
    Out << "struct SPropertyEntry\n";
    Out << "{\n";
    Out << "    const char* OwnerType;\n";
    Out << "    const char* PropertyName;\n";
    Out << "    const char* Owner;\n";
    Out << "    const char* CppType;\n";
    Out << "    const char* MacroArgs;\n";
    Out << "    const char* HeaderPath;\n";
    Out << "};\n\n";
    Out << "struct SFunctionEntry\n";
    Out << "{\n";
    Out << "    const char* OwnerType;\n";
    Out << "    const char* FunctionName;\n";
    Out << "    const char* Owner;\n";
    Out << "    const char* Signature;\n";
    Out << "    const char* MacroArgs;\n";
    Out << "    const char* HeaderPath;\n";
    Out << "    const char* Transport;\n";
    Out << "    const char* MessageName;\n";
    Out << "    bool bIsRpc;\n";
    Out << "};\n\n";
    Out << "struct SEnumValueEntry\n";
    Out << "{\n";
    Out << "    const char* EnumName;\n";
    Out << "    const char* ValueName;\n";
    Out << "    const char* Owner;\n";
    Out << "    const char* HeaderPath;\n";
    Out << "};\n\n";

    Out << "inline const STypeEntry* GetTypes()\n";
    Out << "{\n";
    Out << "    static const STypeEntry Entries[] = {\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        Out << "        {\"" << EscapeCppStringLiteral(std::string(GetTypeKindName(ParsedClass.Kind)))
            << "\", \"" << EscapeCppStringLiteral(ParsedClass.Name)
            << "\", \"" << EscapeCppStringLiteral(ParsedClass.Owner)
            << "\", \"" << EscapeCppStringLiteral(ParsedClass.HeaderPath.generic_string()) << "\"},\n";
    }
    Out << "    };\n";
    Out << "    return Entries;\n";
    Out << "}\n\n";
    Out << "inline size_t GetTypeCount()\n";
    Out << "{\n";
    Out << "    return " << Classes.size() << ";\n";
    Out << "}\n\n";

    size_t PropertyCount = 0;
    Out << "inline const SPropertyEntry* GetProperties()\n";
    Out << "{\n";
    Out << "    static const SPropertyEntry Entries[] = {\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        for (const SParsedProperty& Property : ParsedClass.Properties)
        {
            ++PropertyCount;
            Out << "        {\"" << EscapeCppStringLiteral(ParsedClass.Name)
                << "\", \"" << EscapeCppStringLiteral(Property.Name)
                << "\", \"" << EscapeCppStringLiteral(Property.Owner)
                << "\", \"" << EscapeCppStringLiteral(Property.Type)
                << "\", \"" << EscapeCppStringLiteral(Property.MacroArgs)
                << "\", \"" << EscapeCppStringLiteral(ParsedClass.HeaderPath.generic_string()) << "\"},\n";
        }
    }
    Out << "    };\n";
    Out << "    return Entries;\n";
    Out << "}\n\n";
    Out << "inline size_t GetPropertyCount()\n";
    Out << "{\n";
    Out << "    return " << PropertyCount << ";\n";
    Out << "}\n\n";

    size_t FunctionCount = 0;
    Out << "inline const SFunctionEntry* GetFunctions()\n";
    Out << "{\n";
    Out << "    static const SFunctionEntry Entries[] = {\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        for (const SParsedFunction& Function : ParsedClass.Functions)
        {
            ++FunctionCount;
            Out << "        {\"" << EscapeCppStringLiteral(ParsedClass.Name)
                << "\", \"" << EscapeCppStringLiteral(Function.Name)
                << "\", \"" << EscapeCppStringLiteral(Function.Owner)
                << "\", \"" << EscapeCppStringLiteral(Function.Signature)
                << "\", \"" << EscapeCppStringLiteral(Function.MacroArgs)
                << "\", \"" << EscapeCppStringLiteral(ParsedClass.HeaderPath.generic_string())
                << "\", \"" << EscapeCppStringLiteral(Function.Transport)
                << "\", \"" << EscapeCppStringLiteral(Function.MessageName)
                << "\", " << (Function.bIsRpc ? "true" : "false") << "},\n";
        }
    }
    Out << "    };\n";
    Out << "    return Entries;\n";
    Out << "}\n\n";
    Out << "inline size_t GetFunctionCount()\n";
    Out << "{\n";
    Out << "    return " << FunctionCount << ";\n";
    Out << "}\n\n";

    size_t EnumValueCount = 0;
    Out << "inline const SEnumValueEntry* GetEnumValues()\n";
    Out << "{\n";
    Out << "    static const SEnumValueEntry Entries[] = {\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        if (ParsedClass.Kind != EParsedTypeKind::Enum)
        {
            continue;
        }

        for (const std::string& Value : ParsedClass.EnumValues)
        {
            ++EnumValueCount;
            Out << "        {\"" << EscapeCppStringLiteral(ParsedClass.Name)
                << "\", \"" << EscapeCppStringLiteral(Value)
                << "\", \"" << EscapeCppStringLiteral(ParsedClass.Owner)
                << "\", \"" << EscapeCppStringLiteral(ParsedClass.HeaderPath.generic_string()) << "\"},\n";
        }
    }
    Out << "    };\n";
    Out << "    return Entries;\n";
    Out << "}\n\n";
    Out << "inline size_t GetEnumValueCount()\n";
    Out << "{\n";
    Out << "    return " << EnumValueCount << ";\n";
    Out << "}\n";
    Out << "} // namespace MReflectionManifest\n";
    return true;
}

bool WriteGeneratedClientManifest(const fs::path& OutputDir, const std::vector<SParsedClass>& Classes)
{
    struct SClientEntry
    {
        std::string OwnerType;
        std::string FunctionName;
        std::string ClientApiName;
        std::string ResponseTypeName;
        std::string FunctionIdExpr;
        std::string Owner;
        std::string HeaderPath;
        std::string Transport;
        std::string MessageName;
        std::string BinderFunctionExpr;
        std::string RouteName;
        std::string TargetName;
        std::string AuthMode;
        std::string WrapMode;
    };

    std::set<std::string> SourceIncludeHeaders;
    std::set<std::string> GeneratedIncludeHeaders;
    std::vector<SClientEntry> Entries;
    for (const SParsedClass& ParsedClass : Classes)
    {
        for (const SParsedFunction& Function : ParsedClass.Functions)
        {
            if (Function.Transport != "Client" && Function.Transport != "ClientCall" && Function.MessageName.empty())
            {
                continue;
            }

            Entries.push_back(SClientEntry{
                ParsedClass.Name,
                Function.Name,
                Function.ClientApi.empty() ? Function.Name : Function.ClientApi,
                (Function.Transport == "ClientCall" && Function.Params.size() >= 2)
                    ? Function.Params[1].StorageType
                    : (Function.ReturnStorageType != "void" ? Function.ReturnStorageType : ""),
                Function.Transport == "ClientCall"
                    ? ("MGET_STABLE_CLIENT_FUNCTION_ID(\"" +
                       EscapeCppStringLiteral(Function.ClientApi.empty() ? Function.Name : Function.ClientApi) + "\")")
                    : ("MGET_STABLE_RPC_FUNCTION_ID(\"" + EscapeCppStringLiteral(ParsedClass.Name) + "\", \"" +
                       EscapeCppStringLiteral(Function.Name) + "\")"),
                Function.Owner,
                ParsedClass.HeaderPath.generic_string(),
                Function.Transport,
                Function.MessageName,
                (Function.Transport == "Client" || !Function.MessageName.empty())
                    ? ("&" + BuildClientBinderFunctionName(ParsedClass, Function))
                    : "nullptr",
                Function.Route,
                Function.Target,
                Function.Auth,
                Function.Wrap});
            SourceIncludeHeaders.insert(MakeIncludePathFromHeader(ParsedClass.HeaderPath));
            GeneratedIncludeHeaders.insert(SanitizeIdentifier(ParsedClass.Name) + ".mgenerated.h");
        }
    }

    const fs::path ManifestPath = OutputDir / "MClientManifest.generated.h";
    std::ofstream Out(ManifestPath);
    if (!Out)
    {
        std::cerr << "Failed to write generated client manifest: " << ManifestPath << "\n";
        return false;
    }

    Out << "#pragma once\n";
    Out << "// Generated by MHeaderTool\n\n";
    Out << "#include <cstring>\n";
    Out << "#include \"Common/Runtime/MLib.h\"\n";
    for (const std::string& IncludeHeader : SourceIncludeHeaders)
    {
        Out << "#include \"" << IncludeHeader << "\"\n";
    }
    for (const std::string& IncludeHeader : GeneratedIncludeHeaders)
    {
        Out << "#include \"" << IncludeHeader << "\"\n";
    }
    Out << "\n";
    Out << "namespace MClientManifest\n";
    Out << "{\n";
    Out << "using FBindParamsFn = bool(*)(uint64 ConnectionId, const TByteArray& Payload, TByteArray& OutParamStorage);\n";
    Out << "struct SEntry\n";
    Out << "{\n";
    Out << "    uint16 FunctionId;\n";
    Out << "    const char* OwnerType;\n";
    Out << "    const char* FunctionName;\n";
    Out << "    const char* ClientApiName;\n";
    Out << "    const char* ResponseTypeName;\n";
    Out << "    const char* Owner;\n";
    Out << "    const char* HeaderPath;\n";
    Out << "    const char* Transport;\n";
    Out << "    const char* MessageName;\n";
    Out << "    const char* RouteName;\n";
    Out << "    const char* TargetName;\n";
    Out << "    const char* AuthMode;\n";
    Out << "    const char* WrapMode;\n";
    Out << "    FBindParamsFn BindParams;\n";
    Out << "};\n\n";
    Out << "inline const SEntry* GetEntries()\n";
    Out << "{\n";
    Out << "    static const SEntry Entries[] = {\n";
    for (const SClientEntry& Entry : Entries)
    {
        Out << "        {" << Entry.FunctionIdExpr
            << ", \"" << EscapeCppStringLiteral(Entry.OwnerType)
            << "\", \"" << EscapeCppStringLiteral(Entry.FunctionName)
            << "\", \"" << EscapeCppStringLiteral(Entry.ClientApiName)
            << "\", \"" << EscapeCppStringLiteral(Entry.ResponseTypeName)
            << "\", \"" << EscapeCppStringLiteral(Entry.Owner)
            << "\", \"" << EscapeCppStringLiteral(Entry.HeaderPath)
            << "\", \"" << EscapeCppStringLiteral(Entry.Transport)
            << "\", \"" << EscapeCppStringLiteral(Entry.MessageName)
            << "\", \"" << EscapeCppStringLiteral(Entry.RouteName)
            << "\", \"" << EscapeCppStringLiteral(Entry.TargetName)
            << "\", \"" << EscapeCppStringLiteral(Entry.AuthMode)
            << "\", \"" << EscapeCppStringLiteral(Entry.WrapMode)
            << "\", " << Entry.BinderFunctionExpr << "},\n";
    }
    Out << "    };\n";
    Out << "    return Entries;\n";
    Out << "}\n\n";
    Out << "inline const SEntry* FindByFunctionId(uint16 FunctionId)\n";
    Out << "{\n";
    Out << "    const SEntry* Entries = GetEntries();\n";
    Out << "    for (size_t Index = 0; Index < " << Entries.size() << "; ++Index)\n";
    Out << "    {\n";
    Out << "        if (Entries[Index].FunctionId == FunctionId)\n";
    Out << "        {\n";
    Out << "            return &Entries[Index];\n";
    Out << "        }\n";
    Out << "    }\n";
    Out << "    return nullptr;\n";
    Out << "}\n\n";
    Out << "inline const SEntry* FindByMessageName(const char* OwnerType, const char* MessageName)\n";
    Out << "{\n";
    Out << "    if (!OwnerType || !MessageName)\n";
    Out << "    {\n";
    Out << "        return nullptr;\n";
    Out << "    }\n";
    Out << "    const SEntry* Entries = GetEntries();\n";
    Out << "    for (size_t Index = 0; Index < " << Entries.size() << "; ++Index)\n";
    Out << "    {\n";
    Out << "        const SEntry& Entry = Entries[Index];\n";
    Out << "        if (!Entry.OwnerType || !Entry.MessageName)\n";
    Out << "        {\n";
    Out << "            continue;\n";
    Out << "        }\n";
    Out << "        if (std::strcmp(Entry.OwnerType, OwnerType) == 0 && std::strcmp(Entry.MessageName, MessageName) == 0)\n";
    Out << "        {\n";
    Out << "            return &Entry;\n";
    Out << "        }\n";
    Out << "    }\n";
    Out << "    return nullptr;\n";
    Out << "}\n\n";
    Out << "inline size_t GetEntryCount()\n";
    Out << "{\n";
    Out << "    return " << Entries.size() << ";\n";
    Out << "}\n";
    Out << "} // namespace MClientManifest\n";
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
    Out << "set(MESSION_GENERATED_GROUPS\n";
    for (const auto& [Group, _] : GroupedSources)
    {
        Out << "    \"" << EscapeCMakeListValue(Group) << "\"\n";
    }
    Out << ")\n";

    for (const auto& [Group, Sources] : GroupedSources)
    {
        const std::string UpperGroup = ReplaceAll(Group, "-", "_");
        std::string VarName = "MESSION_GENERATED_" + UpperGroup + "_SOURCES";
        for (char& Ch : VarName)
        {
            Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
        }

        Out << "set(" << VarName << "\n";
        for (const fs::path& SourcePath : Sources)
        {
            Out << "    \"" << EscapeCMakePath(SourcePath) << "\"\n";
        }
        Out << ")\n";
    }

    Out << "set(MESSION_REFLECTED_TYPE_MANIFEST\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        Out << "    \""
            << EscapeCMakeListValue(std::string(GetTypeKindName(ParsedClass.Kind))) << "|"
            << EscapeCMakeListValue(ParsedClass.Name) << "|"
            << EscapeCMakeListValue(ParsedClass.Owner) << "|"
            << EscapeCMakeListValue(EscapeCMakePath(ParsedClass.HeaderPath))
            << "\"\n";
    }
    Out << ")\n";

    Out << "set(MESSION_REFLECTED_PROPERTY_MANIFEST\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        for (const SParsedProperty& Property : ParsedClass.Properties)
        {
            Out << "    \""
                << EscapeCMakeListValue(ParsedClass.Name) << "|"
                << EscapeCMakeListValue(Property.Name) << "|"
                << EscapeCMakeListValue(Property.Owner) << "|"
                << EscapeCMakeListValue(Property.Type) << "|"
                << EscapeCMakeListValue(Property.MacroArgs)
                << "\"\n";
        }
    }
    Out << ")\n";

    Out << "set(MESSION_REFLECTED_FUNCTION_MANIFEST\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        for (const SParsedFunction& Function : ParsedClass.Functions)
        {
            Out << "    \""
                << EscapeCMakeListValue(ParsedClass.Name) << "|"
                << EscapeCMakeListValue(Function.Name) << "|"
                << EscapeCMakeListValue(Function.Owner) << "|"
                << EscapeCMakeListValue(Function.Signature) << "|"
                << EscapeCMakeListValue(Function.MacroArgs) << "|"
                << EscapeCMakeListValue(EscapeCMakePath(ParsedClass.HeaderPath)) << "|"
                << EscapeCMakeListValue(Function.Transport) << "|"
                << EscapeCMakeListValue(Function.MessageName)
                << "\"\n";
        }
    }
    Out << ")\n";

    Out << "set(MESSION_REFLECTED_ENUM_VALUE_MANIFEST\n";
    for (const SParsedClass& ParsedClass : Classes)
    {
        if (ParsedClass.Kind != EParsedTypeKind::Enum)
        {
            continue;
        }

        for (const std::string& Value : ParsedClass.EnumValues)
        {
            Out << "    \""
                << EscapeCMakeListValue(ParsedClass.Name) << "|"
                << EscapeCMakeListValue(Value) << "|"
                << EscapeCMakeListValue(ParsedClass.Owner)
                << "\"\n";
        }
    }
    Out << ")\n";

    return true;
}
} // namespace

int main(int Argc, char** Argv)
{
    try
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

        if (!WriteGeneratedRpcManifest(Options.OutputDir, Classes))
        {
            return 1;
        }

        if (!WriteGeneratedReflectionManifest(Options.OutputDir, Classes))
        {
            return 1;
        }

        if (!WriteGeneratedClientManifest(Options.OutputDir, Classes))
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
    catch (const std::exception& Ex)
    {
        std::cerr << "MHeaderTool error: " << Ex.what() << "\n";
        return 1;
    }
}
