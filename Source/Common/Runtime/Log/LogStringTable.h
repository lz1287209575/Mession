#pragma once
#include "Common/Runtime/MLib.h"

class MLogStringTable
{
public:
    static void Init();
    static uint32 Intern(const char* Str);
    static const char* Get(uint32 Id);
    static size_t Count();
};
