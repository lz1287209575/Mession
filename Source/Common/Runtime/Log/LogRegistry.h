#pragma once
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/MLib.h"
#include <mutex>

class MLogRegistry {
    public:
    static MLogRegistry& Get();

    /**
     * @brief 按名字注册一个日志分类(幂等)。
     * @param Name 分类名(C 字符串,需保持常量生命周期)
     * @param DefaultLevel 新分类的默认级别
     * @return 该分类的 SLogCategory 指针;同名已存在时直接返回已有项,
     * 不做任何修改。否则追加新项,其 Id 等于追加后的元素个数,
     * DefaultLevel 与 RuntimeLevel 均为 DefaultLevel
     */
    SLogCategory* RegisterCategory(const char* Name, ELogLevel DefaultLevel);

    const SLogCategory* GetById(uint16 Id) const;
    SLogCategory*       GetById(uint16 Id);
    const SLogCategory* FindByName(const MString& Name) const;

    size_t NumCategories() const {
        return Categories.size();
    }

    private:
    MLogRegistry() = default;

    // SLogCategory 含 std::atomic 成员,不可拷贝 / 移动,因此不能内联
    // 存于 TVector(需求原文写的 `TVector<SLogCategory>` 无法编译 ——
    // vector 要求元素至少可移动插入)。改为存 unique_ptr,对外分发裸
    // SLogCategory*;重分配不会使已分发的指针失效。
    TVector<TUniquePtr<SLogCategory>> Categories;
    // mutable:让 const 的 FindByName 也能加锁
    mutable std::mutex Mutex;
};
