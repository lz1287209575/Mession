#include "Common/Runtime/Log/LogStringTable.h"
#include <cstring>
#include <mutex>
#include <unordered_map>

static std::unordered_map<uint32, MString> GStringTable;
static std::unordered_map<MString, uint32> GStringToId;
static std::mutex                          GTableMutex;
static uint32                              GNextId = 1; // 0 = 空/无效

void MLogStringTable::Init() {
    std::lock_guard<std::mutex> L(GTableMutex);
    GStringTable.clear();
    GStringToId.clear();
    GNextId = 1;
}

uint32 MLogStringTable::Intern(const char* Str) {
    if (!Str || !Str[0])
        return 0;
    std::lock_guard<std::mutex> L(GTableMutex);
    auto                        It = GStringToId.find(Str);
    if (It != GStringToId.end())
        return It->second;
    uint32 Id        = GNextId++;
    GStringToId[Str] = Id;
    GStringTable[Id] = Str;
    return Id;
}

const char* MLogStringTable::Get(uint32 Id) {
    std::lock_guard<std::mutex> L(GTableMutex);
    auto                        It = GStringTable.find(Id);
    if (It != GStringTable.end())
        return It->second.c_str();
    return "";
}

size_t MLogStringTable::Count() {
    std::lock_guard<std::mutex> L(GTableMutex);
    return GStringTable.size();
}
