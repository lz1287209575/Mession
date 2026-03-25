#pragma once

#include <cmath>


// 3D向量
struct SVector
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    SVector() = default;
    SVector(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

    SVector operator+(const SVector& V) const { return SVector(X + V.X, Y + V.Y, Z + V.Z); }
    SVector operator-(const SVector& V) const { return SVector(X - V.X, Y - V.Y, Z - V.Z); }
    SVector operator*(float Scalar) const { return SVector(X * Scalar, Y * Scalar, Z * Scalar); }
    SVector operator/(float Scalar) const { return SVector(X / Scalar, Y / Scalar, Z / Scalar); }
    
    float Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
    float SizeSquared() const { return X * X + Y * Y + Z * Z; }

    // 归一化，返回单位向量；若长度为 0 则返回 Zero()
    SVector Normalized() const
    {
        const float Len = Size();
        if (Len <= 0.0f)
        {
            return Zero();
        }
        return SVector(X / Len, Y / Len, Z / Len);
    }

    // 点积
    float Dot(const SVector& V) const
    {
        return X * V.X + Y * V.Y + Z * V.Z;
    }

    static SVector Zero() { return SVector(0, 0, 0); }
    static SVector One() { return SVector(1, 1, 1); }
};

// 两点距离
inline float Distance(const SVector& A, const SVector& B)
{
    return (B - A).Size();
}

// 基础数学：Clamp / Lerp（数值与向量）
inline float Clamp(float Value, float MinVal, float MaxVal)
{
    if (Value < MinVal)
    {
        return MinVal;
    }
    if (Value > MaxVal)
    {
        return MaxVal;
    }
    return Value;
}

inline float Lerp(float A, float B, float T)
{
    return A + (B - A) * T;
}

inline SVector Lerp(const SVector& A, const SVector& B, float T)
{
    return SVector(Lerp(A.X, B.X, T), Lerp(A.Y, B.Y, T), Lerp(A.Z, B.Z, T));
}