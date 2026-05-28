#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cctype>
#include <algorithm>

namespace MHeaderTool
{

// ============================================================================
// String View Helpers
// ============================================================================

inline std::string_view TrimView(std::string_view sv)
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
    {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
    {
        sv.remove_suffix(1);
    }
    return sv;
}

inline std::string_view TrimStartView(std::string_view sv)
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
    {
        sv.remove_prefix(1);
    }
    return sv;
}

// ============================================================================
// String Conversion (non-allocating where possible)
// ============================================================================

inline std::string ToString(std::string_view sv)
{
    return std::string(sv);
}

inline bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

inline bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// ============================================================================
// Character Classification
// ============================================================================

inline bool IsIdentifierChar(char ch)
{
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

inline bool IsWhitespace(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch));
}

// ============================================================================
// String Operations
// ============================================================================

inline std::string Trim(std::string_view text)
{
    return ToString(TrimView(text));
}

inline std::string ReplaceAll(std::string text, std::string_view from, std::string_view to)
{
    if (from.empty())
    {
        return text;
    }

    size_t pos = 0;
    while ((pos = text.find(from.data(), pos, from.size())) != std::string::npos)
    {
        text.replace(pos, from.size(), to.data(), to.size());
        pos += to.size();
    }
    return text;
}

inline std::string SanitizeIdentifier(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
        {
            result.push_back(ch);
        }
        else
        {
            result.push_back('_');
        }
    }
    return result;
}

inline std::string EscapeCppStringLiteral(std::string value)
{
    value = ReplaceAll(std::move(value), "\\", "\\\\");
    value = ReplaceAll(std::move(value), "\"", "\\\"");
    return value;
}

inline std::string EscapeJsonString(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
    {
        switch (ch)
        {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    return result;
}

// ============================================================================
// Find Operations
// ============================================================================

inline size_t FindMatching(const std::string& text, size_t openPos, char openChar, char closeChar)
{
    int depth = 0;
    bool inString = false;
    for (size_t i = openPos; i < text.size(); ++i)
    {
        char c = text[i];
        char prev = (i > 0) ? text[i-1] : '\0';

        // Handle strings
        if (c == '"' && prev != '\\') {
            inString = !inString;
            continue;
        }
        if (inString) continue;

        // Skip angle brackets when looking for parentheses or braces
        if ((openChar == '(' || openChar == '{') && (c == '<' || c == '>')) {
            continue;
        }

        if (c == openChar)
        {
            ++depth;
        }
        else if (c == closeChar)
        {
            --depth;
            if (depth == 0)
            {
                return i;
            }
        }
    }
    return std::string::npos;
}

inline size_t SkipWhitespace(const std::string& text, size_t pos)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
    {
        ++pos;
    }
    return pos;
}

inline bool IsKeywordAt(const std::string& text, size_t pos, std::string_view keyword)
{
    if (pos + keyword.size() > text.size())
    {
        return false;
    }

    if (text.compare(pos, keyword.size(), keyword) != 0)
    {
        return false;
    }

    const bool leftOk = (pos == 0) || !IsIdentifierChar(text[pos - 1]);
    const bool rightOk = (pos + keyword.size() >= text.size()) || !IsIdentifierChar(text[pos + keyword.size()]);
    return leftOk && rightOk;
}

// ============================================================================
// Split Operations
// ============================================================================

inline std::vector<std::string> SplitTopLevelArgs(const std::string& text)
{
    std::vector<std::string> parts;
    size_t partStart = 0;
    int parenDepth = 0;
    int angleDepth = 0;
    int braceDepth = 0;
    int bracketDepth = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        switch (ch)
        {
        case '(': ++parenDepth; break;
        case ')': --parenDepth; break;
        case '<': ++angleDepth; break;
        case '>': if (angleDepth > 0) --angleDepth; break;
        case '{': ++braceDepth; break;
        case '}': --braceDepth; break;
        case '[': ++bracketDepth; break;
        case ']': --bracketDepth; break;
        case ',':
            if (parenDepth == 0 && angleDepth == 0 && braceDepth == 0 && bracketDepth == 0)
            {
                parts.push_back(Trim(text.substr(partStart, i - partStart)));
                partStart = i + 1;
            }
            break;
        default: break;
        }
    }

    if (partStart <= text.size())
    {
        parts.push_back(Trim(text.substr(partStart)));
    }

    return parts;
}

inline std::vector<std::string> SplitTopLevelPipes(const std::string& text)
{
    std::vector<std::string> parts;
    size_t partStart = 0;
    int parenDepth = 0;
    int angleDepth = 0;
    int braceDepth = 0;
    int bracketDepth = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        switch (ch)
        {
        case '(': ++parenDepth; break;
        case ')': --parenDepth; break;
        case '<': ++angleDepth; break;
        case '>': if (angleDepth > 0) --angleDepth; break;
        case '{': ++braceDepth; break;
        case '}': --braceDepth; break;
        case '[': ++bracketDepth; break;
        case ']': --bracketDepth; break;
        case '|':
            if (parenDepth == 0 && angleDepth == 0 && braceDepth == 0 && bracketDepth == 0)
            {
                parts.push_back(Trim(text.substr(partStart, i - partStart)));
                partStart = i + 1;
            }
            break;
        default: break;
        }
    }

    if (partStart <= text.size())
    {
        parts.push_back(Trim(text.substr(partStart)));
    }

    return parts;
}

// ============================================================================
// Masking Operations
// ============================================================================

inline std::string MakeMaskedCopy(const std::string& text)
{
    std::string result = text;
    enum class EState : uint8_t
    {
        Normal,
        LineComment,
        BlockComment,
        StringLiteral,
        CharLiteral
    };

    EState state = EState::Normal;
    for (size_t i = 0; i < result.size(); ++i)
    {
        char current = result[i];
        char next = (i + 1 < result.size()) ? result[i + 1] : '\0';

        switch (state)
        {
        case EState::Normal:
            if (current == '/' && next == '/')
            {
                result[i] = ' ';
                result[i + 1] = ' ';
                ++i;
                state = EState::LineComment;
            }
            else if (current == '/' && next == '*')
            {
                result[i] = ' ';
                result[i + 1] = ' ';
                ++i;
                state = EState::BlockComment;
            }
            else if (current == '"')
            {
                result[i] = ' ';
                state = EState::StringLiteral;
            }
            else if (current == '\'')
            {
                result[i] = ' ';
                state = EState::CharLiteral;
            }
            break;

        case EState::LineComment:
            if (current != '\n')
            {
                result[i] = ' ';
            }
            else
            {
                state = EState::Normal;
            }
            break;

        case EState::BlockComment:
            if (current == '*' && next == '/')
            {
                result[i] = ' ';
                result[i + 1] = ' ';
                ++i;
                state = EState::Normal;
            }
            else if (current != '\n')
            {
                result[i] = ' ';
            }
            break;

        case EState::StringLiteral:
            if (current == '\\' && next != '\0')
            {
                result[i] = ' ';
                result[i + 1] = ' ';
                ++i;
            }
            else if (current == '"')
            {
                result[i] = ' ';
                state = EState::Normal;
            }
            else if (current != '\n')
            {
                result[i] = ' ';
            }
            break;

        case EState::CharLiteral:
            if (current == '\\' && next != '\0')
            {
                result[i] = ' ';
                result[i + 1] = ' ';
                ++i;
            }
            else if (current == '\'')
            {
                result[i] = ' ';
                state = EState::Normal;
            }
            else if (current != '\n')
            {
                result[i] = ' ';
            }
            break;
        }
    }

    return result;
}

// ============================================================================
// Macro Helpers
// ============================================================================

inline std::string StripEnclosingPair(std::string value, char open, char close)
{
    value = Trim(value);
    if (value.size() >= 2 && value.front() == open && value.back() == close)
    {
        return Trim(value.substr(1, value.size() - 2));
    }
    return value;
}

inline std::string UnquoteStringLiteral(std::string value)
{
    value = Trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

inline std::optional<std::string> ExtractMacroValue(const std::string& macroArgs, std::string_view key)
{
    for (const std::string& part : SplitTopLevelArgs(macroArgs))
    {
        size_t equalsPos = part.find('=');
        if (equalsPos == std::string::npos)
        {
            continue;
        }

        std::string candidateKey = Trim(part.substr(0, equalsPos));
        if (candidateKey != key)
        {
            continue;
        }

        return Trim(part.substr(equalsPos + 1));
    }

    return std::nullopt;
}

}  // namespace MHeaderTool
