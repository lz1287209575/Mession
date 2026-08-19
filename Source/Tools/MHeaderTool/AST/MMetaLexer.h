/**
 * @file MMetaLexer.h
 * @brief MPROPERTY Meta=(K=V,...) 参数的 lexer + parser.
 *
 * 设计：手写 lexer + 循环 parser，专门处理 `Meta=(K=V,...)` 结构。
 * 不用 std::regex：regex 不能解析嵌套 paren。
 * 不用 clang MacroPiece API：依赖太重。
 *
 * 容忍写法（任选）：
 *   MPROPERTY(Meta=(Cli="--listen"))
 *   MPROPERTY(Meta = (Cli = "--listen"))
 *   MPROPERTY(Meta = (Cli = "--listen", NonZero))    // 裸标记 → val="true"
 *   MPROPERTY(Meta = (Cli = "--listen", Range = "1..100"))
 *
 * 失败时返回 std::nullopt（宏解析失败应该优雅降级）。
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

    struct SToken {
        ETok    Kind = ETok::End;
        MString Text; // Ident 名称 / String 解码后内容
    };
    using ST = SToken;

    class FLexer {
        public:
        explicit FLexer(const MString& In) : Src(In) {
        }

        // Next 优先返回 Peek 缓存的 token（一次消耗），否则真扫下一 token。
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

        // Peek 不消耗：缓存 token，Next 一次性返回。
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
                ++Pos; // 吞闭引号
            }
            return {ETok::String, std::move(Out)};
        }

        const MString& Src;
        size_t         Pos = 0;
        SToken         PeekBuf;
        bool           bPeekCached = false;
    };

    // 语法（BNF）：
    //   MetaBlock = "Meta" WS* "=" WS* "(" Item (WS* "," WS* Item)* WS* ")"
    //   Item      = Ident (WS* "=" WS* Value)?
    //   Value     = String | Ident
    //
    // 容错：ident 后没 "=" → 默认 value="true"（裸标记）。
    // 任何语法错误返回 std::nullopt（不抛）。

    inline std::optional<TVector<TPair<MString, MString>>> ParseMetaBlock(const MString& Args) {
        TVector<TPair<MString, MString>> Out;
        FLexer                           Lex(Args);

        // 必须以 "Meta" 开头（可有前导空白）
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

        // Items：循环到 ')' 或 End
        // 设计：循环里读一项 + 项间的分隔符；break 留给 ','
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
                // 裸标记（Meta=(NonZero, ...)）→ 默认 "true"
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

        // Loop 退出（Peek == RParen or End），消耗 ')'
        if (Lex.Peek() == ETok::RParen) {
            Lex.Next();
            return Out;
        }
        return std::nullopt;
    }

} // namespace mession::headercodegen::metaarg