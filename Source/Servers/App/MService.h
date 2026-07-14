#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Log/Logger.h"
#include <cstring>
#include <string>

/**
 * MService<TConfigType> — Service 通用配置中心模板。
 *
 * 设计意图（替代旧 ParseCommonArgs 形式）：
 *   1. 每个 Service 配一个 SEchoServiceConfig 之类的反射可见配置 struct
 *      （由 MHeaderTool 在 Build/Generated/<Name>.mgenerated.cpp 自动注册
 *       MHeaderTool_Generated_RegisterStruct_<Name>()）。
 *   2. main 流程：MService<TConfigType>::LoadConfig(argc, argv) → 通过反射
 *      FindStruct(typeid(TConfigType)) 拿到 MClass，遍历 Properties 找
 *      Meta=(Cli="--xxx") 字段，把 argv 里匹配的 --xxx 值写到对应字段
 *      （SetValueFromString 已经为 POD / MString / bool / enum / TVector<T>
 *       / nested MSTRUCT 实现了特化，ReflectionPropertyTemplates.inl）。
 *   3. Service 类自己实现 Run / Init / ShutdownConnections，通过
 *      MServiceMain::Run<TService, TConfig>(argc, argv) 启动。
 *
 * MSTRUCT 类型必须先被反射注册（用 MSTRUCT() 标记即可，由 MHeaderTool
 * 生成 SAutoRegisterStruct_<Name> 自注册单例）。如果 LoadConfig 找不到
 * 反射元数据，会 LOG_FATAL 退出。
 *
 * 关于 MObject 生命周期：
 *   - MObject 子类（包括所有 MEchoService/MGatewayServer）的生命周期由
 *     TSharedPtr 控制，参见 Common/Runtime/Object/Object.h 中"DestroyMObject
 *     已删除——MObject 生命周期由 TSharedPtr 持有自动管理"。
 *   - 因此 Run 模板用 NewMObject<TService>() 持有 Service 实例；进程
 *     退出时 TSharedPtr 在 stack unwinding 时自然释放，触发 ~MObject
 *     内的 IDisposable::Dispose()（如果实现了）。
 *   - 严禁对 MObject 派生类直接 `delete`（绕开 TSharedPtr 会破坏对象图）。
 */
template<typename TConfigType>
class MService
{
public:
    /**
     * LoadConfig — 反射式解析 argv 写入 TConfigType 单例。
     *
     * argv 形式支持两种：
     *   --key value            下一个 argv 是 value
     *   --key=value            = 后直接是 value
     *
     * 字段必须有 Meta=(Cli="--xxx")；未注册的字段被忽略。
     * 重复出现的 --key 覆盖（每次解析都对同一个字段 SetValueFromString）。
     */
    static bool LoadConfig(int argc, char** argv)
    {
        MClass* ConfigClass = MObject::FindStruct(std::type_index(typeid(TConfigType)));
        if (!ConfigClass)
        {
            LOG_FATAL("MService<%s>: MSTRUCT not registered (no reflection metadata); "
                      "make sure the struct is annotated with MSTRUCT()",
                      typeid(TConfigType).name());
            return false;
        }

        TConfigType& Storage = MutableConfig();

        for (int i = 1; i < argc; ++i)
        {
            const MString Arg = argv[i] ? argv[i] : MString();
            if (Arg.size() < 2 || Arg[0] != '-' || Arg[1] != '-')
            {
                continue;
            }

            MString Flag;
            MString Value;
            const size_t Eq = Arg.find('=');
            if (Eq == MString::npos)
            {
                Flag = Arg;
                if (i + 1 < argc)
                {
                    Value = argv[i + 1] ? argv[i + 1] : MString();
                    ++i;
                }
                else
                {
                    continue;
                }
            }
            else
            {
                Flag = Arg.substr(0, Eq);
                Value = Arg.substr(Eq + 1);
            }

            MProperty* Prop = ConfigClass->FindPropertyByMetadata(MString("Cli"), Flag);
            if (!Prop)
            {
                continue;
            }

            MString Err;
            if (!Prop->SetValueFromString(&Storage, Value, &Err))
            {
                LOG_WARN("MService<%s>: failed to parse %s=%s (%s)",
                         typeid(TConfigType).name(), Flag.c_str(), Value.c_str(), Err.c_str());
            }
        }
        return true;
    }

    static const TConfigType& GetConfig()
    {
        return Storage();
    }

    static TConfigType& MutableConfig()
    {
        return Storage();
    }

private:
    static TConfigType& Storage()
    {
        static TConfigType Instance{};
        return Instance;
    }
};