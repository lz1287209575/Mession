#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"
#include <optional>
#include <filesystem>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Class Region
// ============================================================================

struct SClassRegion
{
    std::string Keyword;
    std::string Name;
    size_t BodyOpen = std::string::npos;
    size_t BodyClose = std::string::npos;
};

// ============================================================================
// Class Parser - 解析 class/struct 定义
// ============================================================================

class ClassParser
{
public:
    ClassParser() = default;

    // 解析所有类区域
    std::vector<SClassRegion> ParseClassRegions(const std::string& contents) const
    {
        std::vector<SClassRegion> regions;
        const std::string masked = MakeMaskedCopy(contents);
        size_t searchPos = 0;

        while (searchPos < masked.size())
        {
            // 找到下一个 class 或 struct
            size_t bestPos = std::string::npos;
            std::string keyword;

            const size_t classPos = masked.find("class", searchPos);
            const size_t structPos = masked.find("struct", searchPos);

            if (classPos != std::string::npos && (bestPos == std::string::npos || classPos < bestPos))
            {
                bestPos = classPos;
                keyword = "class";
            }
            if (structPos != std::string::npos && (bestPos == std::string::npos || structPos < bestPos))
            {
                bestPos = structPos;
                keyword = "struct";
            }

            if (bestPos == std::string::npos)
            {
                break;
            }

            // 验证是关键字而不是其他词的一部分
            if (!IsKeywordAt(masked, bestPos, keyword))
            {
                searchPos = bestPos + 1;
                continue;
            }

            size_t cursor = SkipWhitespace(masked, bestPos + keyword.size());

            // 跳过属性
            if (cursor < masked.size() && masked[cursor] == '[')
            {
                size_t attrClose = FindMatching(masked, cursor, '[', ']');
                if (attrClose == std::string::npos)
                {
                    searchPos = bestPos + 1;
                    continue;
                }
                cursor = SkipWhitespace(masked, attrClose + 1);
            }

            // 读取类名
            const std::optional<std::string> className = ReadIdentifier(masked, cursor);
            if (!className)
            {
                searchPos = bestPos + 1;
                continue;
            }

            // 跳过模板参数
            size_t nameEnd = cursor;
            size_t suffixCursor = SkipWhitespace(masked, nameEnd);
            if (suffixCursor < masked.size() && masked[suffixCursor] == '<')
            {
                searchPos = suffixCursor + 1;
                continue;
            }

            // 找到类体开始
            size_t bracePos = cursor;
            while (bracePos < masked.size() && masked[bracePos] != '{' && masked[bracePos] != ';')
            {
                ++bracePos;
            }

            if (bracePos >= masked.size() || masked[bracePos] != '{')
            {
                searchPos = cursor;
                continue;
            }

            size_t closeBrace = FindMatching(masked, bracePos, '{', '}');
            if (closeBrace == std::string::npos)
            {
                searchPos = bracePos + 1;
                continue;
            }

            regions.push_back({keyword, *className, bracePos, closeBrace});
            searchPos = closeBrace + 1;
        }

        return regions;
    }

    // 解析单个类的完整信息
    std::optional<SParsedClass> ParseClass(
        const fs::path& headerPath,
        const std::string& contents,
        const SClassRegion& region) const
    {
        const std::string classBody = contents.substr(region.BodyOpen + 1, region.BodyClose - region.BodyOpen - 1);

        SParsedClass parsed;
        parsed.Kind = (region.Keyword == "struct") ? EParsedTypeKind::Struct : EParsedTypeKind::Class;
        parsed.Name = region.Name;
        parsed.HeaderPath = headerPath;

        // 检查是否有 MGENERATED_BODY
        const bool hasGeneratedBody = classBody.find("MGENERATED_BODY(") != std::string::npos;

        // 跳过没有 MGENERATED_BODY 的类
        if (parsed.Kind == EParsedTypeKind::Class && !hasGeneratedBody)
        {
            return std::nullopt;
        }

        // 解析类型标记
        ParseTypeMarkerMetadata(contents, region, parsed);

        // 检查 MSTRUCT 标记
        const bool hasTypeMarker = HasNearbyTypeMarker(contents, region, parsed.Kind);
        if (parsed.Kind == EParsedTypeKind::Struct && !hasTypeMarker)
        {
            return std::nullopt;
        }

        // 设置 Owner
        if (parsed.Owner.empty())
        {
            parsed.Owner = DetermineOwnerFromHeaderPath(headerPath);
        }

        // 解析父类信息
        if (hasGeneratedBody)
        {
            ParseGeneratedBodyMetadata(classBody, parsed);
        }

        // 解析类型别名
        parsed.TypeAliases = ParseTypeAliasesInBody(classBody);

        // 解析属性
        parsed.Properties = ParsePropertiesInTypeBody(classBody);

        // 解析函数
        parsed.Functions = ParseFunctionsInClassBody(classBody);

        // 解析 MPROPERTY Injection 标记
        for (const auto& prop : parsed.Properties)
        {
            if (prop.MacroArgs.find("Injection") != std::string::npos)
            {
                parsed.InjectionProperties.push_back(prop);
            }
        }

        // 解析 MFUNCTION Injection 标记
        for (const auto& func : parsed.Functions)
        {
            if (func.MacroArgs.find("Injection") != std::string::npos)
            {
                parsed.InjectionFunctions.push_back(func);
            }
        }

        // 设置属性和函数的 Owner
        for (auto& prop : parsed.Properties)
        {
            if (prop.Owner.empty())
            {
                prop.Owner = parsed.Owner;
            }
        }
        for (auto& func : parsed.Functions)
        {
            if (func.Owner.empty())
            {
                func.Owner = parsed.Owner;
            }
        }

        return parsed;
    }

private:
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

    void ParseTypeMarkerMetadata(const std::string& contents, const SClassRegion& region, SParsedClass& parsed) const
    {
        const char* marker = (parsed.Kind == EParsedTypeKind::Struct) ? "MSTRUCT(" : "MCLASS(";
        const size_t searchStart = (region.BodyOpen > 512) ? (region.BodyOpen - 512) : 0;
        const size_t markerPos = contents.rfind(marker, region.BodyOpen);
        if (markerPos == std::string::npos || markerPos < searchStart)
        {
            return;
        }

        const size_t macroOpen = contents.find('(', markerPos);
        const size_t macroClose = (macroOpen == std::string::npos)
            ? std::string::npos
            : FindMatching(contents, macroOpen, '(', ')');
        if (macroOpen == std::string::npos || macroClose == std::string::npos || macroClose > region.BodyOpen)
        {
            return;
        }

        const std::string macroArgs = contents.substr(macroOpen + 1, macroClose - macroOpen - 1);
        if (auto owner = ExtractMacroValue(macroArgs, "Owner"))
        {
            parsed.Owner = *owner;
        }
        if (auto type = ExtractMacroValue(macroArgs, "Type"))
        {
            parsed.ReflectionType = *type;
        }
        else if (parsed.Kind == EParsedTypeKind::Struct)
        {
            parsed.ReflectionType = "Struct";
        }
        if (auto injectionClass = ExtractMacroValue(macroArgs, "InjectionClass"))
        {
            parsed.InjectionClass = *injectionClass;
        }
    }

    void ParseGeneratedBodyMetadata(const std::string& typeBody, SParsedClass& parsed) const
    {
        const size_t macroPos = typeBody.find("MGENERATED_BODY(");
        if (macroPos == std::string::npos)
        {
            return;
        }

        const size_t macroOpen = typeBody.find('(', macroPos);
        const size_t macroClose = (macroOpen == std::string::npos)
            ? std::string::npos
            : FindMatching(typeBody, macroOpen, '(', ')');
        if (macroOpen == std::string::npos || macroClose == std::string::npos)
        {
            return;
        }

        const auto parts = SplitTopLevelArgs(typeBody.substr(macroOpen + 1, macroClose - macroOpen - 1));
        if (parts.size() >= 2)
        {
            parsed.ParentClass = parts[1];
        }
        if (parts.size() >= 3)
        {
            parsed.ClassFlagsExpr = parts[2];
        }
    }

    bool HasNearbyTypeMarker(const std::string& contents, const SClassRegion& region, EParsedTypeKind kind) const
    {
        const char* marker = (kind == EParsedTypeKind::Struct) ? "MSTRUCT(" : "MCLASS(";
        const size_t searchStart = (region.BodyOpen > 512) ? (region.BodyOpen - 512) : 0;
        const size_t markerPos = contents.rfind(marker, region.BodyOpen);
        if (markerPos == std::string::npos || markerPos < searchStart)
        {
            return false;
        }
        return !IsMacroDefinitionAt(contents, markerPos);
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
        return StartsWith(text.substr(cursor, pos - cursor), "#define");
    }

    std::map<std::string, std::string> ParseTypeAliasesInBody(const std::string& classBody) const
    {
        std::map<std::string, std::string> aliases;
        size_t searchPos = 0;

        while (true)
        {
            searchPos = classBody.find("using ", searchPos);
            if (searchPos == std::string::npos)
            {
                break;
            }

            size_t nameCursor = searchPos + 6;
            const std::optional<std::string> aliasName = ReadIdentifier(classBody, nameCursor);
            if (!aliasName)
            {
                searchPos += 6;
                continue;
            }

            size_t cursor = nameCursor;
            cursor = SkipWhitespace(classBody, cursor);
            if (cursor >= classBody.size() || classBody[cursor] != '=')
            {
                searchPos = cursor;
                continue;
            }

            const size_t semiPos = classBody.find(';', cursor);
            if (semiPos == std::string::npos)
            {
                break;
            }

            aliases[*aliasName] = Trim(classBody.substr(cursor + 1, semiPos - cursor - 1));
            searchPos = semiPos + 1;
        }

        return aliases;
    }

    std::vector<SParsedProperty> ParsePropertiesInTypeBody(const std::string& typeBody) const
    {
        std::vector<SParsedProperty> properties;
        size_t searchPos = 0;

        while (true)
        {
            const size_t macroPos = typeBody.find("MPROPERTY(", searchPos);
            if (macroPos == std::string::npos) break;

            const size_t macroOpen = typeBody.find('(', macroPos);
            const size_t macroClose = (macroOpen == std::string::npos) ? std::string::npos : FindMatching(typeBody, macroOpen, '(', ')');
            if (macroOpen == std::string::npos || macroClose == std::string::npos) break;

            const std::string macroArgs = typeBody.substr(macroOpen + 1, macroClose - macroOpen - 1);
            const size_t declStart = macroClose + 1;
            const size_t declEnd = typeBody.find(';', declStart);
            if (declEnd == std::string::npos) break;

            const std::string declaration = typeBody.substr(declStart, declEnd - declStart + 1);
            std::string clean = Trim(declaration);
            const size_t semicolon = clean.rfind(';');
            if (semicolon != std::string::npos) clean = Trim(clean.substr(0, semicolon));
            const size_t equalsPos = clean.find('=');
            const std::string left = Trim(clean.substr(0, equalsPos));
            if (left.empty() || left.find('(') != std::string::npos) {
                searchPos = declEnd + 1;
                continue;
            }

            size_t nameEnd = left.size();
            while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(left[nameEnd - 1]))) --nameEnd;
            size_t nameStart = nameEnd;
            while (nameStart > 0 && IsIdentifierChar(left[nameStart - 1])) --nameStart;
            if (nameStart == nameEnd) {
                searchPos = declEnd + 1;
                continue;
            }

            SParsedProperty prop;
            prop.MacroArgs = Trim(macroArgs);
            prop.Name = left.substr(nameStart, nameEnd - nameStart);
            prop.Type = Trim(left.substr(0, nameStart));
            if (auto owner = ExtractMacroValue(prop.MacroArgs, "Owner")) prop.Owner = *owner;
            prop.FlagsExpr = "EPropertyFlags::None";
            properties.push_back(prop);
            searchPos = declEnd + 1;
        }
        return properties;
    }

    std::vector<SParsedFunction> ParseFunctionsInClassBody(const std::string& classBody) const
    {
        std::vector<SParsedFunction> functions;
        size_t searchPos = 0;

        while (searchPos < classBody.size())
        {
            const char* macroNames[] = {"MFUNCTION(", "MFUNCTION(Async)", "__MFUNC__(", nullptr};
            size_t macroPos = std::string::npos;
            std::string matchedMacro;

            for (int i = 0; macroNames[i] != nullptr; ++i)
            {
                size_t pos = classBody.find(macroNames[i], searchPos);
                if (pos != std::string::npos && (macroPos == std::string::npos || pos < macroPos))
                {
                    macroPos = pos;
                    matchedMacro = macroNames[i];
                    if (matchedMacro == "MFUNCTION(Async)" || matchedMacro == "__MFUNC__(")
                        matchedMacro = "MFunction";
                    else
                        matchedMacro = matchedMacro.substr(0, matchedMacro.size() - 1);
                }
            }

            if (macroPos == std::string::npos) break;

            const size_t macroOpen = classBody.find('(', macroPos);
            const size_t macroClose = (macroOpen == std::string::npos) ? std::string::npos : FindMatching(classBody, macroOpen, '(', ')');
            if (macroOpen == std::string::npos || macroClose == std::string::npos) break;

            const std::string macroArgs = classBody.substr(macroOpen + 1, macroClose - macroOpen - 1);
            const size_t declStart = macroClose + 1;
            size_t declEnd = classBody.find(';', declStart);

            if (declEnd == std::string::npos) break;

            const std::string declaration = classBody.substr(declStart, declEnd - declStart + 1);
            std::string clean = Trim(declaration);
            if (clean.back() == '}') {
                clean = Trim(clean.substr(0, clean.size() - 1)) + ";";
            }

            const size_t openParen = clean.find('(');
            const size_t closeParen = (openParen == std::string::npos) ? std::string::npos : FindMatching(clean, openParen, '(', ')');
            const size_t semicolon = clean.rfind(';');

            if (openParen != std::string::npos && closeParen != std::string::npos && semicolon != std::string::npos)
            {
                const std::string head = Trim(clean.substr(0, openParen));
                const size_t nameSplit = head.find_last_of(" \t");
                if (nameSplit != std::string::npos)
                {
                    SParsedFunction func;
                    func.MacroArgs = Trim(macroArgs);
                    func.ReturnType = Trim(head.substr(0, nameSplit));
                    func.Name = Trim(head.substr(nameSplit + 1));
                    func.Signature = Trim(clean.substr(openParen, closeParen - openParen + 1));
                    func.ReturnStorageType = func.ReturnType;
                    functions.push_back(func);
                }
            }

            searchPos = declEnd + 1;
        }
        return functions;
    }

    std::string DetermineOwnerFromHeaderPath(const fs::path& headerPath) const
    {
        fs::path relativePath = headerPath;
        std::vector<std::string> parts;
        for (const fs::path& part : relativePath)
        {
            parts.push_back(part.generic_string());
        }
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i] == "Source" && i + 2 < parts.size())
            {
                if (parts[i + 1] == "Servers") return parts[i + 2];
            }
        }
        return "Shared";
    }
};

}  // namespace MHeaderTool
