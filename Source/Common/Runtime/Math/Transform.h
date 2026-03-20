#pragma once

#include "Common/Runtime/Math/Vector.h"
#include "Common/Runtime/Math/Rotator.h"

// 4x4变换矩阵
struct STransform
{
    SVector Translation;
    SRotator Rotation;
    SVector Scale = SVector(1.0f, 1.0f, 1.0f);
};