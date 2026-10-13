/**
 * @file MService.h
 * @brief MService<TConfigType> —— Service 进程的通用配置中心(反射式 CLI 解析)。
 *
 * @detail 取代旧的 ParseCommonArgs / BuildConfig / ApplyConfig 手写解析:
 *   1. 每个 Service 配一个 MSTRUCT() 标注的配置 struct(如 SEchoServiceConfig)。
 *      MHeaderTool 在 Build/Generated/<Name>.mgenerated.cpp 里生成
 *      `SAutoRegisterStruct_<Name> GAutoRegisterStruct_<Name>` 静态对象,其构造期调用
 *      MHeaderTool_Generated_RegisterStruct_<Name>() 把 MClass 塞进全局反射表。
 *   2. LoadConfig(argc, argv) 用 MObject::FindStruct(typeid(TConfigType)) 取 MClass,
 *      遍历 Properties 找 Meta=(Cli="--xxx") 字段,把 argv 里匹配到的 --xxx 值写回该字段
 *      (POD / MString / bool / enum / TVector<T> / 嵌套 MSTRUCT 的 SetValueFromString
 *      特化见 ReflectionPropertyTemplates.inl)。
 *   3. Service 类自己实现 Init / Run / ShutdownConnections,由
 *      MServiceMain::Run<TService, TConfig>(argc, argv) 启动。
 *
 * @detail MSTRUCT 必须先注册反射元数据(标 MSTRUCT() 即可,MHeaderTool 生成
 *   SAutoRegisterStruct_<Name> 自注册单例)。LoadConfig 找不到反射元数据时直接失败返回。
 *
 * @detail 静态初始化顺序(重要):C++ 只保证同一 translation unit 内静态对象按出现顺序
 *   初始化,跨 TU 顺序由实现定义;而反射表正是由这些静态对象填充的。因此:
 *     - main() 所在的 TU 必须链接到该配置 struct 的 .mgenerated.cpp(CMake 自动收集,
 *       见 MHeaderToolTargets.cmake)。
 *     - LoadConfig 之前不要在别的 TU 里触发任何会 FindStruct 的代码:注册可能尚未发生,
 *       拿到的 nullptr 会被误判成"配置错误"并触发 LOG_FATAL。
 *     - 不要把 LoadConfig 放进另一个静态对象的构造期。本模板的单例 Storage() 用
 *       function-local static(首次访问才构造)只规避了自身的跨 TU 顺序问题,管不到调用方。
 *
 * @detail MObject 生命周期:实现子类(MEchoService / MGatewayServer / MPlayerService ...)
 *   一律由 TSharedPtr 持有(见 Common/Runtime/Object/Object.h "DestroyMObject 已删除"):
 *   Run 模板用 NewMObject<TService>() 持有实例,进程退出时 TSharedPtr 在栈回退中释放,
 *   触发 ~MObject 里的 IDisposable::Dispose()(若实现)。严禁对 MObject 派生类直接 delete
 *   ——绕开 TSharedPtr 会破坏对象图。
 */
#pragma once

#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include <cstring>
#include <string>
#include <typeindex>

/**
 * @brief MService<TConfigType> - 配置中心:把 argv 反射式解析进 TConfigType 单例。
 *
 * @detail 使用顺序:先 LoadConfig() 写单例,再 GetConfig() / MutableConfig() 读单例。
 *   TConfigType 必须是已注册反射元数据的 MSTRUCT,否则 LoadConfig 直接失败。
 */
template <typename TConfigType> class MService {
    public:
    /**
     * @brief LoadConfig - 反射式解析 argv,写入 TConfigType 单例。
     * @param argc main() 的 argc(含程序名,解析从 i=1 开始)
     * @param argv main() 的 argv
     * @return true 每个匹配到的字段都解析成功;false = 反射元数据缺失,或某个字段解析失败
     *
     * @detail 只识别两种形式:`--key value`(把下一个 argv 当值)与 `--key=value`。
     *   字段必须带 Meta=(Cli="--xxx");未注册的 flag 与普通位置参数都被静默忽略。
     *   同一 flag 重复出现时后一次覆盖前一次(对同一字段反复 SetValueFromString)。
     *   解析失败即 fail-fast:LOG_ERROR 后返回 false,不让半成品配置进入 Init()。
     *   注意:失败不会回滚,单例里保留此前已写入的字段值。
     */
    static bool LoadConfig(int argc, char** argv) {
        MClass* ConfigClass = MObject::FindStruct(std::type_index(typeid(TConfigType)));
        if (!ConfigClass) {
            LOG_FATAL("MService<%s>: MSTRUCT not registered (no reflection metadata); "
                      "make sure the struct is annotated with MSTRUCT()",
                      typeid(TConfigType).name());
            return false;
        }

        TConfigType& Storage = MutableConfig();

        for (int i = 1; i < argc; ++i) {
            const MString Arg = argv[i] ? argv[i] : MString();
            if (Arg.size() < 2 || Arg[0] != '-' || Arg[1] != '-') {
                continue;
            }

            MString      Flag;
            MString      Value;
            const size_t Eq = Arg.find('=');
            if (Eq == MString::npos) {
                Flag = Arg;
                if (i + 1 < argc) {
                    Value = argv[i + 1] ? argv[i + 1] : MString();
                    ++i;
                } else {
                    continue;
                }
            } else {
                Flag  = Arg.substr(0, Eq);
                Value = Arg.substr(Eq + 1);
            }

            MProperty* Prop = ConfigClass->FindPropertyByMetadata(MString("Cli"), Flag);
            if (!Prop) {
                continue;
            }

            MString Err;
            if (!Prop->SetValueFromString(&Storage, Value, &Err)) {
                // Fail-fast:命令行参数格式错误必须让 Service 在 Init() 之前退出。
                // 继续跑只会静默保留字段默认值,把错误掩盖成"服务启动正常、稍后才炸"
                // ——绑错端口、连错对端,或者要等到第一次调用才报 actor 找不到。
                // 现在大声退出,远比一个"看起来启动成功"的错配服务好排查。
                LOG_ERROR("MService<%s>: failed to parse %s=%s (%s); aborting", typeid(TConfigType).name(), Flag.c_str(), Value.c_str(), Err.c_str());
                return false;
            }
        }
        return true;
    }

    /** @brief GetConfig - 只读访问配置单例(必须在 LoadConfig 之后调用)。 */
    static const TConfigType& GetConfig() {
        return Storage();
    }

    /** @brief MutableConfig - 可写访问配置单例(供 LoadConfig 内部按字段写值)。 */
    static TConfigType& MutableConfig() {
        return Storage();
    }

    private:
    /** @brief Storage - 配置单例(function-local static:首次访问才构造,规避跨 TU 静态初始化顺序)。 */
    static TConfigType& Storage() {
        static TConfigType Instance{};
        return Instance;
    }
};