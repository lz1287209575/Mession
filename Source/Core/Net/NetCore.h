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
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <optional>
#if __cplusplus >= 201703L
#include <string_view>
#endif
#include <utility>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>

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

template<typename T, typename Container = std::deque<T>>
using TStack = std::stack<T, Container>;

template<typename T>
using TDeque = std::deque<T>;

template<typename T, typename Compare = std::less<T>>
using TSet = std::set<T, Compare>;

template<typename T, typename Compare = std::less<T>>
using TMultiSet = std::multiset<T, Compare>;

template<typename K, typename V, typename Compare = std::less<K>>
using TMultiMap = std::map<K, V, Compare>;

template<typename T>
using TSharedPtr = std::shared_ptr<T>;

template<typename T, typename... TArgs>
TSharedPtr<T> MakeShared(TArgs&&... Args)
{
    return std::make_shared<T>(std::forward<TArgs>(Args)...);
}

template<typename T>
using TWeakPtr = std::weak_ptr<T>;

template<typename T>
using TUniquePtr = std::unique_ptr<T>;

template<typename T>
using TEnableSharedFromThis = std::enable_shared_from_this<T>;

template<typename Signature>
using TFunction = std::function<Signature>;

template<typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
using TUnorderedMap = std::unordered_map<K, V, Hash, KeyEqual>;

template<typename T, typename Hash = std::hash<T>, typename KeyEqual = std::equal_to<T>>
using TUnorderedSet = std::unordered_set<T, Hash, KeyEqual>;

template<typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
using TUnorderedMultiMap = std::unordered_multimap<K, V, Hash, KeyEqual>;

template<typename T, typename Hash = std::hash<T>, typename KeyEqual = std::equal_to<T>>
using TUnorderedMultiSet = std::unordered_multiset<T, Hash, KeyEqual>;

#if __cplusplus >= 201703L
using TStringView = std::string_view;
#endif

template<typename T>
using TOptional = std::optional<T>;

template<typename TFirst, typename TSecond>
using TPair = std::pair<TFirst, TSecond>;

// 文件流
#include <fstream>
using TIfstream = std::ifstream;
using TOfstream = std::ofstream;

// 兼容旧命名，后续逐步迁移
using FString = TString;
using FName = TName;
using TArray = TByteArray;

// 字节序：协议使用网络字节序（大端），提供 HostToNetwork/NetworkToHost 供序列化使用
#if defined(_WIN32) || defined(_WIN64)
    #include <stdlib.h>
    inline uint16 HostToNetwork(uint16 Value) { return _byteswap_ushort(Value); }
    inline uint32 HostToNetwork(uint32 Value) { return _byteswap_ulong(Value); }
    inline uint64 HostToNetwork(uint64 Value) { return _byteswap_uint64(Value); }
#else
    inline uint16 HostToNetwork(uint16 Value) { return __builtin_bswap16(Value); }
    inline uint32 HostToNetwork(uint32 Value) { return __builtin_bswap32(Value); }
    inline uint64 HostToNetwork(uint64 Value) { return __builtin_bswap64(Value); }
#endif
inline uint16 NetworkToHost(uint16 Value) { return HostToNetwork(Value); }
inline uint32 NetworkToHost(uint32 Value) { return HostToNetwork(Value); }
inline uint64 NetworkToHost(uint64 Value) { return HostToNetwork(Value); }

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

// 时间抽象
class MTime
{
public:
    static double GetTimeSeconds()
    {
        auto Now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(Now.time_since_epoch()).count();
    }

    static void SleepSeconds(double Seconds)
    {
        if (Seconds > 0.0)
        {
            std::this_thread::sleep_for(std::chrono::duration<double>(Seconds));
        }
    }

    static void SleepMilliseconds(uint32 Ms)
    {
        if (Ms > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
        }
    }
};

// Result/Error 类型 - 统一错误返回
template<typename T, typename E = TString>
struct TResult
{
    TOptional<T> Value;
    TOptional<E> Error;

    static TResult Ok(T InValue)
    {
        TResult R;
        R.Value = InValue;
        return R;
    }

    static TResult Err(E InError)
    {
        TResult R;
        R.Error = InError;
        return R;
    }

    bool IsOk() const { return Value.has_value(); }
    bool IsErr() const { return Error.has_value(); }
    T& GetValue() { return *Value; }
    const T& GetValue() const { return *Value; }
    E& GetError() { return *Error; }
    const E& GetError() const { return *Error; }
};

// void 特化：仅表示成功/失败
template<typename E>
struct TResult<void, E>
{
    bool bSuccess = false;
    TOptional<E> Error;

    static TResult Ok()
    {
        TResult R;
        R.bSuccess = true;
        return R;
    }

    static TResult Err(E InError)
    {
        TResult R;
        R.bSuccess = false;
        R.Error = InError;
        return R;
    }

    bool IsOk() const { return bSuccess; }
    bool IsErr() const { return Error.has_value(); }
    E& GetError() { return *Error; }
    const E& GetError() const { return *Error; }
};

// 唯一ID生成器（线程安全）
class MUniqueIdGenerator
{
private:
    inline static std::atomic<uint64> CurrentId{0};

public:
    static uint64 Generate()
    {
        return CurrentId.fetch_add(1, std::memory_order_relaxed) + 1;
    }
};

// 不可复制基类（可移动），用于服务、连接等
struct MNonCopyable
{
    MNonCopyable() = default;
    MNonCopyable(const MNonCopyable&) = delete;
    MNonCopyable& operator=(const MNonCopyable&) = delete;
    MNonCopyable(MNonCopyable&&) = default;
    MNonCopyable& operator=(MNonCopyable&&) = default;
};

// 只读缓冲区视图（C++20），协议解析等场景使用项目别名
#if __cplusplus >= 202002L
#include <span>
template<typename T>
using TSpan = std::span<const T>;
template<typename T>
using TSpanMutable = std::span<T>;
#endif
