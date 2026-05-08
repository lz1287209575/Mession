#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"

namespace MHeaderTool
{

// ============================================================================
// Macro Expander - 处理 RPC_LIST 等宏
// ============================================================================

class MacroExpander
{
public:
    MacroExpander() = default;

    // 解析所有 RPC_LIST 宏
    TRpcListMacroMap ParseRpcListMacros(const std::string& contents) const
    {
        TRpcListMacroMap macroMap;
        std::istringstream input(contents);
        std::string physicalLine;
        std::string logicalLine;

        auto flushLogicalLine = [&macroMap, this](const std::string& line)
        {
            const std::string trimmed = Trim(line);
            if (!StartsWith(trimmed, "#define "))
            {
                return;
            }

            size_t cursor = std::string("#define ").size();
            while (cursor < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[cursor])))
            {
                ++cursor;
            }

            const size_t nameStart = cursor;
            while (cursor < trimmed.size() && IsIdentifierChar(trimmed[cursor]))
            {
                ++cursor;
            }

            if (cursor <= nameStart)
            {
                return;
            }

            const std::string macroName = trimmed.substr(nameStart, cursor - nameStart);
            if (macroName.find("_RPC_LIST") == std::string::npos)
            {
                return;
            }

            cursor = SkipWhitespace(trimmed, cursor);
            if (cursor >= trimmed.size() || trimmed[cursor] != '(')
            {
                return;
            }

            const size_t paramsClose = FindMatching(trimmed, cursor, '(', ')');
            if (paramsClose == std::string::npos)
            {
                return;
            }

            const std::string params = Trim(trimmed.substr(cursor + 1, paramsClose - cursor - 1));
            if (params != "OP")
            {
                return;
            }

            const std::string body = Trim(trimmed.substr(paramsClose + 1));
            if (!StartsWith(body, "OP("))
            {
                return;
            }

            const size_t callOpen = body.find('(');
            const size_t callClose = (callOpen == std::string::npos)
                ? std::string::npos
                : FindMatching(body, callOpen, '(', ')');
            if (callClose == std::string::npos)
            {
                return;
            }

            const std::string entryArgs = body.substr(callOpen + 1, callClose - callOpen - 1);
            std::vector<SParsedFunction> functions;
            if (auto parsed = ParseRpcListEntry("MDECLARE_SERVER_HOSTED_RPC_METHOD", entryArgs))
            {
                functions.push_back(std::move(*parsed));
            }

            if (!functions.empty())
            {
                macroMap[macroName] = std::move(functions);
            }
        };

        while (std::getline(input, physicalLine))
        {
            std::string line = physicalLine;
            bool bContinued = false;
            if (!line.empty() && line.back() == '\\')
            {
                line.pop_back();
                bContinued = true;
            }

            logicalLine += line;
            if (bContinued)
            {
                logicalLine.push_back(' ');
                continue;
            }

            flushLogicalLine(logicalLine);
            logicalLine.clear();
        }

        if (!logicalLine.empty())
        {
            flushLogicalLine(logicalLine);
        }

        return macroMap;
    }

    // 解析 RPC_LIST 条目
    std::optional<SParsedFunction> ParseRpcListEntry(
        const std::string& expanderMacro,
        const std::string& entryArgs) const
    {
        return ParseWrappedFunctionMacro(expanderMacro, entryArgs);
    }

    // 解析包装的函数宏
    std::optional<SParsedFunction> ParseWrappedFunctionMacro(
        const std::string& macroName,
        const std::string& macroArgs) const
    {
        const auto parts = SplitTopLevelArgs(macroArgs);

        if (macroName == "MDECLARE_SERVICE_RPC")
        {
            if (parts.size() < 5)
            {
                return std::nullopt;
            }

            SParsedFunction parsed;
            parsed.ReturnType = "void";
            parsed.ReturnStorageType = "void";
            parsed.ReturnPropertyKind = "None";
            parsed.Name = parts[0];
            parsed.Signature = parts[1];
            parsed.MacroArgs = parts[2] + ", Rpc=" + parts[3] + ", Reliable=" + parts[4] + ", Handler=true";
            parsed.bIsRpc = true;
            parsed.RpcKind = parts[3];
            parsed.bReliable = ParseBoolLiteral(parts[4], true);
            parsed.Params = ParseFunctionParameters(parsed.Signature);
            return parsed;
        }

        if (macroName == "MDECLARE_RPC_METHOD" || macroName == "MDECLARE_RPC_METHOD_WITH_HANDLER")
        {
            if (parts.size() < 5)
            {
                return std::nullopt;
            }

            const size_t methodIndex = 0;
            const size_t signatureIndex = (macroName == "MDECLARE_RPC_METHOD") ? 1 : 2;
            const size_t flagsIndex = (macroName == "MDECLARE_RPC_METHOD") ? 2 : 3;
            const size_t rpcIndex = (macroName == "MDECLARE_RPC_METHOD") ? 3 : 4;
            const size_t reliableIndex = (macroName == "MDECLARE_RPC_METHOD") ? 4 : 5;
            if (reliableIndex >= parts.size())
            {
                return std::nullopt;
            }

            SParsedFunction parsed;
            parsed.ReturnType = "void";
            parsed.ReturnStorageType = "void";
            parsed.ReturnPropertyKind = "None";
            parsed.Name = parts[methodIndex];
            parsed.Signature = parts[signatureIndex];
            parsed.MacroArgs = parts[flagsIndex] + ", Rpc=" + parts[rpcIndex] + ", Reliable=" + parts[reliableIndex];
            parsed.bIsRpc = true;
            parsed.RpcKind = parts[rpcIndex];
            parsed.bReliable = ParseBoolLiteral(parts[reliableIndex], true);
            parsed.Params = ParseFunctionParameters(parsed.Signature);
            return parsed;
        }

        if (macroName == "MDECLARE_SERVER_HOSTED_RPC_METHOD")
        {
            if (parts.size() < 7)
            {
                return std::nullopt;
            }

            SParsedFunction parsed;
            parsed.ReturnType = "void";
            parsed.ReturnStorageType = "void";
            parsed.ReturnPropertyKind = "None";
            parsed.Name = parts[2];
            parsed.Signature = parts[3];
            parsed.MacroArgs = parts[4] + ", Rpc=" + parts[5] + ", Reliable=" + parts[6] + ", Endpoint=" + parts[1];
            parsed.bIsRpc = true;
            parsed.RpcKind = parts[5];
            parsed.bReliable = ParseBoolLiteral(parts[6], true);
            parsed.Endpoint = parts[1];
            parsed.Params = ParseFunctionParameters(parsed.Signature);
            return parsed;
        }

        return std::nullopt;
    }

private:
    std::vector<SParsedParameter> ParseFunctionParameters(const std::string& signature) const
    {
        std::vector<SParsedParameter> params;
        const size_t openParen = signature.find('(');
        const size_t closeParen = (openParen == std::string::npos)
            ? std::string::npos
            : FindMatching(signature, openParen, '(', ')');
        if (openParen == std::string::npos || closeParen == std::string::npos)
        {
            return params;
        }

        const std::string paramList = Trim(signature.substr(openParen + 1, closeParen - openParen - 1));
        if (paramList.empty() || paramList == "void")
        {
            return params;
        }

        for (std::string entry : SplitTopLevelArgs(paramList))
        {
            entry = Trim(entry);
            if (entry.empty()) continue;

            const size_t equalsPos = entry.find('=');
            if (equalsPos != std::string::npos)
            {
                entry = Trim(entry.substr(0, equalsPos));
            }

            size_t nameEnd = entry.size();
            while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(entry[nameEnd - 1])))
            {
                --nameEnd;
            }

            size_t nameStart = nameEnd;
            while (nameStart > 0 && IsIdentifierChar(entry[nameStart - 1]))
            {
                --nameStart;
            }

            if (nameStart == nameEnd) continue;

            SParsedParameter parsedParam;
            parsedParam.Name = entry.substr(nameStart, nameEnd - nameStart);
            parsedParam.Type = Trim(entry.substr(0, nameStart));
            parsedParam.StorageType = NormalizeReflectionType(parsedParam.Type);
            parsedParam.PropertyKind = InferPropertyKind(parsedParam.StorageType);
            params.push_back(std::move(parsedParam));
        }

        return params;
    }

    bool ParseBoolLiteral(std::string_view text, bool defaultValue) const
    {
        const std::string value = Trim(text);
        if (value == "true") return true;
        if (value == "false") return false;
        return defaultValue;
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
};

}  // namespace MHeaderTool
