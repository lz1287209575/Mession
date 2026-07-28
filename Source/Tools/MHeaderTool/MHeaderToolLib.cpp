#include "Core/Types.h"
#include "Parsing/FunctionParser.h"
#include "Util/StringUtil.h"
#include "Common/Runtime/MLib.h"

#include <cctype>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace MHT = MHeaderTool;

// P4 — spec 2026-07-28 §B: scan every header in `FileContents` for
// namespace-scope `MFUNCTION(Async)` free functions. Reject any free
// function that carries a transport tag (ServerCall/ClientCall/RPC/
// NetServer/NetClient/Client) — those belong on class methods, not on
// free functions. The error message cites the relevant specs so the
// user can navigate to the rule.
//
// Extracted from MHeaderTool.cpp (P4) so the test executable can link
// this without colliding with MHeaderTool.cpp's `main()` symbol.
//
// IMPORTANT: When Task 2 was implemented, the body lived inline in
// MHeaderTool.cpp. Copy the EXACT current implementation from there;
// do not re-edit the logic. The move is a pure relocation.
std::vector<MHT::SFreeAsyncFunc> ProcessFreeFunctions(
    const std::map<fs::path, std::string>& FileContents)
{
    std::vector<MHT::SFreeAsyncFunc> Result;

    // Transport tags that must NOT appear on a free function (spec 2026-07-24 §6.1
    // + 2026-07-28 §B — transport is a class-method concept).
    const TSet<std::string> TransportTags = {
        "ServerCall", "ClientCall", "RPC", "NetServer", "NetClient", "Client"
    };

    const std::string Needle = "MFUNCTION(";

    for (const auto& [HeaderPath, Contents] : FileContents)
    {
        // Skip headers that have no reflection markers — mirrors main()'s
        // `HeaderScanner::HasReflectionMarkers` short-circuit.
        if (Contents.find("MFUNCTION") == std::string::npos)
        {
            continue;
        }

        // v1 simplification (spec §6 risk register): mask only class/struct
        // declaration bodies so namespace blocks remain visible. We tokenize
        // the header and look for `class` or `struct` keywords at token
        // boundaries; on match, we find the next `{` and mask everything
        // between the keyword and the matching `}`. Known limitation:
        // `class`/`struct` literals inside `// ...` or `/* ... */` comments
        // will be treated as keywords (P4 fixture files avoid that).
        std::string Masked = Contents;
        size_t Index = 0;
        int ClassDepth = 0;
        while (Index < Masked.size())
        {
            if (ClassDepth > 0)
            {
                const char Char = Masked[Index];
                if (Char == '{')
                {
                    ++ClassDepth;
                }
                else if (Char == '}')
                {
                    --ClassDepth;
                }
                else
                {
                    Masked[Index] = ' ';
                }
                ++Index;
            }
            else
            {
                const bool AtTokenBoundary = (Index == 0)
                    || std::isspace(static_cast<unsigned char>(Masked[Index - 1]))
                    || Masked[Index - 1] == ';'
                    || Masked[Index - 1] == '{'
                    || Masked[Index - 1] == '}';
                const bool IsClass = AtTokenBoundary
                    && (Masked.compare(Index, 5, "class") == 0)
                    && (Index + 5 < Masked.size())
                    && std::isspace(static_cast<unsigned char>(Masked[Index + 5]));
                const bool IsStruct = AtTokenBoundary
                    && !IsClass
                    && (Masked.compare(Index, 6, "struct") == 0)
                    && (Index + 6 < Masked.size())
                    && std::isspace(static_cast<unsigned char>(Masked[Index + 6]));
                if (IsClass || IsStruct)
                {
                    const size_t KeywordLen = IsClass ? 5 : 6;
                    const size_t BracePos = Masked.find('{', Index + KeywordLen);
                    if (BracePos != std::string::npos)
                    {
                        // Mask keyword + declaration header up to '{' (keep '{' visible
                        // so the inner brace-tracking loop sees it).
                        for (size_t K = Index; K < BracePos; ++K)
                        {
                            Masked[K] = ' ';
                        }
                        ClassDepth = 1;
                        Index = BracePos + 1;
                        continue;
                    }
                }
                ++Index;
            }
        }

        size_t SearchPos = 0;
        while ((SearchPos = Masked.find(Needle, SearchPos)) != std::string::npos)
        {
            const size_t MacroOpen = Contents.find('(', SearchPos);
            const size_t MacroClose = (MacroOpen == std::string::npos)
                ? std::string::npos
                : MHT::FindMatching(Contents, MacroOpen, '(', ')');
            if ((MacroOpen == std::string::npos) || (MacroClose == std::string::npos))
            {
                SearchPos = SearchPos + Needle.size();
                continue;
            }

            const std::string MacroArgs =
                Contents.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);

            // Tokenize MacroArgs on ',' and reject any transport tag with an
            // exact match. Substring matching is wrong (e.g. "ClientCall"
            // would be flagged as carrying "Client" — see F4 review).
            TVector<std::string> Tokens;
            {
                size_t TokenStart = 0;
                for (size_t K = 0; K <= MacroArgs.size(); ++K)
                {
                    if ((K == MacroArgs.size()) || (MacroArgs[K] == ','))
                    {
                        Tokens.push_back(MHT::Trim(MacroArgs.substr(TokenStart, K - TokenStart)));
                        TokenStart = K + 1;
                    }
                }
            }
            for (const std::string& Token : Tokens)
            {
                if (TransportTags.find(Token) != TransportTags.end())
                {
                    throw std::runtime_error(
                        "MHeaderTool: free function at " + HeaderPath.string() +
                        " carries transport tag '" + Token +
                        "' in MFUNCTION — spec 2026-07-24 §6.1 + 2026-07-28 §B "
                        "require transport on class methods only; "
                        "use plain `MFUNCTION(Async)` for free functions");
                }
            }

            // Only handle MFUNCTION(Async) — i.e. exactly one token, equal to
            // "Async". Anything else is irrelevant to free-function codegen.
            if ((Tokens.size() != 1) || (Tokens[0] != "Async"))
            {
                SearchPos = MacroClose + 1;
                continue;
            }

            // Parse the declaration that follows the macro: walk past
            // whitespace + return-type, find the '(' of the signature, then
            // the function name (last identifier before '('), and extract
            // the response type.
            size_t DeclStart = MacroClose + 1;
            while ((DeclStart < Contents.size()) &&
                   std::isspace(static_cast<unsigned char>(Contents[DeclStart])))
            {
                ++DeclStart;
            }
            const size_t SigOpen = Contents.find('(', DeclStart);
            if (SigOpen == std::string::npos)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            const size_t SigClose = MHT::FindMatching(Contents, SigOpen, '(', ')');
            if (SigClose == std::string::npos)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            // Walk backward from SigOpen over the return type to find the
            // identifier that is the function name (last token before '(').
            size_t NameEnd = SigOpen;
            while ((NameEnd > DeclStart) &&
                   std::isspace(static_cast<unsigned char>(Contents[NameEnd - 1])))
            {
                --NameEnd;
            }
            size_t NameStart = NameEnd;
            while ((NameStart > DeclStart) &&
                   (std::isalnum(static_cast<unsigned char>(Contents[NameStart - 1])) ||
                    (Contents[NameStart - 1] == '_')))
            {
                --NameStart;
            }
            if (NameStart == NameEnd)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            const std::string FuncName = Contents.substr(NameStart, NameEnd - NameStart);

            // Return type = everything from DeclStart up to the start of FuncName.
            const std::string ReturnType =
                MHT::Trim(Contents.substr(DeclStart, NameStart - DeclStart));

            // Response type = unwrap SFutureResult<T> to T. For now a local
            // string-stripping version; Task 3 Step 2 introduces a shared
            // helper in CodeGenerator.h that this code can switch to.
            std::string ResponseType = ReturnType;
            const std::string FutureResultPrefix = "SFutureResult<";
            if ((ResponseType.rfind(FutureResultPrefix, 0) == 0) &&
                (!ResponseType.empty()) &&
                (ResponseType.back() == '>'))
            {
                ResponseType = ResponseType.substr(
                    FutureResultPrefix.size(), ResponseType.size() - FutureResultPrefix.size() - 1);
            }

            MHT::SFreeAsyncFunc Func;
            Func.HeaderPath = HeaderPath;
            Func.Name = FuncName;
            Func.ResponseType = MHT::Trim(ResponseType);
            Func.AsyncBody = "";  // free functions are declaration-only in P4
            Result.push_back(std::move(Func));

            SearchPos = MacroClose + 1;
        }
    }

    return Result;
}
