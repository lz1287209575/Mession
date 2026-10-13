/**
 * @file MTestScriptBindClass.cpp
 * @brief 让 MHeaderTool 扫到这个 TU,从而读取 MTestScriptBindClass.h 上的
 *   MCLASS / MFUNCTION(ScriptBind) 注解并 emit .mgenerated.cpp。
 *
 * @detail 函数体都已在 .h 里 inline 定义,本 .cpp 不产出任何符号;它存在的唯一意义是
 *   让 CMake 把该 TU 加进编译数据库(CDB),MHeaderTool 才能看到并 emit
 *   MTestScriptBindClass.mgenerated.cpp(其中调用 MREGISTER_NATIVE_METHOD
 *   把这 4 个 ScriptBind 入口注册进全局反射表)。
 */
#include "MTestScriptBindClass.h"