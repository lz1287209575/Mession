#pragma once

#include "Vector.h"
#include "Rotator.h"

// 4x4变换矩阵
struct STransform
{
    SVector Translation;
    SRotator Rotation;
    SVector Scale = SVector(1.0f, 1.0f, 1.0f);
};