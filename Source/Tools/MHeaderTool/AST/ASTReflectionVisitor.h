/**
 * @file ASTReflectionVisitor.h
 * @brief MASTReflectionVisitor —— 从 Clang AST 提取反射信息的遍历器(生产路径的唯一实现)。
 *
 * @detail 所有权与线程安全:
 *   - 持有 Ctx / IR / SM 以及宏展开记录表的**引用**,一概不拥有。调用方必须保证它们
 *     在整个 TraverseDecl 期间存活,且 IR 可独占写入。
 *   - 宏展开记录由 PPCallbacks 按 TU 解析顺序追加(见 ASTPipeline.cpp),本类只做只读倒序
 *     查找,不重排、不修改。
 *   - 非线程安全:每个 TU 一个实例(由 MASTReflectionAction 创建),不共享。
 *   - 与 ASTDumpAction 的裸 dump 不同,这里会按反射宏(MCLASS/MSTRUCT/MFUNCTION/MPROPERTY)
 *     过滤,并解析类型、await 站点等语义信息。
 */
#pragma once

#include "AST/IR.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace mession::headercodegen {

    /**
     * @brief 一条反射宏展开记录(MCLASS/MPROPERTY/MFUNCTION/MSTRUCT/MGENERATED_BODY)。
     *
     * @detail 参数文本直接取自宏调用的源码范围(未展开 token),没有字节窗口限制,
     *         Meta=(...) 之类的长参数不会被截断。
     */
    struct SMacroExpand {
        MString               Name;        // 宏名(不含括号)
        MString               Args;        // 括号内参数原文,多个实参用 ", " 连接
        clang::SourceLocation EndLoc;      // 宏调用结束位置;用 isBeforeInTranslationUnit 比较,跨文件也正确
        uint32                EndLine = 0; // 宏调用 ')' 所在行,用于同文件内的"宏 → 声明"关联判定
    };

    /**
     * @brief 反射信息提取器:把反射宏修饰过的声明翻译成 IR 节点。
     *
     * @detail 大致分工:Visit 系列负责"发现并记录",Collect 系列负责"深入分析函数体",
     *         Extract / Split / QualTypeTo 系列是宏参数与类型的解析辅助。
     */
    class MASTReflectionVisitor : public clang::RecursiveASTVisitor<MASTReflectionVisitor> {
        public:
        explicit MASTReflectionVisitor(clang::ASTContext& Ctx, SParseIR& IR, const TVector<SMacroExpand>& MacroExpands);

        bool TraverseDecl(clang::Decl* D);
        bool VisitCXXRecordDecl(clang::CXXRecordDecl* RD);
        bool VisitFieldDecl(clang::FieldDecl* FD);
        bool VisitTypeAliasDecl(clang::TypeAliasDecl* TAD);
        bool VisitFunctionDecl(clang::FunctionDecl* FD);
        bool VisitEnumDecl(clang::EnumDecl* ED);

        private:
        /** @brief 收集函数体里的 await 挂起点(AWAIT_OK / TAwaitable<> / 裸 Awaitable)。 */
        void CollectAwaitSites(clang::FunctionDecl* FD, SParsedFunction& OutFunc);
        /** @brief 收集跨 await 点仍然存活的局部变量(状态机 Frame 需要持久化它们)。 */
        void CollectLiveAcrossAwait(clang::FunctionDecl* FD, SParsedFunction& OutFunc);

        /** @brief 在已收集的 Records 里按 Clang 声明反查 IR 记录;找不到返回 nullptr。 */
        SParsedRecord* FindRecordByDecl(const clang::CXXRecordDecl* RD);
        /** @brief 取 SourceLocation 对应的规范化文件路径(lexically_normal,跨 TU 才能比对相等)。 */
        fs::path GetFilePath(clang::SourceLocation Loc) const;
        /** @brief 取源码范围的原始拼写文本;范围非法时返回空串。 */
        MString GetSourceText(clang::SourceRange Range) const;

        /** @brief 解析 MFUNCTION(...) 实参文本:裸标记 → 传输/RPC 语义,Key=Value → 文本字段。 */
        void ApplyMFUNCTIONMacroArgs(const MString& MacroArgs, SParsedFunction& OutFunc) const;

        /**
         * @brief 一次宏调用的命中结果。
         */
        struct SMacroCallHit {
            MString Args;          // 括号内参数原文
            uint32  CloseLine = 0; // 宏调用 ')' 所在行(用于宏-声明关联判定)
        };

        /**
         * @brief 查 `MacroName` 在 Loc 之前、且与 Loc 同文件的最近一次调用。
         * @param Loc       待修饰声明的位置
         * @param MacroName 宏名(如 "MPROPERTY")
         * @return 命中则返回参数文本 + 结束行;无命中返回 std::nullopt
         *
         * @detail 两个硬性条件:宏与声明必须同文件(否则 include 链里其它头的同名宏会误配),
         *         且宏位置必须早于声明。注释/字符串不产生宏展开事件,所以不会有误命中;
         *         参数取的是源码原文,长度不受窗口限制。
         */
        TOptional<SMacroCallHit> ExtractMacroCallArgs(clang::SourceLocation Loc, llvm::StringRef MacroName) const;

        /**
         * @brief 从宏参数文本里取 `Key = Value` 的 Value(已去引号与首尾空白)。
         * @param MacroArgs 宏参数原文
         * @param Key       键名
         * @return 命中返回值;缺失返回 std::nullopt
         */
        TOptional<MString> ExtractMacroValue(const MString& MacroArgs, const MString& Key) const;

        /**
         * @brief 按顶层逗号切分宏参数(尊重括号与引号嵌套)。
         */
        TVector<MString> SplitMacroArgs(const MString& Args) const;

        /**
         * @brief 把 Clang 的 QualType 规范化成 SParsedType(引用/const/指针 + 模板实参递归展开)。
         * @param QT             待转换的类型
         * @param SpellingRange  可选:类型的源码范围,用于取"作者写的那种拼写"而不是规范化名
         * @return 规范化后的类型
         */
        SParsedType QualTypeToSParsedType(clang::QualType QT, clang::SourceRange SpellingRange = {}) const;

        /** @brief 是否为 SFuture<T> / 等价 future 结果类型(决定 async 返回值处理方式)。 */
        bool IsSFutureResultType(const SParsedType& T) const;

        clang::ASTContext&           Ctx;             // 不拥有
        SParseIR&                    IR;              // 不拥有;写入目标
        clang::SourceManager&        SM;              // 不拥有
        const TVector<SMacroExpand>& MacroExpandsRef; // 不拥有;只读
    };

} // namespace mession::headercodegen
