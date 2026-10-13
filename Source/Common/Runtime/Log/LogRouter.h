#pragma once
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogSinks.h" // ELogSinkId + MakeSinkMask live here (Task 5)
#include "Common/Runtime/MLib.h"
#include <atomic>

/**
 * @file LogRouter.h
 * @brief MLogRouter —— 分类到 (SinkMask, MinLevel) 的无锁读路由表;
 * 及路由规则结构体。
 *
 * @detail 说明:ELogSinkId / MakeSinkMask 曾在 Task 4 临时定义在本文件,
 * 现已上移到 LogSinks.h(Task 5),让枚举与消费其位值的 ILogSink 接口
 * 相邻。位值 0..4 属稳定 ABI,禁止重排。
 */

struct SLogRouteRule {
    const SLogCategory* Category = nullptr;
    uint32              SinkMask = 0xFFFFFFFFu;
    ELogLevel           MinLevel = ELogLevel::Trace;
};

/**
 * @brief 分类 -> (SinkMask, MinLevel) 路由表。
 *
 * @detail 热路径读(ResolveSinkMask)无锁:两个 std::atomic<T*> 指针
 * 持有当前代表代的表。写(SetRule / ClearRules)先拷贝现有数组,改拷贝,
 * 再原子发布新指针。旧代对在途读者仍然有效,由代计数器回收
 * (不逐调用 delete,策略见 .cpp)。
 */
class MLogRouter {
    public:
    static MLogRouter& Get();

    /**
     * @brief 应用或替换单个分类的路由规则。
     * @param Rule 新规则;Category 为 nullptr 时忽略该调用
     */
    void SetRule(const SLogRouteRule& Rule);

    /**
     * @brief 清空所有自定义路由,回落到默认(全部 sink、无级别过滤)。
     */
    void ClearRules();

    /**
     * @brief 热路径读:解析某分类某级别应送达的 sink 位掩码。
     * @param CategoryId 分类 id(SLogCategory::Id)
     * @param Level 记录级别
     * @return 应接收该记录的 sink 位掩码;级别低于 MinLevel 或该分类
     * 未配置任何 sink 时返回 0
     */
    uint32 ResolveSinkMask(uint16 CategoryId, ELogLevel Level) const;

    private:
    MLogRouter() = default;

    // 惰性分配初始空表。首次使用时由 SetRule 在写者互斥量下调用,
    // 那里不需要 atomic-CAS。保持为私有成员,让写者代码能以原子方式
    // 修改这些成员指针。
    void EnsureTablesAllocatedLocked();

    // 两个指针作为一个整体被替换;读者经由同一代总能看到一致的
    // 一对(代推进序列见 .cpp)。
    std::atomic<TVector<uint32>*>    CategoryToMask{nullptr};
    std::atomic<TVector<ELogLevel>*> CategoryToMinLevel{nullptr};
};
