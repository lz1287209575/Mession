/**
 * @file Vector.h
 * @brief SVector(3D 向量)与 Clamp / Lerp / Distance 等基础数学工具。
 *
 * @detail 全部是纯值类型:无平台依赖、无堆分配、无全局状态。SVector 由 3 个 float
 * 组成且可平凡复制,反射存档按 POD 整体写出(见 ReflectionArchiveInvoke.inl),
 * 因此字段的个数/顺序/类型就是序列化布局,禁止调整。
 */
#pragma once

#include <cmath>

/**
 * @brief SVector - 3D 向量(float 精度)。
 *
 * @detail 只覆盖最常用的算子与几何运算,没有叉积、矩阵、四元数等功能。
 * 除 operator/ 之外都不对输入做校验(除零、NaN、Inf 一律原样传播),由调用方保证。
 */
struct SVector {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    SVector() = default;
    SVector(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {
    }

    // 逐分量四则运算;operator/ 不检查 Scalar 是否为 0
    SVector operator+(const SVector& V) const {
        return SVector(X + V.X, Y + V.Y, Z + V.Z);
    }
    SVector operator-(const SVector& V) const {
        return SVector(X - V.X, Y - V.Y, Z - V.Z);
    }
    SVector operator*(float Scalar) const {
        return SVector(X * Scalar, Y * Scalar, Z * Scalar);
    }
    SVector operator/(float Scalar) const {
        return SVector(X / Scalar, Y / Scalar, Z / Scalar);
    }

    /**
     * @brief Size - 欧氏模长。
     * @return sqrt(X² + Y² + Z²)
     *
     * @detail 含一次 sqrt;只做长度比较时改用 SizeSquared() 更省。
     */
    float Size() const {
        return std::sqrt(X * X + Y * Y + Z * Z);
    }

    /**
     * @brief SizeSquared - 模长的平方。
     * @return X² + Y² + Z²
     */
    float SizeSquared() const {
        return X * X + Y * Y + Z * Z;
    }

    /**
     * @brief Normalized - 归一化。
     * @return 单位向量;零长度(Size() == 0)时返回 Zero() 而非 NaN
     *
     * @detail 兜底只判 <= 0,极短向量(如 1e-8)会被放大,不额外做 epsilon 处理。
     */
    SVector Normalized() const {
        const float Len = Size();
        if (Len <= 0.0f) {
            return Zero();
        }
        return SVector(X / Len, Y / Len, Z / Len);
    }

    /**
     * @brief Dot - 点积。
     * @param V 另一个向量
     * @return X*V.X + Y*V.Y + Z*V.Z
     */
    float Dot(const SVector& V) const {
        return X * V.X + Y * V.Y + Z * V.Z;
    }

    /**
     * @brief Zero - 零向量 (0,0,0)。
     */
    static SVector Zero() {
        return SVector(0, 0, 0);
    }

    /**
     * @brief One - 全 1 向量 (1,1,1)。
     */
    static SVector One() {
        return SVector(1, 1, 1);
    }
};

/**
 * @brief Distance - 两点欧氏距离。
 * @param A 起点
 * @param B 终点
 * @return |B - A|
 */
inline float Distance(const SVector& A, const SVector& B) {
    return (B - A).Size();
}

/**
 * @brief Clamp - 把标量夹到闭区间 [MinVal, MaxVal]。
 * @param Value  待夹取的值
 * @param MinVal 下界(含)
 * @param MaxVal 上界(含)
 * @return 夹取后的值
 *
 * @detail 不校验 MinVal <= MaxVal:若区间反了,先命中 < MinVal 的分支,返回 MinVal。
 */
inline float Clamp(float Value, float MinVal, float MaxVal) {
    if (Value < MinVal) {
        return MinVal;
    }
    if (Value > MaxVal) {
        return MaxVal;
    }
    return Value;
}

/**
 * @brief Lerp - 标量线性插值。
 * @param A 起点(T = 0 时的值)
 * @param B 终点(T = 1 时的值)
 * @param T 插值权重,不做 [0,1] 夹取(外插由调用方决定)
 * @return A + (B - A) * T
 */
inline float Lerp(float A, float B, float T) {
    return A + (B - A) * T;
}

/**
 * @brief Lerp - 向量线性插值(逐分量)。
 * @param A 起点
 * @param B 终点
 * @param T 插值权重
 * @return 逐分量插值结果
 */
inline SVector Lerp(const SVector& A, const SVector& B, float T) {
    return SVector(Lerp(A.X, B.X, T), Lerp(A.Y, B.Y, T), Lerp(A.Z, B.Z, T));
}
