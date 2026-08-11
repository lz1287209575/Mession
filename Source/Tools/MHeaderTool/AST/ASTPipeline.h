#pragma once

#include "Core/Types.h"
#include "AST/IR.h"

namespace mession::headercodegen
{

// SOptions lives in MHeaderTool; bring it in so the brief's verbatim
// `SOptions` signature inside `mession::headercodegen` compiles cleanly
// without forcing every caller to write `MHeaderTool::SOptions`.
using MHeaderTool::SOptions;

class MASTPipeline
{
public:
    // Brief uses `MASTPipeline::Run(Options)` (class-scope call without
    // object), so this is a static — even though the brief's class
    // declaration shows it as a non-static member. Static matches the
    // brief's call site.
    static SParseIR Run(const SOptions& InOptions);
};

}  // namespace mession::headercodegen
