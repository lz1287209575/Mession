/**
 * @file LogContext.h
 * @brief MLogContext 线程局部日志上下文(key-value 键值对),
 * 以及 RAII 作用域辅助类 MLogContextScope。
 *
 * @detail 说明:需求原文用 MStringView 作参数类型;本代码库已把
 * `namespace MStringView` 改名为 `namespace MStringViewUtil`
 * (StringUtils.h),避免与 MStringView 类型别名撞名。本头文件统一
 * 使用 MStringView 类型。
 */
#pragma once
#include "Common/Runtime/MLib.h"
#include <array>


class MLogContext {
    public:
    static constexpr size_t kMaxEntries   = 32;
    static constexpr size_t kMaxSnapshots = 4096;

    struct SEntry {
        MString Key;
        MString Value;
    };

    void Set(MStringView Key, MStringView Value);
    void Set(MStringView Key, int64 Value);
    void Unset(MStringView Key);

    /**
     * @brief 把当前上下文打包为一份快照。
     * @return 快照 id(写入全局快照池);日志记录通过 id 引用快照,
     * 避免逐条复制 key-value
     */
    uint32 CaptureSnapshot();

    /**
     * @brief 释放一份快照,归还快照池槽位。
     * @param SnapshotId CaptureSnapshot() 返回的 id
     * @detail 记录出队后即可释放;重复释放同一 id 是调用方缺陷。
     */
    void ReleaseSnapshot(uint32 SnapshotId);

    static MLogContext& GetTLS();

    // 仅供测试查看:返回当前活动条目数。生产代码应读快照而非
    // 直接查上下文。
    size_t Size() const {
        return EntryCount;
    }

    private:
    std::array<SEntry, kMaxEntries> Entries;
    size_t                          EntryCount = 0;
};

/**
 * @brief 全局快照池条目(进程级,静态存储)。
 */
struct SContextSnapshot {
    std::array<MLogContext::SEntry, MLogContext::kMaxEntries> Entries;
    size_t                                                    EntryCount = 0;
    std::atomic<bool>                                         InUse{true};
};

class MLogContextScope {
    public:
    MLogContextScope(MStringView Key, MStringView Value) {
        MLogContext::GetTLS().Set(Key, Value);
        SavedKey = Key;
    }
    ~MLogContextScope() {
        MLogContext::GetTLS().Unset(SavedKey);
    }

    private:
    MStringView SavedKey;
};
