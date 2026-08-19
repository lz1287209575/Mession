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

// 把 "MScalar|nil" / "{string}" / "integer|number|string|boolean" 拆成 tokens
TVector<MString> SplitPipe(const MString& S)
{
    TVector<MString> Out;
    size_t Start = 0;
    for (size_t i = 0; i <= S.size(); ++i) {
        if (i == S.size() || S[i] == '|') {
            Out.push_back(S.substr(Start, i - Start));
            Start = i + 1;
        }
    }
    return Out;
}

// Trim 首尾空白
MString Trim(MString S)
{
    auto Front = S.find_first_not_of(" \t\r\n");
    if (Front == MString::npos) return {};
    auto Back = S.find_last_not_of(" \t\r\n");
    return S.substr(Front, Back - Front + 1);
}

// 解析一行 "name type" / "name? type"
SLuaParam ParseParamLine(MString Line)
{
    SLuaParam P;
    Line = Trim(Line);
    if (Line.empty()) return P;

    // optional trailing '?' on name
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

// 解析 @lua-self "Type" — 仅取第一个 token
SLuaParam ParseSelfLine(MString Line)
{
    Line = Trim(Line);
    SLuaParam P;
    P.bSelf = true;
    P.bOptional = false;
    auto Sp = Line.find_first_of(" \t");
    if (Sp == MString::npos) {
        P.Name = "self";
        P.TealType = Line;
    } else {
        P.Name = "self";
        P.TealType = Trim(Line.substr(Sp + 1));
    }
    return P;
}

// 解析注释块(注释行 vector)
// 返回 false 表示没找到 @lua-stdlib,这个块不是 binding
bool ParseCommentBlock(const TVector<MString>& Block,
                       SLuaStdlibBinding& Binding)
{
    bool bFound = false;
    for (auto& Raw : Block) {
        MString Line = Raw;
        // strip leading "///" + optional space
        if (Line.rfind("///", 0) != 0) continue;
        Line = Line.substr(3);
        Line = Trim(Line);

        if (Line.rfind("@lua-stdlib", 0) == 0) {
            MString Spec = Trim(Line.substr(std::string("@lua-stdlib").size()));
            // Spec = "Log.info" / "Vector.get" / "RPC.call"
            auto Dot = Spec.find('.');
            if (Dot == MString::npos) return false;
            Binding.NamespaceName = Spec.substr(0, Dot);
            Binding.FunctionName  = Spec.substr(Dot + 1);
            bFound = true;
        } else if (Line.rfind("@lua-param", 0) == 0) {
            MString Rest = Trim(Line.substr(std::string("@lua-param").size()));
            SLuaParam P = ParseParamLine(Rest);
            if (!P.Name.empty()) {
                Binding.Params.push_back(P);
            }
        } else if (Line.rfind("@lua-return", 0) == 0) {
            MString Rest = Trim(Line.substr(std::string("@lua-return").size()));
            Binding.Return.TealTypes = SplitPipe(Rest);
        } else if (Line.rfind("@lua-self", 0) == 0) {
            MString Rest = Trim(Line.substr(std::string("@lua-self").size()));
            SLuaParam P = ParseSelfLine(Rest);
            // self 插到 params 最前面
            Binding.Params.insert(Binding.Params.begin(), P);
            Binding.bIsMethod = true;
        }
    }
    if (!bFound) return false;

    // default return: nil
    if (Binding.Return.TealTypes.empty()) {
        Binding.Return.TealTypes.push_back(MString("nil"));
        Binding.Return.bIsVoid = true;
    } else {
        Binding.Return.bIsVoid = (Binding.Return.TealTypes.size() == 1
                                  && Binding.Return.TealTypes[0] == "nil");
    }
    return true;
}

// 从文件路径 + 注释块内容推断 cfunction name
// 注释块后面紧跟一行 "int CPPNAME(lua_State* L)" 或 "static int CPPNAME(...)"
MString FindCppNameAfterComment(const TVector<MString>& AllLines, size_t AfterLine)
{
    for (size_t i = AfterLine; i < AllLines.size() && i < AfterLine + 5; ++i) {
        const MString& L = AllLines[i];
        if (L.empty()) continue;
        // match: int FuncName(lua_State* L) { ...
        // 或: static int FuncName(lua_State*) {
        if (L.find("lua_State") == MString::npos) continue;
        auto Pos = L.find("int ");
        if (Pos == MString::npos) continue;
        Pos += 4;
        // skip static
        while (Pos < L.size() && (L[Pos] == ' ' || L[Pos] == '\t')) Pos++;
        if (Pos + 6 <= L.size() && L.substr(Pos, 6) == "static") {
            Pos += 6;
            while (Pos < L.size() && (L[Pos] == ' ' || L[Pos] == '\t')) Pos++;
        }
        auto End = L.find('(', Pos);
        if (End == MString::npos) continue;
        return Trim(L.substr(Pos, End - Pos));
    }
    return {};
}

// 解析单个 MLua*.cpp 文件,append 到 OutBindings
void ParseFile(const fs::path& Path, TVector<SLuaStdlibBinding>& Out)
{
    std::ifstream In(Path);
    if (!In.is_open()) {
        std::fprintf(stderr, "LuaStdlibHintEmitter: cannot open %s\n", Path.c_str());
        return;
    }
    TVector<MString> Lines;
    MString Line;
    while (std::getline(In, Line)) {
        Lines.push_back(Line);
    }

    TVector<MString> Block;
    size_t BlockStartLine = 0;
    for (size_t i = 0; i < Lines.size(); ++i) {
        MString& L = Lines[i];
        MString Trimmed = Trim(L);
        if (Trimmed.rfind("///", 0) == 0) {
            if (Block.empty()) BlockStartLine = i;
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
                    std::fprintf(stderr, "LuaStdlibHintEmitter: %s:%u: @lua-stdlib %s.%s but no int FuncName(lua_State*) found\n",
                                 B.SourceFile.c_str(), B.SourceLine,
                                 B.NamespaceName.c_str(), B.FunctionName.c_str());
                }
            }
            Block.clear();
        }
    }
}

// 按 namespace 分组
TMap<MString, TVector<SLuaStdlibBinding>> GroupByNamespace(TVector<SLuaStdlibBinding> Bs)
{
    TMap<MString, TVector<SLuaStdlibBinding>> G;
    for (auto& B : Bs) {
        G[B.NamespaceName].push_back(B);
    }
    return G;
}

// namespace 内排序:new 优先 + 其它字母序
void SortBindings(TVector<SLuaStdlibBinding>& Bs)
{
    std::sort(Bs.begin(), Bs.end(), [](const SLuaStdlibBinding& A, const SLuaStdlibBinding& B) {
        if (A.FunctionName == "new") return true;
        if (B.FunctionName == "new") return false;
        return A.FunctionName < B.FunctionName;
    });
}

// 渲染 Teal type(单一或联合)
MString TealTypeOr(const SLuaParam& P)
{
    if (P.TealType.empty()) return "any";
    return P.TealType;
}

// 渲染 LuaCATS param(方法 self 不文档化)
MString RenderLuaParam(const SLuaParam& P)
{
    if (P.bSelf) return {};
    MString S;
    S += "    --- @param ";
    S += P.Name;
    S += " ";
    S += TealTypeOr(P);
    if (P.bOptional) S += " (optional)";
    S += "\n";
    return S;
}

// 渲染 LuaCATS return(联合类型合并成单行)
MString RenderLuaReturn(const SLuaReturnType& R)
{
    if (R.TealTypes.empty()) return {};
    if (R.TealTypes.size() == 1) {
        MString S = "    --- @return ";
        S += R.TealTypes[0].empty() ? "any" : R.TealTypes[0];
        S += "\n";
        return S;
    }
    MString S = "    --- @return ";
    for (size_t i = 0; i < R.TealTypes.size(); ++i) {
        if (i > 0) S += "|";
        S += R.TealTypes[i].empty() ? "any" : R.TealTypes[i];
    }
    S += "\n";
    return S;
}

// 在 self 类型 + param 类型之间选择元方法类型(record name)
MString LuaRecordName(const MString& NS)
{
    if (NS == "Vector") return "MVector";
    if (NS == "Map") return "MMap";
    if (NS == "Log") return "MLog";
    if (NS == "Format") return "MFormat";
    if (NS == "RPC") return "MRPC";
    if (NS == "Time") return "MTime";
    if (NS == "Id") return "MId";
    return "M" + NS;
}

// 渲染 Mession.lua(language-server 风格)
MString RenderLua(const TVector<SLuaStdlibBinding>& Bs)
{
    auto Groups = GroupByNamespace(Bs);

    // 排序 namespace
    TVector<MString> NSOrder;
    for (auto& Kv : Groups) NSOrder.push_back(Kv.first);
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
                if (P.bSelf) continue;
                if (!bFirst) Out << ", ";
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

// 渲染 Mession.d.tl(Teal 风格) — 简化版,只声明 namespace + function 签名
MString RenderTeal(const TVector<SLuaStdlibBinding>& Bs)
{
    auto Groups = GroupByNamespace(Bs);
    TVector<MString> NSOrder;
    for (auto& Kv : Groups) NSOrder.push_back(Kv.first);
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
                if (P.bSelf) continue;
                if (!bFirst) Out << ", ";
                Out << P.Name << ": " << (P.TealType.empty() ? MString("any") : P.TealType);
                bFirst = false;
            }
            Out << "): ";
            for (size_t i = 0; i < B.Return.TealTypes.size(); ++i) {
                if (i > 0) Out << ", ";
                Out << B.Return.TealTypes[i];
            }
            if (B.Return.TealTypes.empty()) Out << "nil";
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

void WriteFile(const fs::path& Path, const MString& Content)
{
    fs::create_directories(Path.parent_path());
    std::ofstream Out(Path);
    Out << Content;
    if (!Out.good()) {
        std::fprintf(stderr, "LuaStdlibHintEmitter: failed to write %s\n", Path.c_str());
    }
}

} // namespace

int LuaStdlibHintEmitter::ExtractBindings(const fs::path& SourceRoot,
                                         TVector<SLuaStdlibBinding>& OutBindings)
{
    fs::path LuaDir = SourceRoot / "Common" / "Script" / "Lua";
    if (!fs::exists(LuaDir)) {
        std::fprintf(stderr, "LuaStdlibHintEmitter: source dir not found: %s\n", LuaDir.c_str());
        return 0;
    }
    for (auto& Entry : fs::directory_iterator(LuaDir)) {
        if (!Entry.is_regular_file()) continue;
        if (Entry.path().extension() != ".cpp") continue;
        if (Entry.path().filename().string().rfind("MLua", 0) != 0) continue;
        ParseFile(Entry.path(), OutBindings);
    }
    for (auto& Kv : GroupByNamespace(OutBindings)) {
        SortBindings(Kv.second);
    }
    return static_cast<int>(OutBindings.size());
}

void LuaStdlibHintEmitter::Run(const fs::path& SourceRoot,
                              const fs::path& LuaOutPath,
                              const fs::path& TealOutPath)
{
    TVector<SLuaStdlibBinding> Bindings;
    int Count = ExtractBindings(SourceRoot, Bindings);
    if (Count == 0) {
        std::fprintf(stderr, "LuaStdlibHintEmitter: no bindings found\n");
        return;
    }

    // 重新分组(因为 SortBindings 在临时 map 上修改,Bindings 未重新排序)
    auto Groups = GroupByNamespace(Bindings);
    TVector<SLuaStdlibBinding> Sorted;
    TVector<MString> NSOrder;
    for (auto& Kv : Groups) NSOrder.push_back(Kv.first);
    std::sort(NSOrder.begin(), NSOrder.end());
    for (auto& NS : NSOrder) {
        for (auto& B : Groups[NS]) Sorted.push_back(B);
    }

    MString LuaContent  = RenderLua(Sorted);
    MString TealContent = RenderTeal(Sorted);

    WriteFile(fs::absolute(LuaOutPath),  LuaContent);
    WriteFile(fs::absolute(TealOutPath), TealContent);

    std::fprintf(stderr, "LuaStdlibHintEmitter: %d bindings → %s + %s\n",
                 Count, LuaOutPath.c_str(), TealOutPath.c_str());
}

} // namespace MHeaderTool