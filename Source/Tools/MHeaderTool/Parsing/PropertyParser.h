#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"

namespace MHeaderTool
{

// ============================================================================
// Property Parser - 解析 MPROPERTY 宏
// ============================================================================

class PropertyParser
{
public:
    PropertyParser() = default;

    // 解析类型体中的所有属性
    std::vector<SParsedProperty> ParsePropertiesInTypeBody(const std::string& typeBody) const
    {
        std::vector<SParsedProperty> properties;
        size_t searchPos = 0;

        while (true)
        {
            const size_t macroPos = typeBody.find("MPROPERTY(", searchPos);
            if (macroPos == std::string::npos)
            {
                break;
            }

            const size_t macroOpen = typeBody.find('(', macroPos);
            const size_t macroClose = (macroOpen == std::string::npos)
                ? std::string::npos
                : FindMatching(typeBody, macroOpen, '(', ')');
            if (macroOpen == std::string::npos || macroClose == std::string::npos)
            {
                break;
            }

            const std::string macroArgs = typeBody.substr(macroOpen + 1, macroClose - macroOpen - 1);
            const size_t declStart = macroClose + 1;
            const size_t declEnd = typeBody.find(';', declStart);
            if (declEnd == std::string::npos)
            {
                break;
            }

            const std::string declaration = typeBody.substr(declStart, declEnd - declStart + 1);
            if (auto parsed = ParsePropertyDeclaration(macroArgs, declaration))
            {
                parsed->PropertyKind = InferPropertyKind(parsed->Type);
                parsed->FlagsExpr = BuildPropertyFlagsExpr(parsed->MacroArgs);
                properties.push_back(std::move(*parsed));
            }

            searchPos = declEnd + 1;
        }

        return properties;
    }

    // 解析单个属性声明
    std::optional<SParsedProperty> ParsePropertyDeclaration(
        const std::string& macroArgs,
        const std::string& declaration) const
    {
        std::string clean = Trim(declaration);
        if (clean.empty())
        {
            return std::nullopt;
        }

        const size_t semicolon = clean.rfind(';');
        if (semicolon == std::string::npos)
        {
            return std::nullopt;
        }
        clean = Trim(clean.substr(0, semicolon));

        const size_t equalsPos = clean.find('=');
        const std::string left = Trim(clean.substr(0, equalsPos));
        if (left.empty() || left.find('(') != std::string::npos)
        {
            return std::nullopt;
        }

        size_t nameEnd = left.size();
        while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(left[nameEnd - 1])))
        {
            --nameEnd;
        }

        size_t nameStart = nameEnd;
        while (nameStart > 0 && IsIdentifierChar(left[nameStart - 1]))
        {
            --nameStart;
        }

        if (nameStart == nameEnd)
        {
            return std::nullopt;
        }

        SParsedProperty parsed;
        parsed.MacroArgs = Trim(macroArgs);
        parsed.Name = left.substr(nameStart, nameEnd - nameStart);
        parsed.Type = Trim(left.substr(0, nameStart));
        if (auto owner = ExtractMacroValue(parsed.MacroArgs, "Owner"))
        {
            parsed.Owner = *owner;
        }
        parsed.Metadata = ParsePropertyMetadataEntries(parsed.MacroArgs);
        return parsed;
    }

    // 解析属性元数据
    std::vector<SMetadataEntry> ParsePropertyMetadataEntries(const std::string& macroArgs) const
    {
        auto metaValue = ExtractMacroValue(macroArgs, "Meta");
        if (!metaValue.has_value())
        {
            return {};
        }

        const std::string inner = StripEnclosingPair(*metaValue, '(', ')');
        std::vector<SMetadataEntry> entries;

        for (const std::string& part : SplitTopLevelArgs(inner))
        {
            if (part.empty())
            {
                continue;
            }

            const size_t equalsPos = part.find('=');
            SMetadataEntry entry;
            if (equalsPos == std::string::npos)
            {
                entry.Key = Trim(part);
                entry.Value = "true";
            }
            else
            {
                entry.Key = Trim(part.substr(0, equalsPos));
                entry.Value = UnquoteStringLiteral(Trim(part.substr(equalsPos + 1)));
            }

            if (!entry.Key.empty())
            {
                entries.push_back(std::move(entry));
            }
        }

        return entries;
    }

    // 推断属性类型
    std::string InferPropertyKind(const std::string& typeName) const
    {
        const std::string compact = ReplaceAll(Trim(typeName), " ", "");
        if (compact == "int8") return "Int8";
        if (compact == "int16") return "Int16";
        if (compact == "int32") return "Int32";
        if (compact == "int64") return "Int64";
        if (compact == "uint8") return "UInt8";
        if (compact == "uint16") return "UInt16";
        if (compact == "uint32") return "UInt32";
        if (compact == "uint64") return "UInt64";
        if (compact == "float") return "Float";
        if (compact == "double") return "Double";
        if (compact == "bool") return "Bool";
        if (compact == "MString") return "String";
        if (compact == "MName") return "Name";
        if (compact == "SVector") return "Vector";
        if (compact == "SRotator") return "Rotator";
        if (StartsWith(compact, "TVector<") || StartsWith(compact, "TByteArray<") ||
            StartsWith(compact, "TMap<") || StartsWith(compact, "TSet<"))
        {
            return "Array";
        }
        return "Struct";
    }

    std::string BuildPropertyFlagsExpr(const std::string& macroArgs) const
    {
        const auto args = SplitTopLevelArgs(macroArgs);
        std::vector<std::string> parts;
        for (const std::string& arg : args)
        {
            if (arg.empty() || arg.find('=') != std::string::npos)
            {
                continue;
            }

            const auto tokens = SplitTopLevelPipes(arg);
            for (const std::string& token : tokens)
            {
                if (!token.empty())
                {
                    parts.push_back("static_cast<uint64>(EPropertyFlags::" + token + ")");
                }
            }
        }

        if (parts.empty())
        {
            return "EPropertyFlags::None";
        }

        std::string expr = "static_cast<EPropertyFlags>(";
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0) expr += " | ";
            expr += parts[i];
        }
        expr += ")";
        return expr;
    }

private:
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
};

}  // namespace MHeaderTool
