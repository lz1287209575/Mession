#pragma once

#include "Common/Runtime/MLib.h"

// 如果定义了 USE_NLOHMANN_JSON，则内部实现委托给 nlohmann::json；
// 否则回退到轻量自研实现，保证不引入外部依赖。
#if defined(USE_NLOHMANN_JSON)

#include <nlohmann/json.hpp>

using MJsonImpl = nlohmann::json;

class MJsonWriter
{
public:
    MJsonWriter() = default;

    static MJsonWriter Object()
    {
        MJsonWriter W;
        W.J = MJsonImpl::object();
        return W;
    }

    static MJsonWriter Array()
    {
        MJsonWriter W;
        W.J = MJsonImpl::array();
        return W;
    }

    void BeginObject() { EnsureContainer(MJsonImpl::object()); }
    void EndObject() {}
    void BeginArray() { EnsureContainer(MJsonImpl::array()); }
    void EndArray() {}

    void Key(const MString& Key)
    {
        CurrentKey = Key;
    }

    void Value(const MString& V)
    {
        Assign(CurrentKey, V);
        CurrentKey.clear();
    }

    void Value(const char* V)
    {
        Assign(CurrentKey, MString(V ? V : ""));
        CurrentKey.clear();
    }

    void Value(bool V)
    {
        Assign(CurrentKey, V);
        CurrentKey.clear();
    }

    void Value(int64 V)
    {
        Assign(CurrentKey, V);
        CurrentKey.clear();
    }

    void Value(uint64 V)
    {
        Assign(CurrentKey, V);
        CurrentKey.clear();
    }

    void Value(double V)
    {
        Assign(CurrentKey, V);
        CurrentKey.clear();
    }

    void Null()
    {
        Assign(CurrentKey, nullptr);
        CurrentKey.clear();
    }

    MString ToString() const
    {
        return J.dump();
    }

private:
    MJsonImpl J = MJsonImpl::object();
    MString CurrentKey;

    void EnsureContainer(const MJsonImpl& Default)
    {
        if (J.is_null())
        {
            J = Default;
        }
    }

    template<typename T>
    void Assign(const MString& Key, T&& V)
    {
        if (!Key.empty())
        {
            J[Key] = std::forward<T>(V);
        }
        else
        {
            // 顶层为数组时追加元素
            if (!J.is_array())
            {
                if (J.is_null())
                {
                    J = MJsonImpl::array();
                }
                else
                {
                    MJsonImpl Old = J;
                    J = MJsonImpl::array();
                    J.push_back(Old);
                }
            }
            J.push_back(std::forward<T>(V));
        }
    }
};

#else  // USE_NLOHMANN_JSON

// 轻量 Json 写入器：仅做字符串拼接，不分配临时对象。
// 支持 Object / Array / 字符串 / 数字 / bool / null，主要用于调试和简单配置。
class MJsonWriter
{
public:
    MJsonWriter()
    {
        ScopeStack.reserve(8);
    }

    static MJsonWriter Object()
    {
        MJsonWriter W;
        W.BeginObject();
        return W;
    }

    static MJsonWriter Array()
    {
        MJsonWriter W;
        W.BeginArray();
        return W;
    }

    void BeginObject()
    {
        AppendCommaIfNeeded();
        Result.push_back('{');
        ScopeStack.push_back(Scope{true, EScopeType::Object});
    }

    void EndObject()
    {
        if (!ScopeStack.empty() && ScopeStack.back().Type == EScopeType::Object)
        {
            Result.push_back('}');
            ScopeStack.pop_back();
            MarkElementWritten();
        }
    }

    void BeginArray()
    {
        AppendCommaIfNeeded();
        Result.push_back('[');
        ScopeStack.push_back(Scope{true, EScopeType::Array});
    }

    void EndArray()
    {
        if (!ScopeStack.empty() && ScopeStack.back().Type == EScopeType::Array)
        {
            Result.push_back(']');
            ScopeStack.pop_back();
            MarkElementWritten();
        }
    }

    void Key(const MString& Key)
    {
        AppendCommaIfNeeded();
        WriteString(Key);
        Result.push_back(':');
        if (!ScopeStack.empty())
        {
            ScopeStack.back().bFirst = true;
        }
    }

    void Value(const MString& V)
    {
        AppendCommaIfNeeded();
        WriteString(V);
        MarkElementWritten();
    }

    void Value(const char* V)
    {
        AppendCommaIfNeeded();
        WriteString(MString(V ? V : ""));
        MarkElementWritten();
    }

    void Value(bool V)
    {
        AppendCommaIfNeeded();
        Result += V ? "true" : "false";
        MarkElementWritten();
    }

    void Value(int64 V)
    {
        AppendCommaIfNeeded();
        Result += std::to_string(V);
        MarkElementWritten();
    }

    void Value(uint64 V)
    {
        AppendCommaIfNeeded();
        Result += std::to_string(V);
        MarkElementWritten();
    }

    void Value(double V)
    {
        AppendCommaIfNeeded();
        Result += std::to_string(V);
        MarkElementWritten();
    }

    void Null()
    {
        AppendCommaIfNeeded();
        Result += "null";
        MarkElementWritten();
    }

    const MString& ToString() const
    {
        return Result;
    }

private:
    enum class EScopeType : uint8
    {
        Object,
        Array
    };

    struct Scope
    {
        bool bFirst = true;
        EScopeType Type = EScopeType::Object;
    };

    MString Result;
    TVector<Scope> ScopeStack;

    void AppendCommaIfNeeded()
    {
        if (!ScopeStack.empty())
        {
            Scope& S = ScopeStack.back();
            if (!S.bFirst)
            {
                Result.push_back(',');
            }
        }
    }

    void MarkElementWritten()
    {
        if (!ScopeStack.empty())
        {
            ScopeStack.back().bFirst = false;
        }
    }

    static void EscapeString(const MString& In, MString& Out)
    {
        for (char C : In)
        {
            switch (C)
            {
            case '\"': Out += "\\\""; break;
            case '\\': Out += "\\\\"; break;
            case '\b': Out += "\\b";  break;
            case '\f': Out += "\\f";  break;
            case '\n': Out += "\\n";  break;
            case '\r': Out += "\\r";  break;
            case '\t': Out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(C) < 0x20)
                {
                    char Buf[7];
                    std::snprintf(Buf, sizeof(Buf), "\\u%04x", C & 0xff);
                    Out += Buf;
                }
                else
                {
                    Out.push_back(C);
                }
                break;
            }
        }
    }

    void WriteString(const MString& S)
    {
        Result.push_back('\"');
        EscapeString(S, Result);
        Result.push_back('\"');
    }
};

#endif  // USE_NLOHMANN_JSON

// 轻量 Json 读取结果类型
enum class EJsonType : uint8
{
    Null,
    Boolean,
    Number,
    String,
    Object,
    Array
};

struct MJsonValue
{
    EJsonType Type = EJsonType::Null;
    bool BoolValue = false;
    double NumberValue = 0.0;
    MString StringValue;
    TMap<MString, MJsonValue> ObjectValue;
    TVector<MJsonValue> ArrayValue;

    bool IsNull() const { return Type == EJsonType::Null; }
    bool IsBool() const { return Type == EJsonType::Boolean; }
    bool IsNumber() const { return Type == EJsonType::Number; }
    bool IsString() const { return Type == EJsonType::String; }
    bool IsObject() const { return Type == EJsonType::Object; }
    bool IsArray() const { return Type == EJsonType::Array; }
};

// 极简 Json 读取器：仅为配置/调试使用，不做完整错误恢复。
class MJsonReader
{
public:
    static bool Parse(const MString& Text, MJsonValue& OutValue, MString& OutError)
    {
        MJsonReader R(Text);
        R.SkipWhitespace();
        if (!R.ParseValue(OutValue))
        {
            OutError = R.ErrorMessage.empty() ? "Parse failed" : R.ErrorMessage;
            return false;
        }
        R.SkipWhitespace();
        if (!R.EndOfInput())
        {
            OutError = "Trailing characters after JSON value";
            return false;
        }
        return true;
    }

private:
    explicit MJsonReader(const MString& InText)
        : Text(InText)
    {
    }

    bool EndOfInput() const
    {
        return Index >= Text.size();
    }

    char Peek() const
    {
        return EndOfInput() ? '\0' : Text[Index];
    }

    char Get()
    {
        return EndOfInput() ? '\0' : Text[Index++];
    }

    void SkipWhitespace()
    {
        while (!EndOfInput())
        {
            char C = Peek();
            if (C == ' ' || C == '\t' || C == '\n' || C == '\r')
            {
                ++Index;
            }
            else
            {
                break;
            }
        }
    }

    bool ParseValue(MJsonValue& Out)
    {
        SkipWhitespace();
        char C = Peek();
        if (C == 'n')
        {
            return ParseLiteral("null", EJsonType::Null, Out);
        }
        if (C == 't')
        {
            return ParseLiteral("true", EJsonType::Boolean, Out, true);
        }
        if (C == 'f')
        {
            return ParseLiteral("false", EJsonType::Boolean, Out, false);
        }
        if (C == '\"')
        {
            Out.Type = EJsonType::String;
            return ParseString(Out.StringValue);
        }
        if (C == '{')
        {
            Out.Type = EJsonType::Object;
            return ParseObject(Out.ObjectValue);
        }
        if (C == '[')
        {
            Out.Type = EJsonType::Array;
            return ParseArray(Out.ArrayValue);
        }
        if (C == '-' || (C >= '0' && C <= '9'))
        {
            Out.Type = EJsonType::Number;
            return ParseNumber(Out.NumberValue);
        }

        ErrorMessage = "Unexpected character in JSON value";
        return false;
    }

    bool ParseLiteral(const char* Literal, EJsonType Type, MJsonValue& Out, bool BoolValue = false)
    {
        const size_t Len = std::strlen(Literal);
        if (Text.size() - Index < Len)
        {
            ErrorMessage = "Unexpected end of input in literal";
            return false;
        }
        if (Text.compare(Index, Len, Literal) != 0)
        {
            ErrorMessage = "Invalid literal";
            return false;
        }
        Index += Len;
        Out.Type = Type;
        if (Type == EJsonType::Boolean)
        {
            Out.BoolValue = BoolValue;
        }
        return true;
    }

    bool ParseString(MString& Out)
    {
        if (Get() != '\"')
        {
            ErrorMessage = "Expected '\"' at beginning of string";
            return false;
        }
        Out.clear();
        while (!EndOfInput())
        {
            char C = Get();
            if (C == '\"')
            {
                return true;
            }
            if (C == '\\')
            {
                if (EndOfInput())
                {
                    ErrorMessage = "Unexpected end of input in escape";
                    return false;
                }
                char E = Get();
                switch (E)
                {
                case '\"': Out.push_back('\"'); break;
                case '\\': Out.push_back('\\'); break;
                case '/':  Out.push_back('/');  break;
                case 'b':  Out.push_back('\b'); break;
                case 'f':  Out.push_back('\f'); break;
                case 'n':  Out.push_back('\n'); break;
                case 'r':  Out.push_back('\r'); break;
                case 't':  Out.push_back('\t'); break;
                default:
                    ErrorMessage = "Unsupported escape sequence";
                    return false;
                }
            }
            else
            {
                Out.push_back(C);
            }
        }
        ErrorMessage = "Unterminated string";
        return false;
    }

    bool ParseNumber(double& Out)
    {
        size_t Start = Index;
        if (Peek() == '-')
        {
            ++Index;
        }
        while (!EndOfInput() && std::isdigit(static_cast<unsigned char>(Peek())))
        {
            ++Index;
        }
        if (!EndOfInput() && Peek() == '.')
        {
            ++Index;
            while (!EndOfInput() && std::isdigit(static_cast<unsigned char>(Peek())))
            {
                ++Index;
            }
        }
        MString NumStr = Text.substr(Start, Index - Start);
        try
        {
            Out = std::stod(NumStr);
        }
        catch (...)
        {
            ErrorMessage = "Invalid number";
            return false;
        }
        return true;
    }

    bool ParseObject(TMap<MString, MJsonValue>& Out)
    {
        if (Get() != '{')
        {
            ErrorMessage = "Expected '{'";
            return false;
        }
        SkipWhitespace();
        if (Peek() == '}')
        {
            Get();
            return true;
        }
        while (true)
        {
            SkipWhitespace();
            MString Key;
            if (!ParseString(Key))
            {
                return false;
            }
            SkipWhitespace();
            if (Get() != ':')
            {
                ErrorMessage = "Expected ':' after object key";
                return false;
            }
            MJsonValue Value;
            if (!ParseValue(Value))
            {
                return false;
            }
            Out.emplace(Key, std::move(Value));
            SkipWhitespace();
            char C = Get();
            if (C == '}')
            {
                return true;
            }
            if (C != ',')
            {
                ErrorMessage = "Expected ',' or '}' in object";
                return false;
            }
        }
    }

    bool ParseArray(TVector<MJsonValue>& Out)
    {
        if (Get() != '[')
        {
            ErrorMessage = "Expected '['";
            return false;
        }
        SkipWhitespace();
        if (Peek() == ']')
        {
            Get();
            return true;
        }
        while (true)
        {
            MJsonValue V;
            if (!ParseValue(V))
            {
                return false;
            }
            Out.push_back(std::move(V));
            SkipWhitespace();
            char C = Get();
            if (C == ']')
            {
                return true;
            }
            if (C != ',')
            {
                ErrorMessage = "Expected ',' or ']' in array";
                return false;
            }
        }
    }

private:
    MString Text;
    size_t Index = 0;
    MString ErrorMessage;
};

