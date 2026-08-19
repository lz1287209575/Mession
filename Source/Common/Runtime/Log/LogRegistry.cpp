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
            return Up.get(); // already registered, return existing
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
