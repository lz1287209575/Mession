#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Enum Parser - 解析枚举 (支持 MENUM 宏或直接 enum class)
// ============================================================================

class EnumParser
{
public:
    EnumParser() = default;

    // 解析头文件中的所有枚举
    std::vector<SParsedClass> ParseEnumsInHeader(
        const fs::path& headerPath,
        const std::string& contents) const
    {
        std::vector<SParsedClass> enums;
        const std::string masked = MakeMaskedCopy(contents);

        // 优先处理带有 MENUM 宏的枚举
        size_t searchPos = 0;
        while (true)
        {
            const size_t markerPos = masked.find("MENUM(", searchPos);
            if (markerPos == std::string::npos)
            {
                break;
            }

            if (IsMacroDefinitionAt(contents, markerPos))
            {
                searchPos = markerPos + 1;
                continue;
            }

            // MENUM 宏附近找 enum 关键字
            size_t enumPos = masked.find("enum", markerPos);
            auto parsed = ParseEnumAtPosition(masked, contents, headerPath, markerPos, enumPos);
            if (parsed)
            {
                enums.push_back(std::move(*parsed));
                searchPos = markerPos + 1;
            }
            else
            {
                searchPos = markerPos + 1;
            }
        }

        // 处理没有 MENUM 宏的 enum class（备用方案）
        searchPos = 0;
        while (true)
        {
            const size_t enumPos = masked.find("enum class", searchPos);
            if (enumPos == std::string::npos)
            {
                break;
            }

            // 检查是否已经有带 MENUM 的枚举在同一位置
            bool hasMENUMMarker = false;
            for (const auto& e : enums)
            {
                if (e.HeaderPath == headerPath && e.SourceLine == enumPos)
                {
                    hasMENUMMarker = true;
                    break;
                }
            }

            if (!hasMENUMMarker)
            {
                auto parsed = ParseEnumAtPosition(masked, contents, headerPath, enumPos, enumPos);
                if (parsed)
                {
                    enums.push_back(std::move(*parsed));
                }
            }

            searchPos = enumPos + 1;
        }

        return enums;
    }

private:
    // 解析指定位置附近的枚举
    std::optional<SParsedClass> ParseEnumAtPosition(
        const std::string& masked,
        const std::string& contents,
        const fs::path& headerPath,
        size_t searchStart,
        size_t enumPos) const
    {
        // enumPos 已经是已知的，不需要再查找

        size_t cursor = SkipWhitespace(masked, enumPos + 4);
        bool bScopedEnum = StartsWith(masked.substr(cursor), "class");
        if (bScopedEnum)
        {
            cursor = SkipWhitespace(masked, cursor + 5);
        }

        const std::optional<std::string> enumName = ReadIdentifier(masked, cursor);
        if (!enumName)
        {
            return std::nullopt;
        }

        const size_t braceOpen = masked.find('{', cursor);
        if (braceOpen == std::string::npos || braceOpen > enumPos + 500)
        {
            return std::nullopt;
        }

        const size_t braceClose = FindMatching(masked, braceOpen, '{', '}');
        if (braceClose == std::string::npos)
        {
            return std::nullopt;
        }

        SParsedClass parsed;
        parsed.Kind = EParsedTypeKind::Enum;
        parsed.Name = *enumName;
        parsed.HeaderPath = headerPath;
        parsed.Owner = DetermineOwnerFromHeaderPath(headerPath);
        parsed.bScopedEnum = bScopedEnum;
        parsed.SourceLine = enumPos;

        // 解析底层类型
        const size_t underlyingColon = masked.find(':', cursor);
        if (underlyingColon != std::string::npos && underlyingColon < braceOpen)
        {
            parsed.EnumUnderlyingType = NormalizeReflectionType(
                contents.substr(underlyingColon + 1, braceOpen - underlyingColon - 1));
        }

        // 解析 MENUM 宏参数（如果有）
        const size_t macroMarkerPos = masked.find("MENUM(", searchStart);
        if (macroMarkerPos != std::string::npos && macroMarkerPos < enumPos + 100)
        {
            const size_t macroOpen = masked.find('(', macroMarkerPos);
            const size_t macroClose = (macroOpen == std::string::npos)
                ? std::string::npos
                : FindMatching(masked, macroOpen, '(', ')');
            if (macroOpen != std::string::npos && macroClose != std::string::npos)
            {
                const std::string macroArgs = contents.substr(macroOpen + 1, macroClose - macroOpen - 1);
                if (auto owner = ExtractMacroValue(macroArgs, "Owner"))
                {
                    parsed.Owner = *owner;
                }
            }
        }

        // 解析枚举值
        const std::string enumBody = contents.substr(braceOpen + 1, braceClose - braceOpen - 1);
        for (const std::string& entry : SplitTopLevelArgs(enumBody))
        {
            std::string clean = Trim(entry);
            if (clean.empty())
            {
                continue;
            }
            const size_t equalsPos = clean.find('=');
            if (equalsPos != std::string::npos)
            {
                clean = Trim(clean.substr(0, equalsPos));
            }
            if (!clean.empty())
            {
                parsed.EnumValues.push_back(clean);
            }
        }

        return parsed;
    }

    bool IsMacroDefinitionAt(const std::string& text, size_t pos) const
    {
        const size_t lineStart = text.rfind('\n', pos);
        const size_t prefixStart = (lineStart == std::string::npos) ? 0 : (lineStart + 1);
        size_t cursor = prefixStart;
        while (cursor < pos && std::isspace(static_cast<unsigned char>(text[cursor])))
        {
            ++cursor;
        }
        return text.compare(cursor, 7, "#define") == 0;
    }

    std::string NormalizeReflectionType(std::string typeName) const
    {
        typeName = Trim(typeName);
        while (StartsWith(typeName, "const "))
        {
            typeName = Trim(typeName.substr(6));
        }
        while (!typeName.empty() && (typeName.back() == '&' || typeName.back() == '*'))
        {
            typeName.pop_back();
            typeName = Trim(typeName);
        }
        return typeName;
    }

    std::string DetermineOwnerFromHeaderPath(const fs::path& headerPath) const
    {
        std::string relPath = headerPath.generic_string();
        size_t serversPos = relPath.find("/Servers/");
        if (serversPos != std::string::npos)
        {
            size_t start = serversPos + 9;
            size_t end = relPath.find('/', start);
            if (end != std::string::npos)
            {
                return relPath.substr(start, end - start);
            }
        }
        return "Shared";
    }

    std::optional<std::string> ReadIdentifier(const std::string& text, size_t& inOutPos) const
    {
        const size_t start = inOutPos;
        if (start >= text.size() || (!std::isalpha(static_cast<unsigned char>(text[start])) && text[start] != '_'))
        {
            return std::nullopt;
        }

        size_t end = start + 1;
        while (end < text.size() && IsIdentifierChar(text[end]))
        {
            ++end;
        }

        inOutPos = end;
        return text.substr(start, end - start);
    }
};

}  // namespace MHeaderTool
