#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"
#include "Util/FileUtil.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Header Generator - 生成 .gen.h 文件
// ============================================================================

class HeaderGenerator
{
public:
    explicit HeaderGenerator(const SOptions& options)
        : Options_(options)
    {
    }

    // 生成头文件
    std::string Generate(const SParsedClass& parsedClass) const
    {
        if (parsedClass.Kind == EParsedTypeKind::Enum)
        {
            return GenerateEnumHeader(parsedClass);
        }
        return GenerateClassHeader(parsedClass);
    }

private:
    std::string GenerateClassHeader(const SParsedClass& parsedClass) const
    {
        std::string code;
        code += "#pragma once\n\n";
        code += "#include \"Reflection/MGeneratedHeaderBase.h\"\n";
        code += "#include <type_traits>\n\n";

        // Forward declarations
        for (const auto& prop : parsedClass.Properties)
        {
            if (StartsWith(prop.Type, "TSharedPtr<") ||
                StartsWith(prop.Type, "TWeakPtr<") ||
                StartsWith(prop.Type, "TObjectPtr<"))
            {
                code += "#include \"Reflection/MObjectPtr.h\"\n";
                break;
            }
        }

        code += "\n";

        // Begin namespace
        code += "namespace " + parsedClass.Owner + "\n";
        code += "{\n\n";

        // Generate type registration
        code += "template<>\n";
        code += "struct TReflectedTypeInfo<" + parsedClass.Name + ">\n";
        code += "{\n";
        code += "    static constexpr const char* TypeName = \"" + parsedClass.Name + "\";\n";
        code += "    static constexpr const char* Owner = \"" + parsedClass.Owner + "\";\n";
        code += "    static constexpr const char* ReflectionType = \"" + parsedClass.ReflectionType + "\";\n";
        code += "    static constexpr size_t PropertyCount = " + std::to_string(parsedClass.Properties.size()) + ";\n";
        code += "    static constexpr size_t FunctionCount = " + std::to_string(parsedClass.Functions.size()) + ";\n";
        code += "    static constexpr bool bIsClass = true;\n";
        code += "    static constexpr bool bIsStruct = false;\n";
        code += "    static constexpr bool bIsEnum = false;\n";
        code += "};\n\n";

        // Generate property registration
        if (!parsedClass.Properties.empty())
        {
            code += "template<>\n";
            code += "inline constexpr auto TGetPropertyArray<" + parsedClass.Name + ">()\n";
            code += "{\n";
            code += "    return TPropertyArray<\n";
            code += "        " + parsedClass.Name + ",\n";

            for (size_t i = 0; i < parsedClass.Properties.size(); ++i)
            {
                const auto& prop = parsedClass.Properties[i];
                code += "        MPropertyInfo<" + parsedClass.Name + ", " + prop.Type + ", &" + parsedClass.Name + "::" + prop.Name + ", ";
                code += "\"" + prop.PropertyKind + "\", ";
                code += BuildFlagsExpr(prop) + ">";

                if (i < parsedClass.Properties.size() - 1)
                {
                    code += ",\n";
                }
                else
                {
                    code += "\n";
                }
            }

            code += "    >();\n";
            code += "}\n\n";
        }

        // Generate function registration
        for (const auto& func : parsedClass.Functions)
        {
            if (func.bIsRpc)
            {
                code += GenerateRpcDeclaration(parsedClass, func);
            }
        }

        // End namespace
        code += "}  // namespace " + parsedClass.Owner + "\n";

        return code;
    }

    std::string GenerateEnumHeader(const SParsedClass& parsedClass) const
    {
        std::string code;
        code += "#pragma once\n\n";
        code += "#include \"Reflection/MGeneratedHeaderBase.h\"\n\n";
        code += "namespace " + parsedClass.Owner + "\n";
        code += "{\n\n";

        // Generate type registration for enum
        code += "template<>\n";
        code += "struct TReflectedTypeInfo<" + parsedClass.Name + ">\n";
        code += "{\n";
        code += "    static constexpr const char* TypeName = \"" + parsedClass.Name + "\";\n";
        code += "    static constexpr const char* Owner = \"" + parsedClass.Owner + "\";\n";
        code += "    static constexpr const char* ReflectionType = \"Enum\";\n";
        code += "    static constexpr size_t EnumValueCount = " + std::to_string(parsedClass.EnumValues.size()) + ";\n";
        code += "    static constexpr bool bIsClass = false;\n";
        code += "    static constexpr bool bIsStruct = false;\n";
        code += "    static constexpr bool bIsEnum = true;\n";
        code += "    static constexpr bool bScopedEnum = " + std::string(parsedClass.bScopedEnum ? "true" : "false") + ";\n";
        code += "    static constexpr const char* UnderlyingType = \"" + parsedClass.EnumUnderlyingType + "\";\n";
        code += "};\n\n";

        // Generate enum value table
        if (!parsedClass.EnumValues.empty())
        {
            code += "inline const SEnumValueInfo* Get" + parsedClass.Name + "EnumValues()\n";
            code += "{\n";
            code += "    static const SEnumValueInfo values[] = {\n";
            for (size_t i = 0; i < parsedClass.EnumValues.size(); ++i)
            {
                const std::string& valueName = parsedClass.EnumValues[i];
                code += "        {\"" + valueName + "\", static_cast<int64>(" + valueName + ")}";
                if (i < parsedClass.EnumValues.size() - 1)
                {
                    code += ",";
                }
                code += "\n";
            }
            code += "    };\n";
            code += "    return values;\n";
            code += "}\n\n";
        }

        code += "}  // namespace " + parsedClass.Owner + "\n";

        return code;
    }

    std::string GenerateRpcDeclaration(const SParsedClass& parsedClass, const SParsedFunction& func) const
    {
        std::string code;

        // Generate message type ID
        code += "template<>\n";
        code += "inline constexpr uint32 TGetRpcMessageId<" + parsedClass.Name + ", &" + parsedClass.Name + "::" + func.Name + ">()\n";
        code += "{\n";
        code += "    return static_cast<uint32>(EMessageId::" + func.Name + ");\n";
        code += "}\n\n";

        // Generate client stub if needed
        if (func.Transport == "ServerCall" || func.Transport == "Client")
        {
            code += "template<>\n";
            code += "inline void TInvokeClientRpc<" + parsedClass.Name + ", &" + parsedClass.Name + "::" + func.Name + ">(\n";
            code += "    MObject* Target,\n";

            for (const auto& param : func.Params)
            {
                code += "    " + param.Type + " " + param.Name + ",\n";
            }

            code += ")\n";
            code += "{\n";
            code += "    // TODO: Implement client stub\n";
            code += "}\n\n";
        }

        return code;
    }

    std::string BuildFlagsExpr(const SParsedProperty& prop) const
    {
        // Parse flags from prop.FlagsExpr or macro args
        std::vector<std::string> parts;

        if (prop.MacroArgs.find("PersistentData") != std::string::npos)
        {
            parts.push_back("EPF_PersistentData");
        }
        if (prop.MacroArgs.find("Replicated") != std::string::npos)
        {
            parts.push_back("EPF_Replicated");
        }

        if (parts.empty())
        {
            return "EPF_None";
        }

        std::string expr;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0) expr += " | ";
            expr += parts[i];
        }
        return expr;
    }

    SOptions Options_;
};

}  // namespace MHeaderTool
