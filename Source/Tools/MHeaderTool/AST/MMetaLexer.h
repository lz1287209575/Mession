/**
 * @file MMetaLexer.h
 * @brief MPROPERTY Meta=(K=V,...) 参数的 lexer + parser。
 *
 * @detail 为什么手写:专门处理 `Meta=(K=V,...)` 这一种结构。
 *   - 不用 std::regex:regex 无法解析嵌套括号。
 *   - 不用 clang MacroPiece API:依赖过重,而这里只需要一个纯文本解析器。
 *
 *   容忍的写法(任选其一,空白自由):
 *     MPROPERTY(Meta=(Cli="--listen"))
 *     MPROPERTY(Meta = (Cli = "--listen"))
 *     MPROPERTY(Meta = (Cli = "--listen", NonZero))    // 裸标记 → val="true"
 *     MPROPERTY(Meta = (Cli = "--listen", Range = "1..100"))
 *
 *   失败策略:任何语法错误都返回 std::nullopt,从不抛异常 —— 宏参数解析失败必须优雅降级,
 *   不能把一次编译中的小瑕疵升级成构建崩溃。
 */
#pragma once

#include <cctype>
#include <optional>
#include <utility>
#include <vector>

#include "Common/Runtime/MLib.h"

namespace mession::headercodegen::metaarg {
    enum class ETok : uint8 {
        End,
        Ident,
        String,
        Eq,
        LParen,
        RParen,
        Comma,
    };

    /**
     * @brief 一个 token:种类 + 文本。
     */
    struct SToken {
        ETok    Kind = ETok::End; // 默认 End,便于零初始化的临时对象
        MString Text;             // Ident 的名字,或 String 解码(转义已还原、引号已剥掉)后的内容
    };
    using ST = SToken;

    /**
     * @brief `Meta=(...)` 的极简 lexer:按需扫描,支持一 token 前瞻。
     *
     * @detail 持有源字符串的**引用**,不拷贝 —— 调用方必须保证 Src 比 lexer 活得久。
     *         Src 是宏参数原文,解析器和 lexer 都不假设它是 NUL 结尾的 C 字符串。
     */
    class FLexer {
        public:
        explicit FLexer(const MString& In) : Src(In) {
        }

        /**
         * @brief 取下一个 token 并前进。
         * @return token;输入耗尽后持续返回 {End, ""}
         *
         * @detail 若之前调用过 Peek,则消费其缓存(不重复扫描)。未知字的字符会被静默跳过 ——
         *         parser 会在更靠后的位置解析失败,而不是在这里报错。
         */
        SToken Next() {
            if (bPeekCached) {
                bPeekCached = false;
                return PeekBuf;
            }

            SkipWs();
            if (Pos >= Src.size()) {
                return {ETok::End, {}};
            }

            const char C = Src[Pos];

            if (C == '=') {
                ++Pos;
                return {ETok::Eq, {}};
            }
            if (C == '(') {
                ++Pos;
                return {ETok::LParen, {}};
            }
            if (C == ')') {
                ++Pos;
                return {ETok::RParen, {}};
            }
            if (C == ',') {
                ++Pos;
                return {ETok::Comma, {}};
            }

            if (C == '"' || C == '\'') {
                return ReadString();
            }

            if (IsIdentStart(C)) {
                const size_t Begin = Pos;
                ++Pos;
                while (Pos < Src.size() && IsIdentCont(Src[Pos])) {
                    ++Pos;
                }
                return {ETok::Ident, Src.substr(Begin, Pos - Begin)};
            }

            // 未知字符：跳过继续（parser 在后续会失败）
            ++Pos;
            return Next();
        }

        /**
         * @brief 前瞻下一个 token 的种类,不消耗。
         * @return 下一个 token 的种类
         *
         * @detail 令牌被缓存在内部,下一次 Next 会把它作为结果返回(只消费一次)。
         */
        ETok Peek() {
            if (!bPeekCached) {
                PeekBuf     = Next();
                bPeekCached = true;
            }
            return PeekBuf.Kind;
        }

        private:
        void SkipWs() {
            while (Pos < Src.size() && (Src[Pos] == ' ' || Src[Pos] == '\t' || Src[Pos] == '\n' || Src[Pos] == '\r')) {
                ++Pos;
            }
        }

        static bool IsIdentStart(char C) {
            return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_';
        }

        static bool IsIdentCont(char C) {
            return IsIdentStart(C) || (C >= '0' && C <= '9');
        }

        SToken ReadString() {
            const char Quote = Src[Pos++]; // '"' or '\''
            MString    Out;
            Out.reserve(32);
            while (Pos < Src.size() && Src[Pos] != Quote) {
                if (Src[Pos] == '\\' && Pos + 1 < Src.size()) {
                    const char Esc = Src[Pos + 1];
                    switch (Esc) {
                    case 'n':
                        Out.push_back('\n');
                        break;
                    case 't':
                        Out.push_back('\t');
                        break;
                    case 'r':
                        Out.push_back('\r');
                        break;
                    case '\\':
                        Out.push_back('\\');
                        break;
                    case '\'':
                        Out.push_back('\'');
                        break;
                    case '"':
                        Out.push_back('"');
                        break;
                    default:
                        Out.push_back(Esc);
                        break;
                    }
                    Pos += 2;
                } else {
                    Out.push_back(Src[Pos]);
                    ++Pos;
                }
            }
            if (Pos < Src.size()) {
                ++Pos; // 有闭引号就吞掉;未闭合(扫到输入末尾)时保持不前进
            }
            return {ETok::String, std::move(Out)};
        }

        const MString& Src;
        size_t         Pos = 0;
        SToken         PeekBuf;
        bool           bPeekCached = false;
    };

    /**
     * @brief 解析 `Meta=(K=V,...)` 实参文本。
     * @param Args MFUNCTION/MPROPERTY 的括号内原文(即 `Meta` 之后的部分)
     * @return 有序键值对;不是 Meta 块或语法有误时返回 std::nullopt(不抛异常)
     *
     * @detail 文法(BNF):
     *           MetaBlock = "Meta" WS* "=" WS* "(" Item (WS* "," WS* Item)* WS* ")"
     *           Item      = Ident (WS* "=" WS* Value)?
     *           Value     = String | Ident
     *         容错:标识符后没有 "=" 视为裸标记,值取 "true";重复键不做去重,按出现顺序全部保留。
     *         返回顺序即源码顺序 —— 下游依赖这个顺序生成稳定的产物。
     */
    inline std::optional<TVector<TPair<MString, MString>>> ParseMetaBlock(const MString& Args) {
        TVector<TPair<MString, MString>> Out;
        FLexer                           Lex(Args);

        // 必须以 "Meta" 打头(允许前导空白)
        ST Tok = Lex.Next();
        if (Tok.Kind != ETok::Ident || Tok.Text != "Meta") {
            return std::nullopt;
        }

        if (Lex.Next().Kind != ETok::Eq) {
            return std::nullopt;
        }

        if (Lex.Next().Kind != ETok::LParen) {
            return std::nullopt;
        }

        // 条目循环:读到 ')' 或输入结束为止。
        // 循环体每次读"一个条目 + 一个分隔符",',' 继续、')' 结束,其它一律判错。
        while (true) {
            if (Lex.Peek() == ETok::RParen || Lex.Peek() == ETok::End) {
                break;
            }

            ST Key = Lex.Next();
            if (Key.Kind != ETok::Ident) {
                return std::nullopt;
            }

            MString Value;
            if (Lex.Peek() == ETok::Eq) {
                Lex.Next(); // 吞 '='
                ST V = Lex.Next();
                if (V.Kind == ETok::String || V.Kind == ETok::Ident) {
                    Value = std::move(V.Text);
                } else {
                    return std::nullopt;
                }
            } else {
                // 裸标记(如 Meta=(NonZero, ...)):没有 "=" 时值默认 "true"
                Value = "true";
            }

            Out.emplace_back(std::move(Key.Text), std::move(Value));

            // 项间分隔符：','（继续）或 ')'（结束）
            const ETok NextKind = Lex.Next().Kind;
            if (NextKind == ETok::Comma) {
                continue;
            }
            if (NextKind == ETok::RParen) {
                return Out;
            }
            return std::nullopt;
        }

        // 正常结束路径:Peek 为 ')' 时消费掉它;为 End 说明括号没闭合,按失败处理。
        if (Lex.Peek() == ETok::RParen) {
            Lex.Next();
            return Out;
        }
        return std::nullopt;
    }

} // namespace mession::headercodegen::metaarg