#pragma once

#include "AST/IR.h"
#include "Core/Types.h"

namespace mession::headercodegen {

    class MClangToolRunner {
        public:
        SParseIR RunDump(const MHeaderTool::SOptions& InOptions);
    };

} // namespace mession::headercodegen