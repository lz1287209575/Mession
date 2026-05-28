#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"
#include "Parsing/FunctionParser.h"
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
    std::vector<std::string> ParentClasses;  // 所有基类
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

            // 找到类体开始（跳过继承声明和模板参数）
            size_t bracePos = cursor;
            int angleDepth = 0;
            bool foundColon = false;
            while (bracePos < masked.size() && masked[bracePos] != '{' && masked[bracePos] != ';')
            {
                if (masked[bracePos] == '<') ++angleDepth;
                else if (masked[bracePos] == '>')
                {
                    // 跳过 >> 这种情况
                    if (angleDepth > 0) --angleDepth;
                }
                // 检查是否有冒号（继承开始）
                if (!foundColon && masked[bracePos] == ':')
                {
                    foundColon = true;
                }
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

            // 解析基类列表
            std::vector<std::string> parentClasses;
            if (foundColon)
            {
                // 继承声明在冒号之后，类体之前
                size_t inheritStart = bestPos + keyword.size();
                // 找到继承声明的范围
                size_t inheritEnd = bracePos;
                std::string inheritStr = Trim(masked.substr(inheritStart, inheritEnd - inheritStart));
                // 移除冒号开头的部分并解析基类
                // 检查是否有冒号（可能在开头或空格后）
                size_t colonPos = inheritStr.find(':');
                if (colonPos != std::string::npos)
                {
                    inheritStr = Trim(inheritStr.substr(colonPos + 1));
                    // 分割多个基类
                    size_t pos = 0;
                    while (pos < inheritStr.size())
                    {
                        // 跳过 public/protected/private
                        while (pos < inheritStr.size() && std::isspace(static_cast<unsigned char>(inheritStr[pos])))
                            ++pos;
                        size_t keywordEnd = pos;
                        while (keywordEnd < inheritStr.size() && std::isalpha(static_cast<unsigned char>(inheritStr[keywordEnd])))
                            ++keywordEnd;
                        if (keywordEnd > pos)
                        {
                            std::string keyword = inheritStr.substr(pos, keywordEnd - pos);
                            if (keyword != "public" && keyword != "protected" && keyword != "private")
                            {
                                break;  // 不是访问说明符
                            }
                            pos = keywordEnd;
                            while (pos < inheritStr.size() && std::isspace(static_cast<unsigned char>(inheritStr[pos])))
                                ++pos;
                        }

                        // 提取基类名
                        size_t classStart = pos;
                        int depth = 0;
                        while (pos < inheritStr.size())
                        {
                            char c = inheritStr[pos];
                            if (c == '<')
                            {
                                ++depth;
                            }
                            else if (c == '>')
                            {
                                --depth;
                            }
                            else if (c == ',' && depth == 0)
                            {
                                break;
                            }
                            ++pos;
                        }
                        std::string parentClass = Trim(inheritStr.substr(classStart, pos - classStart));
                        if (!parentClass.empty())
                        {
                            // 移除模板参数中的逗号，只保留基类名
                            size_t firstComma = parentClass.find(',');
                            if (firstComma != std::string::npos)
                            {
                                parentClass = Trim(parentClass.substr(0, firstComma));
                            }
                            // 移除模板参数
                            size_t anglePos = parentClass.find('<');
                            if (anglePos != std::string::npos)
                            {
                                parentClass = Trim(parentClass.substr(0, anglePos));
                            }
                            // 移除命名空间前缀中的冒号
                            size_t lastColon = parentClass.rfind(':');
                            size_t lastSpace = parentClass.rfind(' ');
                            size_t actualStart = (lastColon != std::string::npos && lastColon > lastSpace)
                                ? (lastColon + 1) : 0;
                            if (actualStart > 0)
                            {
                                parentClass = parentClass.substr(actualStart);
                            }
                            if (!parentClass.empty())
                            {
                                parentClasses.push_back(parentClass);
                            }
                        }
                        if (pos < inheritStr.size() && inheritStr[pos] == ',')
                            ++pos;
                    }
                }
            }

            regions.push_back({keyword, *className, bracePos, closeBrace, parentClasses});
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

        if (region.Name == "MWorldServer")
        {
        }

        SParsedClass parsed;
        parsed.Kind = (region.Keyword == "struct") ? EParsedTypeKind::Struct : EParsedTypeKind::Class;
        parsed.Name = region.Name;
        parsed.HeaderPath = headerPath;
        parsed.AllParentClasses = region.ParentClasses;

        // 检查是否有 MGENERATED_BODY
        const bool hasGeneratedBody = classBody.find("MGENERATED_BODY(") != std::string::npos;

        // 检查是否有 MCLASS 标记（有 MCLASS 标记的类也需要解析，用于递归继承检查）
        const bool hasClassMarker = HasNearbyTypeMarker(contents, region, parsed.Kind);

        // 跳过没有 MGENERATED_BODY 且没有 MCLASS 标记的类（除非有 MFUNCTION）
        if (parsed.Kind == EParsedTypeKind::Class && !hasGeneratedBody && !hasClassMarker)
        {
            // 检查是否有 MFUNCTION
            if (classBody.find("MFUNCTION(") == std::string::npos &&
                classBody.find("MFUNCTION(Async)") == std::string::npos &&
                classBody.find("MDECLARE_SERVICE_RPC") == std::string::npos)
            {
                return std::nullopt;
            }
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

        // 解析函数
        FunctionParser funcParser;
        parsed.Functions = funcParser.ParseFunctionsInClassBody(classBody, {});

        // 解析父类信息
        if (hasGeneratedBody)
        {
            ParseGeneratedBodyMetadata(classBody, parsed);
        }

        // 解析类型别名
        parsed.TypeAliases = ParseTypeAliasesInBody(classBody);

        // 解析属性
        parsed.Properties = ParsePropertiesInTypeBody(classBody);

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
