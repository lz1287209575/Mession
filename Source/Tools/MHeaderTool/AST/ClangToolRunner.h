#pragma once

#include "AST/IR.h"
#include "Core/Types.h"

namespace mession::headercodegen {

    class MClangToolRunner {
        public:
        SParseIR RunDump(const SOptions& InOptions);
    };

} // namespace mession::headercodegen