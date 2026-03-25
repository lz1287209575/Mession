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
using MString = std::string;
using MName = std::string;
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

// 只读缓冲区视图（C++20），协议解析等场景使用项目别名
#if __cplusplus >= 202002L
#include <span>
template<typename T>
using TSpan = std::span<const T>;
template<typename T>
using TSpanMutable = std::span<T>;
#endif
