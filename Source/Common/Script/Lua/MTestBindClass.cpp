// MTestBindClass.cpp — 让 mht 扫到这个 TU,从而 include 链上的 MTestBindClass.h
// 触发的 MCLASS + MFUNCTION 注解被 BuildFunctionRegistrationBlock 读到,
// 后续 emit MTestBindClass.mgenerated.cpp 时 emit MREGISTER_NATIVE_METHOD
// 把 4 个 LuaBind 静态方法 register 到全局 MObject 反射表。
//
// 实际函数体已经在 .h 里 inline 定义(.h 是头 — mht 不扫 .h 单独).
// 这个 .cpp 唯一的目的是让 cmake 把 TU 加进 CDB,
// 进而被 mht 看到并 emit .mgenerated.cpp。
#include "MTestBindClass.h"