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
#include <deque>

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

// 项目基础类型别名
using TString = std::string;
using TName = std::string;
using TByteArray = std::vector<uint8>;

template<typename T>
using TVector = std::vector<T>;

template<typename K, typename V, typename Compare = std::less<K>>
using TMap = std::map<K, V, Compare>;

template<typename T, size_t Size>
using TFixedArray = std::array<T, Size>;

template<typename T>
using TList = std::list<T>;

template<typename T, typename Container = std::deque<T>>
using TQueue = std::queue<T, Container>;

template<typename T, typename Compare = std::less<T>>
using TSet = std::set<T, Compare>;

template<typename T>
using TSharedPtr = std::shared_ptr<T>;

template<typename T>
using TWeakPtr = std::weak_ptr<T>;

template<typename T>
using TUniquePtr = std::unique_ptr<T>;

template<typename T>
using TEnableSharedFromThis = std::enable_shared_from_this<T>;

template<typename Signature>
using TFunction = std::function<Signature>;

// 兼容旧命名，后续逐步迁移
using FString = TString;
using FName = TName;
using TArray = TByteArray;

// 常量定义
constexpr uint32 MAX_PACKET_SIZE = 65535;
constexpr uint32 MAX_PLAYER_COUNT = 10000;
constexpr float DEFAULT_TICK_RATE = 1.0f / 60.0f;

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
    
    static SVector Zero() { return SVector(0, 0, 0); }
    static SVector One() { return SVector(1, 1, 1); }
};

// 旋转向量
struct SRotator
{
    float Pitch = 0.0f;
    float Yaw = 0.0f;
    float Roll = 0.0f;

    SRotator() = default;
    SRotator(float InPitch, float InYaw, float InRoll) 
        : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}
};

// 4x4变换矩阵
struct STransform
{
    SVector Translation;
    SRotator Rotation;
    SVector Scale = SVector(1.0f, 1.0f, 1.0f);
};

// 唯一ID生成器
class MUniqueIdGenerator
{
private:
    inline static uint64 CurrentId = 0;
    
public:
    static uint64 Generate()
    {
        return ++CurrentId;
    }
};
