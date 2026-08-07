#pragma once

#include "Core/Types.h"
#include "Common/Runtime/MLib.h"
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

namespace mession::headercodegen {

    using SOptions = MHeaderTool::SOptions;

    struct SParsedRecord {
        MString  Name;
        MString  QualifiedName;
        fs::path HeaderPath;
        uint32   SourceLine         = 0;
        bool     bHasMGeneratedBody = false;
        bool     bHasMClassMarker   = false;
        bool     bHasMStructMarker  = false;
    };

    struct SParsedFunction {
        MString  Name;
        fs::path HeaderPath;
        uint32   SourceLine = 0;
        bool     bIsAsync   = false;
    };

    struct SParsedEnum {
        MString  Name;
        fs::path HeaderPath;
    };

    struct SParseIR {
        int                      SchemaVersion = 1;
        TVector<SParsedRecord>   Records;
        TVector<SParsedFunction> Functions;
        TVector<SParsedEnum>     Enums;
    };

} // namespace mession::headercodegen
