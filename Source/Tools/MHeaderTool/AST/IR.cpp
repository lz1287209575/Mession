/**
 * @file IR.cpp
 * @brief IR.h 的 TU 锚点:IR 是纯数据聚合体,此文件不含任何定义。
 *
 * @detail NOTE: 所有 IR 结构体都是无方法的聚合体,因此本编译单元**故意为空** ——
 *         它存在只是为了给 IR 相关的目标提供翻译单元,并让 include 依赖可被构建系统
 *         观测到。字段填充逻辑在 AST 阶段(ASTDumpAction / ASTReflectionVisitor),
 *         不要在 IR.cpp 里加实现。
 */
#include "AST/IR.h"

namespace mession::headercodegen {
    // 本编译单元故意为空:IR 类型全是无方法的聚合体,字段填充在 AST 阶段
    // (ASTDumpAction / ASTReflectionVisitor)完成,不要在这里加实现。
}