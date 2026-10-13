/**
 * @file LuaStdlibHintEmitter.cpp
 * @brief 从 MLua*.cpp 里的 `/// @lua-*` 注解块生成 Mession.lua 与 Mession.d.tl。
 *
 * @detail 流程全是**文本级**处理(不做 C++ 语法分析):
 *   ExtractBindings → 只扫 <SourceRoot>/Common/Script/Lua 这一层(非递归)的 MLua*.cpp
 *   → 逐文件把连续的 `///` 行攒成一个块 → ParseCommentBlock 认出带 `@lua-stdlib` 的块
 *   → 在块结束后 5 行窗口内按 "lua_State" / "int " 的字符串特征取出 cfunction 名
 *   → 按 namespace 分组 + 排序(`new` 优先)→ 渲染两个文件并覆盖式写入。
 *   产物只服务于 IDE 补全 / 类型检查,运行时不会被 require:真正的 `Mession.*` 注册在
 *   C++ 侧 MLuaXxx::Install 里。
 *
 *   NOTE: 解析对格式高度敏感且失败是"静默少产条目":`///` 块一旦缩进,ParseFile 用 Trim 后的
 *   行判断能收进来,而 ParseCommentBlock 却拿原始行做 rfind("///", 0) 前缀判断,整块会被忽略;
 *   cfunction 名也只在 5 行窗口里找,注解与实现之间插入内容就会漏掉。改注解格式时务必同步改这里。
 */
#include "Generation/LuaStdlibHintEmitter.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace MHeaderTool {

    namespace fs = std::filesystem;

    namespace {

        /**
         * @brief 按 '|' 把类型串拆成分支 token(联合类型 → 多个 Teal 类型)。
         * @param S 形如 "MScalar|nil" / "integer|number|string|boolean" 的串
         * @return 逐段切分结果;空串会得到一个空 token,不做去空
         */
        TVector<MString> SplitPipe(const MString& S) {
            TVector<MString> Out;
            size_t           Start = 0;
            for (size_t i = 0; i <= S.size(); ++i) {
                if (i == S.size() || S[i] == '|') {
                    Out.push_back(S.substr(Start, i - Start));
                    Start = i + 1;
                }
            }
            return Out;
        }

        /**
         * @brief 去掉首尾空白(空格 / 制表 / 换行)。
         * @param S 按值传入,就地裁剪后返回
         * @return 裁剪结果;整串都是空白时返回空串
         */
        MString Trim(MString S) {
            auto Front = S.find_first_not_of(" \t\r\n");
            if (Front == MString::npos)
                return {};
            auto Back = S.find_last_not_of(" \t\r\n");
            return S.substr(Front, Back - Front + 1);
        }

        /**
         * @brief 解析一条 `@lua-param` 的正文:`name type`,名字尾部可带 `?` 表示可选。
         * @param Line 注解正文(未裁剪)
         * @return 解析出的形参;正文为空或只有名字时,未填的字段保持默认值
         *
         * @detail 只按第一个空白切成"名字 / 类型"两段,类型里的空格原样保留(如 `{string}`);
         *         没有空白时整行当名字、类型留空(渲染时回落成 `any`)。
         */
        SLuaParam ParseParamLine(MString Line) {
            SLuaParam P;
            Line = Trim(Line);
            if (Line.empty())
                return P;

            // 名字尾部可带 '?' 表示可选(如 "v? number")
            auto Sp = Line.find_first_of(" \t");
            if (Sp == MString::npos) {
                P.Name = Line;
                return P;
            }
            P.Name = Trim(Line.substr(0, Sp));
            if (!P.Name.empty() && P.Name.back() == '?') {
                P.bOptional = true;
                P.Name.pop_back();
            }
            P.TealType = Trim(Line.substr(Sp + 1));
            return P;
        }

        /**
         * @brief 解析 `@lua-self` 的正文,产出固定名为 `self` 的形参。
         * @param Line 注解正文
         * @return bSelf=true 的形参;正文为 "Type" 这种单 token 时整体当作 TealType
         */
        SLuaParam ParseSelfLine(MString Line) {
            Line = Trim(Line);
            SLuaParam P;
            P.bSelf     = true;
            P.bOptional = false;
            auto Sp     = Line.find_first_of(" \t");
            if (Sp == MString::npos) {
                P.Name     = "self";
                P.TealType = Line;
            } else {
                P.Name     = "self";
                P.TealType = Trim(Line.substr(Sp + 1));
            }
            return P;
        }

        /**
         * @brief 把一个 `///` 注释块解析成一条 stdlib 绑定。
         * @param Block       原始注释行(允许带行首缩进)
         * @param Binding     [out] 就地填充;注意是**追加**语义 —— Params 不会被清空
         * @return true 块里出现了 `@lua-stdlib`(即这是一个 binding);false 表示该块与本工具无关
         *
         * @detail 认的伪指令只有 4 个:`@lua-stdlib <NS>.<Fn>`(必需,点号缺失直接判 false)、
         *         `@lua-param`、`@lua-return`(按 '|' 拆联合类型)、`@lua-self`(插到形参最前面
         *         并把 bIsMethod 置真)。
         *         未写 `@lua-return` 时按"无返回值"处理:补一个 "nil" 且 bIsVoid=true。
         *         NOTE: 行首缩进的 `///` 在这里会被 rfind("///", 0) 判掉,与 ParseFile 收行的
         *         判断标准不一致(那边先 Trim),因此缩进的注解块会被整块忽略。
         */
        bool ParseCommentBlock(const TVector<MString>& Block, SLuaStdlibBinding& Binding) {
            bool bFound = false;
            for (auto& Raw : Block) {
                MString Line = Raw;
                // 剥掉行首 "///" 及其后的可选空格
                if (Line.rfind("///", 0) != 0)
                    continue;
                Line = Line.substr(3);
                Line = Trim(Line);

                if (Line.rfind("@lua-stdlib", 0) == 0) {
                    MString Spec = Trim(Line.substr(std::string("@lua-stdlib").size()));
                    // 正文形如 "Log.info" / "Vector.get" / "RPC.call":点号前是 namespace
                    auto Dot = Spec.find('.');
                    if (Dot == MString::npos)
                        return false;
                    Binding.NamespaceName = Spec.substr(0, Dot);
                    Binding.FunctionName  = Spec.substr(Dot + 1);
                    bFound                = true;
                } else if (Line.rfind("@lua-param", 0) == 0) {
                    MString   Rest = Trim(Line.substr(std::string("@lua-param").size()));
                    SLuaParam P    = ParseParamLine(Rest);
                    if (!P.Name.empty()) {
                        Binding.Params.push_back(P);
                    }
                } else if (Line.rfind("@lua-return", 0) == 0) {
                    MString Rest             = Trim(Line.substr(std::string("@lua-return").size()));
                    Binding.Return.TealTypes = SplitPipe(Rest);
                } else if (Line.rfind("@lua-self", 0) == 0) {
                    MString   Rest = Trim(Line.substr(std::string("@lua-self").size()));
                    SLuaParam P    = ParseSelfLine(Rest);
                    // self 插到 params 最前面
                    Binding.Params.insert(Binding.Params.begin(), P);
                    Binding.bIsMethod = true;
                }
            }
            if (!bFound)
                return false;

            // 没写 @lua-return:按无返回值处理,补一个 nil 并标记 bIsVoid
            if (Binding.Return.TealTypes.empty()) {
                Binding.Return.TealTypes.push_back(MString("nil"));
                Binding.Return.bIsVoid = true;
            } else {
                Binding.Return.bIsVoid = (Binding.Return.TealTypes.size() == 1 && Binding.Return.TealTypes[0] == "nil");
            }
            return true;
        }

        /**
         * @brief 在注释块之后找出 cfunction 名。
         * @param AllLines  整个文件的逐行内容
         * @param AfterLine 注释块结束处的行号(从这一行开始往后找)
         * @return cfunction 名;窗口内找不到返回空串
         *
         * @detail 只看 AfterLine 起 **5 行**窗口:要求该行同时含 "lua_State" 与 "int ",
         *         再取 '(' 之前最后一个标识符,因此 `int CppName(lua_State* L) {` 与
         *         `static int CppName(...)` 都能认。窗口大小是硬编码的经验值 —— 注解与实现
         *         之间插入超过几行内容就会漏检(调用方会打 stderr 但不会失败)。
         */
        MString FindCppNameAfterComment(const TVector<MString>& AllLines, size_t AfterLine) {
            for (size_t i = AfterLine; i < AllLines.size() && i < AfterLine + 5; ++i) {
                const MString& L = AllLines[i];
                if (L.empty())
                    continue;
                // 目标形态:int FuncName(lua_State* L) { ...
                // 或: static int FuncName(lua_State*) {
                if (L.find("lua_State") == MString::npos)
                    continue;
                auto Pos = L.find("int ");
                if (Pos == MString::npos)
                    continue;
                Pos += 4;
                // "static" 前缀可选,跳过它继续找函数名
                while (Pos < L.size() && (L[Pos] == ' ' || L[Pos] == '\t'))
                    Pos++;
                if (Pos + 6 <= L.size() && L.substr(Pos, 6) == "static") {
                    Pos += 6;
                    while (Pos < L.size() && (L[Pos] == ' ' || L[Pos] == '\t'))
                        Pos++;
                }
                auto End = L.find('(', Pos);
                if (End == MString::npos)
                    continue;
                return Trim(L.substr(Pos, End - Pos));
            }
            return {};
        }

        /**
         * @brief 解析单个 MLua*.cpp,把其中合法的绑定追加到 Out。
         * @param Path 待解析文件
         * @param Out  [out] 追加结果(不清理旧内容)
         *
         * @detail 收行规则:Line 经 Trim 后以 `///` 开头即视为注解块的一行(允许缩进、允许
         *         行首有空白的 `///`),遇到第一行非注解行即认为块结束并就地提交。
         *         SourceFile 只写**文件名**(不含目录),SourceLine 是块起始行的 1-based 行号。
         *         块结束但取不到 cfunction 名时不产出条目,只在 stderr 提示 —— 调用方拿到的是
         *         "少一条"而不是错误。
         */
        void ParseFile(const fs::path& Path, TVector<SLuaStdlibBinding>& Out) {
            std::ifstream In(Path);
            if (!In.is_open()) {
                std::fprintf(stderr, "LuaStdlibHintEmitter: cannot open %s\n", Path.c_str());
                return;
            }
            TVector<MString> Lines;
            MString          Line;
            while (std::getline(In, Line)) {
                Lines.push_back(Line);
            }

            TVector<MString> Block;
            size_t           BlockStartLine = 0;
            for (size_t i = 0; i < Lines.size(); ++i) {
                MString& L       = Lines[i];
                MString  Trimmed = Trim(L);
                if (Trimmed.rfind("///", 0) == 0) {
                    if (Block.empty())
                        BlockStartLine = i;
                    Block.push_back(L);
                } else if (!Block.empty()) {
                    // 注释块结束
                    // 找 @lua-stdlib + cfunction name
                    SLuaStdlibBinding B;
                    B.SourceFile = Path.filename().string();
                    B.SourceLine = static_cast<uint32>(BlockStartLine + 1);
                    if (ParseCommentBlock(Block, B)) {
                        B.CppName = FindCppNameAfterComment(Lines, i);
                        if (!B.CppName.empty()) {
                            Out.push_back(B);
                        } else {
                            std::fprintf(stderr, "LuaStdlibHintEmitter: %s:%u: @lua-stdlib %s.%s but no int FuncName(lua_State*) found\n", B.SourceFile.c_str(), B.SourceLine, B.NamespaceName.c_str(), B.FunctionName.c_str());
                        }
                    }
                    Block.clear();
                }
            }
        }

        /**
         * @brief 按 NamespaceName 把绑定分组。
         * @param Bs 绑定列表(按值传入)
         * @return namespace → 绑定列表;返回值是**新 map**,组内顺序沿用输入顺序
         */
        TMap<MString, TVector<SLuaStdlibBinding>> GroupByNamespace(TVector<SLuaStdlibBinding> Bs) {
            TMap<MString, TVector<SLuaStdlibBinding>> G;
            for (auto& B : Bs) {
                G[B.NamespaceName].push_back(B);
            }
            return G;
        }

        /**
         * @brief 组内排序:`new` 永远排最前,其余按函数名字典序。
         * @param Bs [in,out] 就地排序
         *
         * @detail NOTE: 比较器对 `new` 一律返回 true,严格弱序并不成立(两个 `new` 之间也
         *         返回"小于"),只是当前数据里 `new` 不会重复才没出问题。
         */
        void SortBindings(TVector<SLuaStdlibBinding>& Bs) {
            std::sort(Bs.begin(), Bs.end(), [](const SLuaStdlibBinding& A, const SLuaStdlibBinding& B) {
                if (A.FunctionName == "new")
                    return true;
                if (B.FunctionName == "new")
                    return false;
                return A.FunctionName < B.FunctionName;
            });
        }

        /**
         * @brief 取形参的 Teal 类型文本。
         * @return TealType 原文;为空时回落成 "any"
         */
        MString TealTypeOr(const SLuaParam& P) {
            if (P.TealType.empty())
                return "any";
            return P.TealType;
        }

        /**
         * @brief 渲染一行 LuaCATS `--- @param`。
         * @param P 形参
         * @return 以 4 空格缩进、以换行结尾的注解行;P.bSelf 时返回空串(self 不文档化)
         */
        MString RenderLuaParam(const SLuaParam& P) {
            if (P.bSelf)
                return {};
            MString S;
            S += "    --- @param ";
            S += P.Name;
            S += " ";
            S += TealTypeOr(P);
            if (P.bOptional)
                S += " (optional)";
            S += "\n";
            return S;
        }

        /**
         * @brief 渲染 LuaCATS `--- @return` 行。
         * @param R 返回值类型
         * @return 缩进 4 空格的注解行(单个类型直接写,多返回值用 '|' 连成一行);无类型时返回空串
         */
        MString RenderLuaReturn(const SLuaReturnType& R) {
            if (R.TealTypes.empty())
                return {};
            if (R.TealTypes.size() == 1) {
                MString S = "    --- @return ";
                S += R.TealTypes[0].empty() ? "any" : R.TealTypes[0];
                S += "\n";
                return S;
            }
            MString S = "    --- @return ";
            for (size_t i = 0; i < R.TealTypes.size(); ++i) {
                if (i > 0)
                    S += "|";
                S += R.TealTypes[i].empty() ? "any" : R.TealTypes[i];
            }
            S += "\n";
            return S;
        }

        /**
         * @brief namespace → Teal record 名的映射。
         * @param NS Lua 侧 namespace(如 "Vector" / "Log" / "RPC")
         * @return 约定的 record 名;未列入白名单的 namespace 回落成 "M" + NS
         *
         * @detail 白名单是硬编码的(与 C++ 侧 MLuaXxx 的类名对齐),新增 stdlib 时要把这里的
         *         映射一并补上,否则 .d.tl 里会出现与 C++ 不一致的 record 名。
         */
        MString LuaRecordName(const MString& NS) {
            if (NS == "Vector")
                return "MVector";
            if (NS == "Map")
                return "MMap";
            if (NS == "Log")
                return "MLog";
            if (NS == "Format")
                return "MFormat";
            if (NS == "RPC")
                return "MRPC";
            if (NS == "Time")
                return "MTime";
            if (NS == "Id")
                return "MId";
            return "M" + NS;
        }

        /**
         * @brief 渲染 Mession.lua(lua-language-server / LuaCATS 风格)。
         * @param Bs 已排序的绑定列表
         * @return 文件全文
         *
         * @detail 每个 namespace 一个 `--- @class M<NS>` 空表 + 其函数注解;函数体统一写成
         *         `function M<NS>.<Fn>(...) end` 空实现 —— 这份文件只为补全/类型检查存在,
         *         真正的实现在 C++ 侧。最后汇总顶层 `Mession` record。
         */
        MString RenderLua(const TVector<SLuaStdlibBinding>& Bs) {
            auto Groups = GroupByNamespace(Bs);

            // 排序 namespace
            TVector<MString> NSOrder;
            for (auto& Kv : Groups)
                NSOrder.push_back(Kv.first);
            std::sort(NSOrder.begin(), NSOrder.end());

            std::ostringstream Out;
            Out << "--- @meta\n";
            Out << "--\n";
            Out << "-- Auto-generated by MHeaderTool LuaStdlibHintEmitter.\n";
            Out << "-- DO NOT EDIT — 改 MLuaVector/Map/Log/Format/Rpc.cpp 上的 /// @lua-* 注解。\n";
            Out << "--\n";
            Out << "-- 此文件仅供 IDE 补全 / 类型检查用,**运行时不会被 require**。\n";
            Out << "-- 实际 `Mession.*` 全局由 C++ 端 MLuaEngine 启动时通过 MLuaXxx::Install 注册。\n";
            Out << "--\n";
            Out << "-- MScalar = integer | number | string | boolean\n";
            Out << "-- Lua 端整数按 int64,带小数点按 double(参考 MScalarValue::FromLua)。\n\n";

            // 每个 namespace 一个独立 record 块
            for (auto& NS : NSOrder) {
                MString RecName = LuaRecordName(NS);
                Out << "--- @class " << RecName << "\n";
                Out << "local " << RecName << " = {}\n\n";

                // 写 namespace 函数(new 优先 + 其它)
                for (auto& B : Groups[NS]) {
                    // params(self 不文档化)
                    for (auto& P : B.Params) {
                        Out << RenderLuaParam(P);
                    }
                    Out << RenderLuaReturn(B.Return);
                    Out << "function " << RecName << "." << B.FunctionName << "(";
                    bool bFirst = true;
                    if (B.bIsMethod) {
                        Out << "self";
                        bFirst = false;
                    }
                    for (auto& P : B.Params) {
                        if (P.bSelf)
                            continue;
                        if (!bFirst)
                            Out << ", ";
                        Out << P.Name;
                        bFirst = false;
                    }
                    Out << ") end\n\n";
                }
            }

            // 顶层 Mession record
            Out << "--- @class Mession\n";
            for (auto& NS : NSOrder) {
                Out << "--- @field " << NS << " " << LuaRecordName(NS) << "\n";
            }
            Out << "\n";
            Out << "--- @type Mession\n";
            Out << "local Mession = {}\n";
            for (auto& NS : NSOrder) {
                Out << "Mession." << NS << " = " << LuaRecordName(NS) << "\n";
            }
            Out << "return Mession\n";
            return Out.str();
        }

        /**
         * @brief 渲染 Mession.d.tl(Teal record 风格)。
         * @param Bs 已排序的绑定列表
         * @return 文件全文
         *
         * @detail 与方法体形式的 RenderLua 不同,这里用 `local record` + 函数类型签名;
         *         方法调用形式的绑定(bIsMethod)会把 self 显式写成第一个形参。
         */
        MString RenderTeal(const TVector<SLuaStdlibBinding>& Bs) {
            auto             Groups = GroupByNamespace(Bs);
            TVector<MString> NSOrder;
            for (auto& Kv : Groups)
                NSOrder.push_back(Kv.first);
            std::sort(NSOrder.begin(), NSOrder.end());

            std::ostringstream Out;
            Out << "--\n";
            Out << "-- Auto-generated by MHeaderTool LuaStdlibHintEmitter.\n";
            Out << "-- DO NOT EDIT — 改 MLuaVector/Map/Log/Format/Rpc.cpp 上的 /// @lua-* 注解。\n";
            Out << "--\n\n";

            Out << "local record MScalar\n";
            Out << "    type Type = integer | number | string | boolean\n";
            Out << "end\n\n";

            for (auto& NS : NSOrder) {
                MString RecName = LuaRecordName(NS);
                Out << "local record " << RecName << "\n";
                Out << "    userdata\n\n";
                for (auto& B : Groups[NS]) {
                    Out << "    " << B.FunctionName << ": function(";
                    bool bFirst = true;
                    if (B.bIsMethod) {
                        Out << "self: " << RecName;
                        bFirst = false;
                    }
                    for (auto& P : B.Params) {
                        if (P.bSelf)
                            continue;
                        if (!bFirst)
                            Out << ", ";
                        Out << P.Name << ": " << (P.TealType.empty() ? MString("any") : P.TealType);
                        bFirst = false;
                    }
                    Out << "): ";
                    for (size_t i = 0; i < B.Return.TealTypes.size(); ++i) {
                        if (i > 0)
                            Out << ", ";
                        Out << B.Return.TealTypes[i];
                    }
                    if (B.Return.TealTypes.empty())
                        Out << "nil";
                    Out << "\n";
                }
                Out << "end\n\n";
            }

            Out << "local record Mession\n";
            Out << "    userdata\n";
            for (auto& NS : NSOrder) {
                Out << "    " << NS << ": " << LuaRecordName(NS) << "\n";
            }
            Out << "end\n\n";
            Out << "return Mession\n";
            return Out.str();
        }

        /**
         * @brief 覆盖式写文件,必要时补建父目录。
         * @param Path    目标路径
         * @param Content 文件全文
         *
         * @detail 写失败只往 stderr 打一行,不抛异常也不返回状态 —— 调用方无法区分是否真的落盘。
         */
        void WriteFile(const fs::path& Path, const MString& Content) {
            fs::create_directories(Path.parent_path());
            std::ofstream Out(Path);
            Out << Content;
            if (!Out.good()) {
                std::fprintf(stderr, "LuaStdlibHintEmitter: failed to write %s\n", Path.c_str());
            }
        }

    } // namespace

    int LuaStdlibHintEmitter::ExtractBindings(const fs::path& SourceRoot, TVector<SLuaStdlibBinding>& OutBindings) {
        fs::path LuaDir = SourceRoot / "Common" / "Script" / "Lua";
        if (!fs::exists(LuaDir)) {
            std::fprintf(stderr, "LuaStdlibHintEmitter: source dir not found: %s\n", LuaDir.c_str());
            return 0;
        }
        for (auto& Entry : fs::directory_iterator(LuaDir)) {
            if (!Entry.is_regular_file())
                continue;
            if (Entry.path().extension() != ".cpp")
                continue;
            if (Entry.path().filename().string().rfind("MLua", 0) != 0)
                continue;
            ParseFile(Entry.path(), OutBindings);
        }
        for (auto& Kv : GroupByNamespace(OutBindings)) {
            SortBindings(Kv.second);
        }
        return static_cast<int>(OutBindings.size());
    }

    void LuaStdlibHintEmitter::Run(const fs::path& SourceRoot, const fs::path& LuaOutPath, const fs::path& TealOutPath) {
        TVector<SLuaStdlibBinding> Bindings;
        int                        Count = ExtractBindings(SourceRoot, Bindings);
        if (Count == 0) {
            std::fprintf(stderr, "LuaStdlibHintEmitter: no bindings found\n");
            return;
        }

        // 重新分组排序:ExtractBindings 里的 SortBindings 作用在 GroupByNamespace 返回的
        // **临时 map** 上,对 OutBindings 本身毫无影响,所以这里必须自己再排一次。
        // NOTE: ExtractBindings 末尾那次排序是无效代码(迟早要删);在它删掉之前不要依赖它。
        auto                       Groups = GroupByNamespace(Bindings);
        TVector<SLuaStdlibBinding> Sorted;
        TVector<MString>           NSOrder;
        for (auto& Kv : Groups)
            NSOrder.push_back(Kv.first);
        std::sort(NSOrder.begin(), NSOrder.end());
        for (auto& NS : NSOrder) {
            for (auto& B : Groups[NS])
                Sorted.push_back(B);
        }

        MString LuaContent  = RenderLua(Sorted);
        MString TealContent = RenderTeal(Sorted);

        WriteFile(fs::absolute(LuaOutPath), LuaContent);
        WriteFile(fs::absolute(TealOutPath), TealContent);

        std::fprintf(stderr, "LuaStdlibHintEmitter: %d bindings → %s + %s\n", Count, LuaOutPath.c_str(), TealOutPath.c_str());
    }

} // namespace MHeaderTool