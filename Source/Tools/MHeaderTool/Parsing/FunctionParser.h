#pragma once

#include "Core/Types.h"
#include "Util/StringUtil.h"
#include <regex>

namespace MHeaderTool
{

// ============================================================================
// Function Parser - 解析 MFUNCTION 宏
// ============================================================================

class FunctionParser
{
public:
    FunctionParser() = default;

    // 解析类体中的所有函数
    std::vector<SParsedFunction> ParseFunctionsInClassBody(
        const std::string& classBody,
        const TRpcListMacroMap& rpcListMacros) const
    {
        std::vector<SParsedFunction> functions;
        const std::string maskedClassBody = MakeMaskedCopy(classBody);

        const std::vector<std::string> macroNames = {
            "MFUNCTION(",
            "MFUNCTION(Async)",
            "__MFUNC__(",
            "MDECLARE_SERVICE_RPC(",
            "MDECLARE_RPC_METHOD(",
            "MDECLARE_RPC_METHOD_WITH_HANDLER(",
            "MDECLARE_SERVER_HOSTED_RPC_METHOD("
        };

        size_t searchPos = 0;
        while (searchPos < classBody.size())
        {
            size_t macroPos = std::string::npos;
            std::string matchedMacro;

            for (const std::string& candidate : macroNames)
            {
                const size_t candidatePos = classBody.find(candidate, searchPos);
                if (candidatePos == std::string::npos)
                {
                    continue;
                }

                if (macroPos == std::string::npos || candidatePos < macroPos)
                {
                    macroPos = candidatePos;
                    if (candidate == "MFUNCTION(Async)" || candidate == "__MFUNC__(")
                    {
                        matchedMacro = "MFunction";
                    }
                    else
                    {
                        matchedMacro = candidate.substr(0, candidate.size() - 1);
                    }
                }
            }

            if (macroPos == std::string::npos)
            {
                break;
            }

            const size_t macroOpen = classBody.find('(', macroPos);
            const size_t macroClose = (macroOpen == std::string::npos)
                ? std::string::npos
                : FindMatching(classBody, macroOpen, '(', ')');
            if (macroOpen == std::string::npos || macroClose == std::string::npos)
            {
                searchPos = macroPos + 1;
                continue;
            }

            const std::string macroArgs = classBody.substr(macroOpen + 1, macroClose - macroOpen - 1);

            if (matchedMacro == "MFUNCTION" || matchedMacro == "MFunction")
            {
                const size_t declStart = macroClose + 1;
                size_t declEnd = classBody.find(';', declStart);
                bool bInlineBody = false;

                // 检查是否是异步宏
                bool bIsAsyncMacro = (macroArgs.find("Async") != std::string::npos);
                size_t asyncSearchPos = declStart;

                if (bIsAsyncMacro)
                {
                    // TODO(cpp17-p0-followup): MFUTURE prefix support is kept for source
                    // compatibility after the macro was removed (see spec commit 5548558).
                    // Drop this branch and migrate any legacy call sites in a follow-up PR.
                    // 跳过空白查找 MFUTURE(...)
                    while (asyncSearchPos < classBody.size() &&
                           std::isspace(static_cast<unsigned char>(classBody[asyncSearchPos])))
                    {
                        ++asyncSearchPos;
                    }

                    const std::string mfuturePrefix = "MFUTURE(";
                    if (classBody.compare(asyncSearchPos, mfuturePrefix.size(), mfuturePrefix) == 0)
                    {
                        size_t futureOpen = asyncSearchPos + mfuturePrefix.size() - 1;
                        size_t futureClose = FindMatching(classBody, futureOpen, '(', ')');
                        if (futureClose != std::string::npos)
                        {
                            asyncSearchPos = futureClose + 1;
                            while (asyncSearchPos < classBody.size() &&
                                   std::isspace(static_cast<unsigned char>(classBody[asyncSearchPos])))
                            {
                                ++asyncSearchPos;
                            }

                            if (asyncSearchPos < classBody.size() && classBody[asyncSearchPos] == '{')
                            {
                                size_t braceClose = FindMatching(classBody, asyncSearchPos, '{', '}');
                                if (braceClose != std::string::npos)
                                {
                                    declEnd = braceClose + 1;
                                    bInlineBody = true;
                                }
                            }
                        }
                    }
                }

                if (!bInlineBody && declEnd != std::string::npos)
                {
                    // P3 fix: detect inline bodies that contain ';' inside
                    // (e.g. `MakeShared<Foo>();` before the body). The
                    // existing logic only fired when '{' appeared before
                    // the FIRST ';' — but for v1 inline bodies (auto Frame
                    // = MakeShared<...>();) the first ';' is *inside* the
                    // body, so declEnd ended up at that ';' and the body
                    // was excluded. We now check: is there a '{' whose
                    // matching '}' is AFTER the first ';' (i.e. the first
                    // ';' is INSIDE a body — inline body marker)?
                    // Important: keep the bracePos < declEnd guard so a
                    // non-inline function (whose own ';' precedes the
                    // NEXT function's '{') isn't misclassified.
                    size_t bracePos = classBody.find('{', declStart);
                    if (bracePos != std::string::npos && bracePos < declEnd)
                    {
                        size_t braceClose = FindMatching(classBody, bracePos, '{', '}');
                        if (braceClose != std::string::npos && braceClose > declEnd)
                        {
                            // First ';' is INSIDE the body (bracePos..braceClose).
                            // This is the v1 inline-body pattern — use braceClose
                            // as declEnd and mark inline body.
                            declEnd = braceClose;
                            bInlineBody = true;
                        }
                    }
                }

                if (declEnd == std::string::npos)
                {
                    size_t semicolonSearch = macroClose + 1;
                    while (semicolonSearch < classBody.size() && classBody[semicolonSearch] != ';') {
                        ++semicolonSearch;
                    }
                    if (semicolonSearch < classBody.size()) {
                        declEnd = semicolonSearch;
                    } else {
                        searchPos = macroClose + 1;
                        continue;
                    }
                }

                const std::string declaration = classBody.substr(declStart, declEnd - declStart + 1);
                if (declaration.find("MethodName") != std::string::npos ||
                    declaration.find("Signature") != std::string::npos)
                {
                    searchPos = declEnd + 1;
                    continue;
                }

                std::string declForParse = declaration;
                if (bInlineBody)
                {
                    declForParse = Trim(declaration);
                    if (!declForParse.empty() && declForParse.back() == '}')
                    {
                        declForParse.pop_back();
                        declForParse = Trim(declForParse);
                        declForParse += ";";
                    }
                }

                auto parsed = ParseFunctionDeclaration(macroArgs, declForParse);
                if (parsed)
                {
                    ApplyFunctionMetadataFromMacroArgs(*parsed);
                    const std::string validateNeedle = parsed->Name + "_Validate(";
                    parsed->bHasValidate = (maskedClassBody.find(validateNeedle) != std::string::npos);

                    if (parsed->bIsAsync && bInlineBody)
                    {
                        size_t braceOpen = declaration.find('{');
                        if (braceOpen != std::string::npos)
                        {
                            size_t braceClose = FindMatching(declaration, braceOpen, '{', '}');
                            if (braceClose != std::string::npos)
                            {
                                parsed->AsyncBody = declaration.substr(braceOpen + 1, braceClose - braceOpen - 1);
                            }
                        }
                    }

                    functions.push_back(std::move(*parsed));
                }

                searchPos = declEnd + 1;
                continue;
            }

            if (auto parsed = ParseWrappedFunctionMacro(matchedMacro, macroArgs))
            {
                functions.push_back(std::move(*parsed));
            }

            searchPos = macroClose + 1;
        }

        // 处理 RPC_LIST 宏
        for (const auto& [listMacroName, expandedFunctions] : rpcListMacros)
        {
            size_t macroPos = 0;
            const std::string invocation = listMacroName + "(";
            while (true)
            {
                macroPos = classBody.find(invocation, macroPos);
                if (macroPos == std::string::npos)
                {
                    break;
                }

                const size_t openPos = classBody.find('(', macroPos);
                const size_t closePos = (openPos == std::string::npos)
                    ? std::string::npos
                    : FindMatching(classBody, openPos, '(', ')');
                if (closePos == std::string::npos)
                {
                    break;
                }

                const std::string expanderArg = Trim(classBody.substr(openPos + 1, closePos - openPos - 1));
                if (expanderArg == "MDECLARE_SERVER_HOSTED_RPC_METHOD")
                {
                    functions.insert(functions.end(), expandedFunctions.begin(), expandedFunctions.end());
                }

                macroPos = closePos + 1;
            }
        }

        return functions;
    }

    // 解析函数声明
    std::optional<SParsedFunction> ParseFunctionDeclaration(
        const std::string& macroArgs,
        const std::string& declaration) const
    {
        const std::string clean = Trim(declaration);

        if (clean.empty()) {
            return std::nullopt;
        }

        const size_t openParen = clean.find('(');
        const size_t semicolon = clean.rfind(';');


        if (openParen == std::string::npos || semicolon == std::string::npos)
        {
            return std::nullopt;
        }

        size_t closeParen = FindMatching(clean, openParen, '(', ')');

        if (closeParen == std::string::npos)
        {
            return std::nullopt;
        }

        const std::string head = Trim(clean.substr(0, openParen));
        const size_t nameSplit = head.find_last_of(" \t");
        if (nameSplit == std::string::npos)
        {
            return std::nullopt;
        }

        SParsedFunction parsed;
        parsed.MacroArgs = Trim(macroArgs);
        parsed.ReturnType = Trim(head.substr(0, nameSplit));
        parsed.Name = Trim(head.substr(nameSplit + 1));
        parsed.Signature = Trim(clean.substr(openParen, closeParen - openParen + 1));
        parsed.ReturnStorageType = NormalizeReflectionType(parsed.ReturnType);
        parsed.ReturnPropertyKind = (parsed.ReturnStorageType == "void")
            ? "None"
            : InferPropertyKind(parsed.ReturnStorageType);
        parsed.Params = ParseFunctionParameters(parsed.Signature);

        const std::string tail = Trim(clean.substr(closeParen + 1, semicolon - closeParen - 1));
        parsed.bConst = (tail == "const");
        return parsed;
    }

    // 解析函数参数
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
            if (entry.empty())
            {
                continue;
            }

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

            if (nameStart == nameEnd)
            {
                continue;
            }

            SParsedParameter parsedParam;
            parsedParam.Name = entry.substr(nameStart, nameEnd - nameStart);
            parsedParam.Type = Trim(entry.substr(0, nameStart));
            parsedParam.StorageType = NormalizeReflectionType(parsedParam.Type);
            parsedParam.PropertyKind = InferPropertyKind(parsedParam.StorageType);
            params.push_back(std::move(parsedParam));
        }

        return params;
    }

    // 应用宏参数中的元数据
    void ApplyFunctionMetadataFromMacroArgs(SParsedFunction& parsed) const
    {
        if (auto owner = ExtractMacroValue(parsed.MacroArgs, "Owner"))
        {
            parsed.Owner = *owner;
        }

        const auto parts = SplitTopLevelArgs(parsed.MacroArgs);
        for (const std::string& part : parts)
        {
            if (part.empty())
            {
                continue;
            }

            if (part.find('=') == std::string::npos)
            {
                if (part == "NetServer" || part == "NetClient")
                {
                    parsed.bIsRpc = true;
                    parsed.Transport = part;
                }
                else if (part == "Client")
                {
                    parsed.Transport = "Client";
                }
                else if (part == "ClientCall")
                {
                    parsed.Transport = "ClientCall";
                    parsed.bIsRpc = true;
                }
                else if (part == "ServerCall")
                {
                    parsed.Transport = "ServerCall";
                    parsed.bIsRpc = true;
                }
                else if (part == "RPC")
                {
                    parsed.bIsRpc = true;
                }
                else if (part == "Async")
                {
                    parsed.bIsAsync = true;
                }
                else if (part == "PlayerRPC")
                {
                    parsed.bIsPlayerRpc = true;
                    parsed.bIsAsync = true;
                    parsed.Transport = "ServerCall";
                    parsed.bIsRpc = true;
                }
                continue;
            }

            const size_t equalsPos = part.find('=');
            const std::string key = Trim(part.substr(0, equalsPos));
            const std::string value = Trim(part.substr(equalsPos + 1));

            if (key == "Rpc")
            {
                parsed.bIsRpc = true;
                parsed.RpcKind = value;
            }
            else if (key == "Reliable")
            {
                parsed.bReliable = ParseBoolLiteral(value, true);
            }
            else if (key == "Endpoint")
            {
                parsed.Endpoint = value;
                parsed.bIsRpc = true;
            }
            else if (key == "Message")
            {
                parsed.MessageName = value;
                if (parsed.Transport.empty())
                {
                    parsed.Transport = "Client";
                }
            }
            else if (key == "Channel")
            {
                parsed.Transport = value;
            }
            else if (key == "Route")
            {
                parsed.Route = value;
            }
            else if (key == "Auth")
            {
                parsed.Auth = value;
            }
            else if (key == "Target")
            {
                parsed.Target = value;
            }
            else if (key == "Wrap")
            {
                parsed.Wrap = value;
            }
            else if (key == "Api" || key == "ClientApi")
            {
                parsed.ClientApi = value;
            }
            else if (key == "ParaMeta")
            {
                std::string inner = value;
                if (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')')
                {
                    inner = inner.substr(1, inner.size() - 2);
                }
                static const std::regex pairRegex(R"(([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*))");
                for (auto it = std::sregex_iterator(inner.begin(), inner.end(), pairRegex);
                     it != std::sregex_iterator(); ++it)
                {
                    parsed.ValidationRules.push_back({it->str(1), it->str(2)});
                }
            }
            else if (key == "Dependencies")
            {
                std::string inner = value;
                if (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')')
                {
                    inner = inner.substr(1, inner.size() - 2);
                }
                std::stringstream ss(inner);
                std::string token;
                while (std::getline(ss, token, ','))
                {
                    token = Trim(token);
                    if (!token.empty())
                    {
                        parsed.DependencyList.push_back(token);
                    }
                }
            }
        }
    }

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
