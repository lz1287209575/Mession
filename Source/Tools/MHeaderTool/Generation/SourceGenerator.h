#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"
#include "Util/FileUtil.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Source Generator - 生成 .gen.cpp 文件
// ============================================================================

class SourceGenerator
{
public:
    explicit SourceGenerator(const SOptions& options)
        : Options_(options)
    {
    }

    // 生成源文件
    std::string Generate(const SParsedClass& parsedClass) const
    {
        if (parsedClass.Kind == EParsedTypeKind::Enum)
        {
            return GenerateEnumSource(parsedClass);
        }
        return GenerateClassSource(parsedClass);
    }

    // 获取生成文件路径
    fs::path GetOutputPath(const fs::path& headerPath, const std::string& typeName) const
    {
        fs::path outputDir = Options_.OutputDir;
        fs::path relativePath = headerPath.lexically_relative(Options_.SourceRoot);
        outputDir /= relativePath.parent_path();
        outputDir /= (typeName + ".gen.cpp");
        return outputDir;
    }

private:
    std::string GenerateClassSource(const SParsedClass& parsedClass) const
    {
        std::string code;
        code += "#include \"" + GetIncludePath(parsedClass) + "\"\n";
        code += "#include \"Reflection/MReflectionMacros.h\"\n";
        code += "#include \"Reflection/MPropertyRegistry.h\"\n";
        code += "#include \"Reflection/MFunctionRegistry.h\"\n";
        code += "#include <type_traits>\n\n";

        // Namespace
        code += "namespace " + parsedClass.Owner + "\n";
        code += "{\n\n";

        // Property registration
        if (!parsedClass.Properties.empty())
        {
            code += "void Register" + parsedClass.Name + "Properties()\n";
            code += "{\n";

            for (const auto& prop : parsedClass.Properties)
            {
                code += "    MPropertyRegistry::RegisterProperty<\n";
                code += "        " + parsedClass.Name + ",\n";
                code += "        decltype(" + parsedClass.Name + "::" + prop.Name + "),\n";
                code += "        &" + parsedClass.Name + "::" + prop.Name + ">(\n";
                code += "        \"" + prop.Name + "\",\n";
                code += "        \"" + prop.Type + "\",\n";
                code += "        \"" + prop.PropertyKind + "\",\n";
                code += "        " + BuildFlagsExpr(prop) + ");\n\n";
            }

            code += "}\n\n";
        }

        // Function registration
        if (!parsedClass.Functions.empty())
        {
            code += "void Register" + parsedClass.Name + "Functions()\n";
            code += "{\n";

            for (const auto& func : parsedClass.Functions)
            {
                if (func.bIsRpc)
                {
                    code += "    MFunctionRegistry::RegisterRpc<\n";
                    code += "        " + parsedClass.Name + ",\n";
                    code += "        &" + parsedClass.Name + "::" + func.Name + ">(\n";
                    code += "        \"" + func.Name + "\",\n";
                    code += "        \"" + func.RpcKind + "\",\n";
                    code += "        " + std::string(func.bReliable ? "true" : "false") + ");\n\n";
                }
            }

            code += "}\n\n";
        }

        // Static init
        code += "static struct S" + parsedClass.Name + "Registrator {\n";
        code += "    S" + parsedClass.Name + "Registrator() {\n";
        code += "        MTypeRegistry::Register<" + parsedClass.Name + ">();\n";
        if (!parsedClass.Properties.empty())
        {
            code += "        Register" + parsedClass.Name + "Properties();\n";
        }
        if (!parsedClass.Functions.empty())
        {
            code += "        Register" + parsedClass.Name + "Functions();\n";
        }
        code += "    }\n";
        code += "} G" + parsedClass.Name + "Registrator;\n\n";

        // End namespace
        code += "}  // namespace " + parsedClass.Owner + "\n";

        return code;
    }

    std::string GenerateEnumSource(const SParsedClass& parsedClass) const
    {
        std::string code;
        code += "#include \"" + GetIncludePath(parsedClass) + "\"\n";
        code += "#include \"Reflection/MReflectionMacros.h\"\n";
        code += "#include \"Reflection/MEnumRegistry.h\"\n\n";

        // Namespace
        code += "namespace " + parsedClass.Owner + "\n";
        code += "{\n\n";

        // Enum registration
        code += "static struct S" + parsedClass.Name + "EnumRegistrator {\n";
        code += "    S" + parsedClass.Name + "EnumRegistrator() {\n";
        code += "        MEnumRegistry::Register<" + parsedClass.Name + ">(\n";
        code += "            \"" + parsedClass.Name + "\",\n";
        code += "            " + std::to_string(parsedClass.EnumValues.size()) + ",\n";
        code += "            Get" + parsedClass.Name + "EnumValues());\n";
        code += "    }\n";
        code += "} G" + parsedClass.Name + "EnumRegistrator;\n\n";

        // End namespace
        code += "}  // namespace " + parsedClass.Owner + "\n";

        return code;
    }

    std::string GetIncludePath(const SParsedClass& parsedClass) const
    {
        fs::path relativePath = parsedClass.HeaderPath.lexically_relative(Options_.SourceRoot);
        return relativePath.generic_string();
    }

    std::string BuildFlagsExpr(const SParsedProperty& prop) const
    {
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
