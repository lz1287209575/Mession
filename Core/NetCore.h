#pragma once

// 基础类型定义 - 仿UE风格
#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <cmath>
#include <string>
#include <array>
#include <list>
#include <queue>
#include <set>

// 类型别名
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;
using float32 = float;
using double64 = double;

// 字符串类型
using FString = std::string;
using FName = std::string;

// 字节数组
using TArray = std::vector<uint8>;

// 常量定义
constexpr uint32 MAX_PACKET_SIZE = 65535;
constexpr uint32 MAX_PLAYER_COUNT = 10000;
constexpr float DEFAULT_TICK_RATE = 1.0f / 60.0f;

// 3D向量
struct FVector
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    FVector() = default;
    FVector(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

    FVector operator+(const FVector& V) const { return FVector(X + V.X, Y + V.Y, Z + V.Z); }
    FVector operator-(const FVector& V) const { return FVector(X - V.X, Y - V.Y, Z - V.Z); }
    FVector operator*(float Scalar) const { return FVector(X * Scalar, Y * Scalar, Z * Scalar); }
    FVector operator/(float Scalar) const { return FVector(X / Scalar, Y / Scalar, Z / Scalar); }
    
    float Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
    float SizeSquared() const { return X * X + Y * Y + Z * Z; }
    
    static FVector Zero() { return FVector(0, 0, 0); }
    static FVector One() { return FVector(1, 1, 1); }
};

// 旋转向量
struct FRotator
{
    float Pitch = 0.0f;
    float Yaw = 0.0f;
    float Roll = 0.0f;

    FRotator() = default;
    FRotator(float InPitch, float InYaw, float InRoll) 
        : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}
};

// 4x4变换矩阵
struct FTransform
{
    FVector Translation;
    FRotator Rotation;
    FVector Scale = FVector(1.0f, 1.0f, 1.0f);
};

// 唯一ID生成器
class FUniqueIdGenerator
{
private:
    inline static uint64 CurrentId = 0;
    
public:
    static uint64 Generate()
    {
        return ++CurrentId;
    }
};
