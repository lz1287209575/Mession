/**
 * @file LogRegistry.cpp
 * @brief MLogRegistry 实现 —— 分类注册表(名 → Id → 运行时级别)的进程级单例。
 *
 * 注册顺序决定 SLogCategory::Id,因此同一进程内 DEFINE_LOG_CATEGORY 的调用顺序必须稳定。
 */
#include "Common/Runtime/Log/LogRegistry.h"
#include <cstring>

MLogRegistry& MLogRegistry::Get() {
    static MLogRegistry Inst;
    return Inst;
}

SLogCategory* MLogRegistry::RegisterCategory(const char* Name, ELogLevel DefaultLevel) {
    std::lock_guard<std::mutex> L(Mutex);
    for (const auto& Up : Categories) {
        if (Up->Name != nullptr && std::strcmp(Up->Name, Name) == 0) {
            // 已注册过:同名的重复 DEFINE_LOG_CATEGORY 直接复用,
            // 保证 SLogCategory::Id 不因重复声明而漂移
            return Up.get();
        }
    }
    auto NewCat          = TUniquePtr<SLogCategory>(new SLogCategory());
    NewCat->Name         = Name;
    NewCat->Id           = static_cast<uint16>(Categories.size());
    NewCat->DefaultLevel = DefaultLevel;
    NewCat->RuntimeLevel.store(DefaultLevel);
    NewCat->bSuppressed.store(false);
    NewCat->DropCount.store(0);
    SLogCategory* Raw = NewCat.get();
    Categories.push_back(std::move(NewCat));
    return Raw;
}

const SLogCategory* MLogRegistry::GetById(uint16 Id) const {
    if (Id < Categories.size())
        return Categories[Id].get();
    return nullptr;
}

SLogCategory* MLogRegistry::GetById(uint16 Id) {
    if (Id < Categories.size())
        return Categories[Id].get();
    return nullptr;
}

const SLogCategory* MLogRegistry::FindByName(const MString& Name) const {
    std::lock_guard<std::mutex> L(Mutex);
    for (const auto& Up : Categories) {
        if (Up->Name == Name)
            return Up.get();
    }
    return nullptr;
}
