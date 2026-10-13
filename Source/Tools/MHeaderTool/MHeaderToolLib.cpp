/**
 * @file MHeaderToolLib.cpp
 * @brief 供测试链接的库式入口:从源码文本里扫出自由函数 `MFUNCTION(Async)`(P4)。
 *
 * @detail 与 mht 可执行文件分开编译 —— 本文件不定义 main(),所以 FreeAsyncFuncGenTest 能直接
 *         链接 ProcessFreeFunctions,而不会与 MHeaderTool.cpp 的 main() 符号冲突。
 *         实现是**纯文本扫描**,完全不经 Clang;这与 MHeaderTool 的 AST 主路径是两套独立实现,
 *         只服务于 P4 的自由函数判定与其回归测试,不要试图把它并进 AST 管线。
 *         规则来源:自由函数不得携带 transport 标记(2026-07-24 §6.1 + 2026-07-28 P4 规范 §B
 *         —— transport 是类方法的概念),违反时抛 std::runtime_error 而非返回错误码。
 */
#include "Core/Types.h"
#include "Util/StringUtil.h"
#include "Common/Runtime/MLib.h"

#include <cctype>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs  = std::filesystem;
namespace MHT = MHeaderTool;

/**
 * @brief 扫描 FileContents 里的头文件,收集命名空间作用域的 `MFUNCTION(Async)` 自由函数。
 * @param FileContents 头文件路径 → 全文内容(调用方负责读入;只读,不修改)
 * @return 收集到的自由函数列表;顺序 = TMap(std::map)的路径字典序 + 文件内出现顺序
 * @throw std::runtime_error 有自由函数带了 transport 标记;消息含路径、标记名与规范章节
 *
 * @detail 处理流水(每一步都在上游结果上操作):
 *   1. 预筛:整个头文件不含 "MFUNCTION" 字样就直接跳过;
 *   2. 把行注释与块注释的字节全部替换成空格(**保留换行**,行号不漂移),得到一个
 *      "无注释视图" Contents;
 *   3. 在 Contents 的副本上掩掉 class/struct 的声明体 —— 让类内方法级的 MFUNCTION 不参与
 *      自由函数扫描,命名空间块保持可见;
 *   4. 在掩码视图里定位 `MFUNCTION(` 的位置,回到 Contents 上取**括号配平**的参数原文;
 *   5. 参数按 ',' 切成 token 并做整 token 精确匹配:命中 transport 标记即抛异常,只有
 *      "恰好一个 token 且等于 Async" 才继续解析紧随其后的函数声明。
 *   AsyncBody 恒为空串 —— P4 阶段自由函数只处理声明,不携带函数体。
 *
 * @detail NOTE(已知局限,与当前实现一致):
 *   1. 预跳过只认注释,**不处理字符串/字符字面量** —— 字面量里出现的 `MFUNCTION(` 仍会被扫到;
 *   2. class/struct 掩码同样不看字符串字面量:`" class Foo {"` 这类内容会被当成类声明,
 *      ClassDepth 会一直吞到匹配的 '}' 或文件末尾,后续自由函数可能被整体漏掉;
 *   3. 函数名靠"签名括号前最后一个标识符"反推,不解析复杂返回类型(函数指针、宏包裹等)。
 *   真正的解析主路径是 AST(MASTPipeline + MASTReflectionVisitor),以上局限只影响这条文本扫描。
 *
 * @detail 历史:本函数原先内联在 MHeaderTool.cpp 里,抽取到本文件是纯搬移,逻辑未改。
 */
TVector<MHT::SFreeAsyncFunc> ProcessFreeFunctions(const TMap<fs::path, MString>& FileContents) {
    TVector<MHT::SFreeAsyncFunc> Result;

    // 自由函数上**禁止**出现的 transport 标记(2026-07-24 §6.1 + 2026-07-28 P4 规范 §B:
    // transport 是类方法的概念)。注意表格里实际有 7 个 —— 含 CallClient。
    const TSet<MString> TransportTags = {"ServerCall", "ClientCall", "RPC", "NetServer", "NetClient", "Client", "CallClient"};

    const MString Needle = "MFUNCTION(";

    for (const auto& [HeaderPath, OriginalContents] : FileContents) {
        // 预筛:整份头文件连 "MFUNCTION" 字样都没有就整体跳过。
        // NOTE: 原注释称这对应 main() 里 HeaderScanner::HasReflectionMarkers 的短路,但该类型
        // 与字符串解析主路径都已从仓库删除,这里只保留预筛行为本身。
        if (OriginalContents.find("MFUNCTION") == MString::npos) {
            continue;
        }

        // 注释预跳过(P4 wrap 回归修复)。否则注释里提到的 `MFUNCTION(...)` 会被扫描循环
        // 当成真标记,产出一个畸形的 codegen 条目(真实例子:AsyncDemo.h 在块注释里把
        // `MFUNCTION(Async)` 当文档写)。做法是把注释字节换成空格并**保留换行**,这样报出的
        // 行号不会漂移;后面的掩码与扫描都在这个"无注释视图"上操作。
        // 已知局限:不处理字符串/字符字面量 —— "..." 或 '...' 里出现的 `MFUNCTION(` 仍会命中
        // (可接受:现有 fixture 不覆盖这个边界)。
        MString Contents = OriginalContents;
        {
            size_t Index = 0;
            while (Index < Contents.size()) {
                const char Char = Contents[Index];
                const char Next = (Index + 1 < Contents.size()) ? Contents[Index + 1] : '\0';
                if (Char == '/' && Next == '/') {
                    // 行注释:把到 '\n' 之前(不含换行本身)的字节都换成空格
                    Contents[Index]     = ' ';
                    Contents[Index + 1] = ' ';
                    Index += 2;
                    while (Index < Contents.size() && Contents[Index] != '\n') {
                        Contents[Index] = ' ';
                        ++Index;
                    }
                } else if (Char == '/' && Next == '*') {
                    // 块注释:把到 '*/' 为止(含 '*/')的字节都换成空格
                    Contents[Index]     = ' ';
                    Contents[Index + 1] = ' ';
                    Index += 2;
                    while (Index + 1 < Contents.size() && !(Contents[Index] == '*' && Contents[Index + 1] == '/')) {
                        Contents[Index] = ' ';
                        ++Index;
                    }
                    if (Index + 1 < Contents.size()) {
                        Contents[Index]     = ' ';
                        Contents[Index + 1] = ' ';
                        Index += 2;
                    } else {
                        // 块注释没有闭合:一路屏蔽到文件末尾
                        while (Index < Contents.size()) {
                            Contents[Index] = ' ';
                            ++Index;
                        }
                    }
                } else {
                    ++Index;
                }
            }
        }

        // v1 简化(2026-07-28 P4 规范 §6 risk register):只掩掉 class/struct 的声明体,
        // 让命名空间块保持可见。做法是在 token 边界上找 `class` / `struct` 关键字,命中后取
        // 紧随其后的 `{`,把关键字到配对 `}` 之间的字节全部掩掉。
        // 因为上一步已经先中和过注释,注释里的 class/struct 不会再被当关键字(该局限已消解);
        // 但**字符串字面量**仍会被误判:`" class Foo {"` 会让 ClassDepth 一直吞到匹配的 '}'。
        MString Masked     = Contents;
        size_t  Index      = 0;
        int     ClassDepth = 0;
        while (Index < Masked.size()) {
            if (ClassDepth > 0) {
                const char Char = Masked[Index];
                if (Char == '{') {
                    ++ClassDepth;
                } else if (Char == '}') {
                    --ClassDepth;
                } else {
                    Masked[Index] = ' ';
                }
                ++Index;
            } else {
                const bool AtTokenBoundary = (Index == 0) || std::isspace(static_cast<unsigned char>(Masked[Index - 1])) || Masked[Index - 1] == ';' || Masked[Index - 1] == '{' || Masked[Index - 1] == '}';
                const bool IsClass         = AtTokenBoundary && (Masked.compare(Index, 5, "class") == 0) && (Index + 5 < Masked.size()) && std::isspace(static_cast<unsigned char>(Masked[Index + 5]));
                const bool IsStruct        = AtTokenBoundary && !IsClass && (Masked.compare(Index, 6, "struct") == 0) && (Index + 6 < Masked.size()) && std::isspace(static_cast<unsigned char>(Masked[Index + 6]));
                if (IsClass || IsStruct) {
                    const size_t KeywordLen = IsClass ? 5 : 6;
                    const size_t BracePos   = Masked.find('{', Index + KeywordLen);
                    if (BracePos != MString::npos) {
                        // 关键字连同声明头一起掩到 '{' 之前;'{' 本身保留可见,
                        // 好让内层的配对计数循环看到它。
                        for (size_t K = Index; K < BracePos; ++K) {
                            Masked[K] = ' ';
                        }
                        ClassDepth = 1;
                        Index      = BracePos + 1;
                        continue;
                    }
                }
                ++Index;
            }
        }

        size_t SearchPos = 0;
        while ((SearchPos = Masked.find(Needle, SearchPos)) != MString::npos) {
            const size_t MacroOpen  = Contents.find('(', SearchPos);
            const size_t MacroClose = (MacroOpen == MString::npos) ? MString::npos : MHT::FindMatching(Contents, MacroOpen, '(', ')');
            if ((MacroOpen == MString::npos) || (MacroClose == MString::npos)) {
                SearchPos = SearchPos + Needle.size();
                continue;
            }

            const MString MacroArgs = Contents.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);

            // 把宏参数按 ',' 切成 token,再对 transport 标记做**整 token 精确匹配**。
            // 不能用子串匹配:那样 "ClientCall" 会被误判成携带了标记 "Client"(P4 评审 F4 结论)。
            TVector<MString> Tokens;
            {
                size_t TokenStart = 0;
                for (size_t K = 0; K <= MacroArgs.size(); ++K) {
                    if ((K == MacroArgs.size()) || (MacroArgs[K] == ',')) {
                        Tokens.push_back(MHT::Trim(MacroArgs.substr(TokenStart, K - TokenStart)));
                        TokenStart = K + 1;
                    }
                }
            }
            for (const MString& Token : Tokens) {
                if (TransportTags.find(Token) != TransportTags.end()) {
                    throw std::runtime_error("MHeaderTool: free function at " + HeaderPath.string() + " carries transport tag '" + Token +
                                             "' in MFUNCTION — spec 2026-07-24 §6.1 + 2026-07-28 §B "
                                             "require transport on class methods only; "
                                             "use plain `MFUNCTION(Async)` for free functions");
                }
            }

            // 只处理 MFUNCTION(Async):参数恰好一个 token 且等于 "Async"。
            // 其它形态(带标记的、多个参数的)与自由函数代码生成无关,直接跳过。
            if ((Tokens.size() != 1) || (Tokens[0] != "Async")) {
                SearchPos = MacroClose + 1;
                continue;
            }

            // 解析紧跟在宏之后的函数声明:跳过空白与返回类型,找到签名的 '(',
            // 再取 '(' 之前最后一个标识符当函数名,最后从返回类型里剥出响应类型。
            size_t DeclStart = MacroClose + 1;
            while ((DeclStart < Contents.size()) && std::isspace(static_cast<unsigned char>(Contents[DeclStart]))) {
                ++DeclStart;
            }
            const size_t SigOpen = Contents.find('(', DeclStart);
            if (SigOpen == MString::npos) {
                SearchPos = MacroClose + 1;
                continue;
            }
            const size_t SigClose = MHT::FindMatching(Contents, SigOpen, '(', ')');
            if (SigClose == MString::npos) {
                SearchPos = MacroClose + 1;
                continue;
            }
            // 从签名 '(' 往前回退,跳过空白,得到函数名的右边界;再往左吃到非标识符字符,
            // 得到左边界 —— 中间那段就是函数名。
            size_t NameEnd = SigOpen;
            while ((NameEnd > DeclStart) && std::isspace(static_cast<unsigned char>(Contents[NameEnd - 1]))) {
                --NameEnd;
            }
            size_t NameStart = NameEnd;
            while ((NameStart > DeclStart) && (std::isalnum(static_cast<unsigned char>(Contents[NameStart - 1])) || (Contents[NameStart - 1] == '_'))) {
                --NameStart;
            }
            if (NameStart == NameEnd) {
                SearchPos = MacroClose + 1;
                continue;
            }
            const MString FuncName = Contents.substr(NameStart, NameEnd - NameStart);

            // 返回类型 = 从声明起点到函数名起点之间的原文(已 trim)。
            const MString ReturnType = MHT::Trim(Contents.substr(DeclStart, NameStart - DeclStart));

            // 响应类型 = 把 SFutureResult<T> 剥成 T。
            // 这里是本地的字符串剥离实现:CodeGenerator.h 里那个更完整的 ExtractResponseType
            // 会连 MFuture<TResult<X,Y>> 一起剥,但它是 MCodeGenerator 的成员函数(要实例),
            // 而本文件是全局作用域的自由函数扫描,因此保留这份简化实现。
            MString       ResponseType       = ReturnType;
            const MString FutureResultPrefix = "SFutureResult<";
            if ((ResponseType.rfind(FutureResultPrefix, 0) == 0) && (!ResponseType.empty()) && (ResponseType.back() == '>')) {
                ResponseType = ResponseType.substr(FutureResultPrefix.size(), ResponseType.size() - FutureResultPrefix.size() - 1);
            }

            MHT::SFreeAsyncFunc Func;
            Func.HeaderPath   = HeaderPath;
            Func.Name         = FuncName;
            Func.ResponseType = MHT::Trim(ResponseType);
            Func.AsyncBody    = ""; // P4 阶段自由函数只处理声明,不带函数体
            Result.push_back(std::move(Func));

            SearchPos = MacroClose + 1;
        }
    }

    return Result;
}
