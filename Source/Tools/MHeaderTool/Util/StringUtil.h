/**
 * @file StringUtil.h
 * @brief MHeaderTool 的字符串辅助:裁剪、切分、遮蔽注释/字面量、宏参数提取。
 *
 * @detail
 *   - 全部 inline,无状态,主要用于源码文本的**轻量**处理(不替代编译器词法分析)。
 *   - 若干函数(FindMatching / SplitTopLevel* / MakeMaskedCopy)只做括号配平与引号切换式的
 *     启发式扫描,遇到 `\\"`、代字符号、raw string 等复杂写法可能误判 —— 因此提供了
 *     MakeMaskedCopy 供调用方在切分前先把注释与字面量抹掉。
 *   - NOTE: 本头文件未 include "Common/Runtime/MLib.h",却使用了 MString/TVector/TOptional;
 *     依赖包含者先引入 MLib.h(它不是自包含头文件)。
 *   - 所有函数按值返回新字符串;接收 std::string_view 的版本不分配,除非另有说明。
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace MHeaderTool {

    // ============================================================================
    // 字符串视图辅助
    // ============================================================================

    /**
     * @brief 去掉首尾空白(空白判定用 std::isspace)。
     * @param sv 输入视图
     * @return 去空白后的子视图,仍指向原缓冲区(零拷贝)
     */
    inline std::string_view TrimView(std::string_view sv) {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
            sv.remove_prefix(1);
        }
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
            sv.remove_suffix(1);
        }
        return sv;
    }

    /**
     * @brief 只去首部空白。
     * @param sv 输入视图
     * @return 去首空白后的子视图(零拷贝)
     */
    inline std::string_view TrimStartView(std::string_view sv) {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
            sv.remove_prefix(1);
        }
        return sv;
    }

    // ============================================================================
    // 字符串转换(尽量避免额外分配)
    // ============================================================================

    /**
     * @brief 视图 → 拥有型字符串(这里必然发生一次分配)。
     * @param sv 输入视图
     * @return 字符串副本
     */
    inline MString ToString(std::string_view sv) {
        return MString(sv);
    }

    /**
     * @brief 判断前缀。
     * @param text   待检查文本
     * @param prefix 前缀
     * @return text 以 prefix 开头时为 true(prefix 为空时恒为 true)
     */
    inline bool StartsWith(std::string_view text, std::string_view prefix) {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    /**
     * @brief 判断后缀。
     * @param text   待检查文本
     * @param suffix 后缀
     * @return text 以 suffix 结尾时为 true(suffix 为空时恒为 true)
     */
    inline bool EndsWith(std::string_view text, std::string_view suffix) {
        return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
    }

    // ============================================================================
    // 字符分类
    // ============================================================================

    /**
     * @brief 是否为 C++ 标识符字符(字母/数字/下划线)。
     * @param ch 待判定字符
     * @return true 是标识符可包含的字符
     *
     * @detail 先转 unsigned char 再交给 std::isalnum,避免负数入参的 UB;
     *         非 ASCII(UTF-8 中文标识符)一律判否。
     */
    inline bool IsIdentifierChar(char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    }

    /**
     * @brief 是否为空白字符。
     * @param ch 待判定字符
     * @return true 是空白(同 std::isspace)
     */
    inline bool IsWhitespace(char ch) {
        return std::isspace(static_cast<unsigned char>(ch));
    }

    // ============================================================================
    // 字符串操作
    // ============================================================================

    /**
     * @brief TrimView 的拥有型版本。
     * @param text 输入文本
     * @return 去首尾空白后的新字符串
     */
    inline MString Trim(std::string_view text) {
        return ToString(TrimView(text));
    }

    /**
     * @brief 就地替换全部出现(from 为空则原样返回,避免死循环)。
     * @param text 待处理字符串(按值传入,函数内移动复用)
     * @param from 待替换片段
     * @param to   替换为
     * @return 替换后的字符串
     *
     * @detail 从左到右顺序替换,不会对替换结果再次匹配(不递归替换)。
     */
    inline MString ReplaceAll(MString text, std::string_view from, std::string_view to) {
        if (from.empty()) {
            return text;
        }

        size_t pos = 0;
        while ((pos = text.find(from.data(), pos, from.size())) != MString::npos) {
            text.replace(pos, from.size(), to.data(), to.size());
            pos += to.size();
        }
        return text;
    }

    /**
     * @brief 把任意文本压成合法标识符:非标识符字符统一替换为 '_'。
     * @param text 输入文本
     * @return 等长(逐字符 1:1 替换)的标识符化字符串;空输入返回空串
     *
     * @detail 不处理首字符为数字的情况(结果可能不是合法标识符),也不去重;仅用于生成变量/结构
     *         体名这类"尽量安全"的场景。
     */
    inline MString SanitizeIdentifier(std::string_view text) {
        MString result;
        result.reserve(text.size());
        for (char ch : text) {
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
                result.push_back(ch);
            } else {
                result.push_back('_');
            }
        }
        return result;
    }

    /**
     * @brief 转义成可嵌入 C++ 字符串字面量的形式(反斜杠与双引号)。
     * @param value 原始文本(按值传入)
     * @return 转义后的文本,不含外层引号
     *
     * @detail 先转反斜杠再转引号 —— 顺序不可交换,否则会把新加的转义符再转一遍。
     *         换行等控制字符不处理(与 EscapeJsonString 不同)。
     */
    inline MString EscapeCppStringLiteral(MString value) {
        value = ReplaceAll(std::move(value), "\\", "\\\\");
        value = ReplaceAll(std::move(value), "\"", "\\\"");
        return value;
    }

    /**
     * @brief 转义成 JSON 字符串内容(不含外层引号)。
     * @param text 原始文本
     * @return 转义后的文本
     *
     * @detail 处理 `\\ " \b \f \n \r \t`。
     *         NOTE: 其余控制字符(0x00-0x1F,如 0x01)原样透传,严格来说会产出非法 JSON;
     *         当前消费方是自产的 manifest,未触发该路径。
     */
    inline MString EscapeJsonString(std::string_view text) {
        MString result;
        result.reserve(text.size());
        for (char ch : text) {
            switch (ch) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += ch;
                break;
            }
        }
        return result;
    }

    // ============================================================================
    // 查找操作
    // ============================================================================

    /**
     * @brief 从 openPos 起找与 openChar 配对的 closeChar 位置。
     * @param text       待扫描文本
     * @param openPos    起始下标(通常指向 openChar)
     * @param openChar   开括号字符,如 '('
     * @param closeChar  闭括号字符,如 ')'
     * @return 配对的 closeChar 下标;未闭合/未找到返回 MString::npos
     *
     * @detail 启发式规则:双引号内的字符不计入括号深度(用前一个字符是否 '\\' 判断转义);
     *         当 openChar 是 '(' 或 '{' 时,< > 一律跳过,以免把模板实参当括号。
     *         代价:`\\\"`(转义的反斜杠后接引号)会被误判为字符串结束。
     */
    inline size_t FindMatching(const MString& text, size_t openPos, char openChar, char closeChar) {
        int  depth    = 0;
        bool inString = false;
        for (size_t i = openPos; i < text.size(); ++i) {
            char c    = text[i];
            char prev = (i > 0) ? text[i - 1] : '\0';

            // 处理字符串字面量:未转义的引号切换 inString
            if (c == '"' && prev != '\\') {
                inString = !inString;
                continue;
            }
            if (inString)
                continue;

            // 找圆括号/花括号配对时跳过模板尖括号
            if ((openChar == '(' || openChar == '{') && (c == '<' || c == '>')) {
                continue;
            }

            if (c == openChar) {
                ++depth;
            } else if (c == closeChar) {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return MString::npos;
    }

    /**
     * @brief 从 pos 起跳过连续空白。
     * @param text 待扫描文本
     * @param pos  起始下标
     * @return 第一个非空白字符的下标;全为空白则返回 text.size()
     */
    inline size_t SkipWhitespace(const MString& text, size_t pos) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        return pos;
    }

    /**
     * @brief 判断 pos 处是否是"独立单词"级别的 keyword。
     * @param text    待扫描文本
     * @param pos     起始下标
     * @param keyword 关键字
     * @return true 完全匹配且左右两侧都不是标识符字符
     *
     * @detail 因此 "class" 不会命中 "classname",也不会命中 "MyClass";不做大小写折叠。
     */
    inline bool IsKeywordAt(const MString& text, size_t pos, std::string_view keyword) {
        if (pos + keyword.size() > text.size()) {
            return false;
        }

        if (text.compare(pos, keyword.size(), keyword) != 0) {
            return false;
        }

        const bool leftOk  = (pos == 0) || !IsIdentifierChar(text[pos - 1]);
        const bool rightOk = (pos + keyword.size() >= text.size()) || !IsIdentifierChar(text[pos + keyword.size()]);
        return leftOk && rightOk;
    }

    // ============================================================================
    // 切分操作
    // ============================================================================

    /**
     * @brief 按顶层逗号切分(忽略 () [] {} <> 内部的逗号)。
     * @param text 待切分文本
     * @return 各片段(已 Trim);空输入会返回**一个空片段**
     *
     * @detail 只做括号配平:不认识字符串/字符字面量,也不认识 `>>` 与模板实参中的逗号以外的
     *         语义。文本里可能含字面量时,请先用 MakeMaskedCopy 遮蔽。
     *         括号不配平会使深度变负,后续分隔符便不再被识别为顶层。
     *         不跳过空片段,也不去重。
     */
    inline TVector<MString> SplitTopLevelArgs(const MString& text) {
        TVector<MString> parts;
        size_t           partStart    = 0;
        int              parenDepth   = 0;
        int              angleDepth   = 0;
        int              braceDepth   = 0;
        int              bracketDepth = 0;

        for (size_t i = 0; i < text.size(); ++i) {
            char ch = text[i];
            switch (ch) {
            case '(':
                ++parenDepth;
                break;
            case ')':
                --parenDepth;
                break;
            case '<':
                ++angleDepth;
                break;
            case '>':
                if (angleDepth > 0)
                    --angleDepth;
                break;
            case '{':
                ++braceDepth;
                break;
            case '}':
                --braceDepth;
                break;
            case '[':
                ++bracketDepth;
                break;
            case ']':
                --bracketDepth;
                break;
            case ',':
                if (parenDepth == 0 && angleDepth == 0 && braceDepth == 0 && bracketDepth == 0) {
                    parts.push_back(Trim(text.substr(partStart, i - partStart)));
                    partStart = i + 1;
                }
                break;
            default:
                break;
            }
        }

        if (partStart <= text.size()) {
            parts.push_back(Trim(text.substr(partStart)));
        }

        return parts;
    }

    /**
     * @brief 按顶层 '|' 切分(忽略括号内部的 '|'),规则同 SplitTopLevelArgs。
     * @param text 待切分文本
     * @return 各片段(已 Trim);空输入返回一个空片段
     *
     * @detail 用于解析 "MScalar|nil" 这类 Teal 类型联合。
     */
    inline TVector<MString> SplitTopLevelPipes(const MString& text) {
        TVector<MString> parts;
        size_t           partStart    = 0;
        int              parenDepth   = 0;
        int              angleDepth   = 0;
        int              braceDepth   = 0;
        int              bracketDepth = 0;

        for (size_t i = 0; i < text.size(); ++i) {
            char ch = text[i];
            switch (ch) {
            case '(':
                ++parenDepth;
                break;
            case ')':
                --parenDepth;
                break;
            case '<':
                ++angleDepth;
                break;
            case '>':
                if (angleDepth > 0)
                    --angleDepth;
                break;
            case '{':
                ++braceDepth;
                break;
            case '}':
                --braceDepth;
                break;
            case '[':
                ++bracketDepth;
                break;
            case ']':
                --bracketDepth;
                break;
            case '|':
                if (parenDepth == 0 && angleDepth == 0 && braceDepth == 0 && bracketDepth == 0) {
                    parts.push_back(Trim(text.substr(partStart, i - partStart)));
                    partStart = i + 1;
                }
                break;
            default:
                break;
            }
        }

        if (partStart <= text.size()) {
            parts.push_back(Trim(text.substr(partStart)));
        }

        return parts;
    }

    // ============================================================================
    // 注释与字面量遮蔽
    // ============================================================================

    /**
     * @brief 复制文本并把注释、字符串、字符字面量**内容**替换成空格。
     * @param text 原始源码文本
     * @return 等长副本:被遮蔽的字符变成空格,换行原样保留
     *
     * @detail 用途:先遮蔽再切分/查找,可以避免注释与字面量里的括号逗号干扰解析。
     *         等长 + 保留换行 ⇒ **字符偏移与行号仍然有效**,这是它能安全替换原文本的前提。
     *         局限:识别 `//`、`/* *\/`、`"..."`、`'...'` 四种状态,不支持 raw string;
     *         转义处理只看前一个字符,`\\\"` 这类写法会提前结束字符串状态。
     */
    inline MString MakeMaskedCopy(const MString& text) {
        MString result = text;
        enum class EState : uint8_t { Normal, LineComment, BlockComment, StringLiteral, CharLiteral };

        EState state = EState::Normal;
        for (size_t i = 0; i < result.size(); ++i) {
            char current = result[i];
            char next    = (i + 1 < result.size()) ? result[i + 1] : '\0';

            switch (state) {
            case EState::Normal:
                if (current == '/' && next == '/') {
                    result[i]     = ' ';
                    result[i + 1] = ' ';
                    ++i;
                    state = EState::LineComment;
                } else if (current == '/' && next == '*') {
                    result[i]     = ' ';
                    result[i + 1] = ' ';
                    ++i;
                    state = EState::BlockComment;
                } else if (current == '"') {
                    result[i] = ' ';
                    state     = EState::StringLiteral;
                } else if (current == '\'') {
                    result[i] = ' ';
                    state     = EState::CharLiteral;
                }
                break;

            case EState::LineComment:
                if (current != '\n') {
                    result[i] = ' ';
                } else {
                    state = EState::Normal;
                }
                break;

            case EState::BlockComment:
                if (current == '*' && next == '/') {
                    result[i]     = ' ';
                    result[i + 1] = ' ';
                    ++i;
                    state = EState::Normal;
                } else if (current != '\n') {
                    result[i] = ' ';
                }
                break;

            case EState::StringLiteral:
                if (current == '\\' && next != '\0') {
                    result[i]     = ' ';
                    result[i + 1] = ' ';
                    ++i;
                } else if (current == '"') {
                    result[i] = ' ';
                    state     = EState::Normal;
                } else if (current != '\n') {
                    result[i] = ' ';
                }
                break;

            case EState::CharLiteral:
                if (current == '\\' && next != '\0') {
                    result[i]     = ' ';
                    result[i + 1] = ' ';
                    ++i;
                } else if (current == '\'') {
                    result[i] = ' ';
                    state     = EState::Normal;
                } else if (current != '\n') {
                    result[i] = ' ';
                }
                break;
            }
        }

        return result;
    }

    // ============================================================================
    // 宏参数辅助
    // ============================================================================

    /**
     * @brief 若首尾正好是 open/close 就剥掉一层(并 Trim 内外)。
     * @param value 输入文本(按值传入)
     * @param open  起始字符,如 '['
     * @param close 结束字符,如 ']'
     * @return 剥壳后的文本;首尾不配对时原样返回
     *
     * @detail 只剥一层、也不校验内部是否配平:"[a][b]" 会得到 "a][b"。
     */
    inline MString StripEnclosingPair(MString value, char open, char close) {
        value = Trim(value);
        if (value.size() >= 2 && value.front() == open && value.back() == close) {
            return Trim(value.substr(1, value.size() - 2));
        }
        return value;
    }

    /**
     * @brief 剥掉最外层双引号(Trim 之后判断)。
     * @param value 输入文本(按值传入)
     * @return 去引号内容;首尾不是成对双引号时返回 Trim 后的原文本
     *
     * @detail 不做转义还原:"a\\\"b" 会原样保留内部反斜杠。
     */
    inline MString UnquoteStringLiteral(MString value) {
        value = Trim(value);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            return value.substr(1, value.size() - 2);
        }
        return value;
    }

    /**
     * @brief 从宏参数文本里取 `key = value` 的 value。
     * @param macroArgs 宏参数字符串(逗号分隔)
     * @param key       键名(与 Trim 后的键精确比较)
     * @return Trim 后的 value;键不存在返回 std::nullopt
     *
     * @detail 只返回第一个匹配。注意与 MASTReflectionVisitor::ExtractMacroValue 的区别:
     *         这里**不剥引号**,AST 层的同名函数会剥掉包裹的 C 字符串引号。
     */
    inline TOptional<MString> ExtractMacroValue(const MString& macroArgs, std::string_view key) {
        for (const MString& part : SplitTopLevelArgs(macroArgs)) {
            size_t equalsPos = part.find('=');
            if (equalsPos == MString::npos) {
                continue;
            }

            MString candidateKey = Trim(part.substr(0, equalsPos));
            if (candidateKey != key) {
                continue;
            }

            return Trim(part.substr(equalsPos + 1));
        }

        return std::nullopt;
    }

} // namespace MHeaderTool
