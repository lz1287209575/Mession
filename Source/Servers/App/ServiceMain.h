/**
 * @file ServiceMain.h
 * @brief MServiceMain —— 所有 Service 进程的统一入口模板(工厂 + 启动流程)。
 *
 * @detail 统一流程:LoadConfig(argc, argv) -> 初始化 MLog 管道 -> 工厂创建 Service
 *   (默认 CreateService<TService>() 走 NewMObject) -> Init(ListenPort) -> Run() ->
 *   Shutdown() -> MLog::Shutdown()。Service 只需提供 MSTRUCT 配置与 Init/Run/
 *   Shutdown 语义,并可(可选)在自己 .cpp 里提供 extern "C" 工厂覆盖默认实现。
 */
#pragma once
#include "Common/Runtime/Concurrency/SignalHandler.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/MService.h"
#include "Servers/App/ServiceId.h"

namespace MServiceMain {

    /**
     * @brief ParseLocalServerType - 把人类可读的服务类型名转成 EServerType。
     * @param Name 来自 --local-type 的原始字符串(如 "Echo" / "Gateway" / "Player")
     * @return 匹配到的 EServerType;无法识别时返回 EServerType::Unknown
     *
     * @detail 这是"后处理派生"步骤:配置 struct 里 LocalServerTypeName(MString)由反射解析器
     *   填,而 LocalServerType(EServerType)反射填不了 —— 必须等 LoadConfig 返回后,由
     *   Service::Init() 调用本函数,再把结果写回 Service 侧的配置副本。
     *   新增业务 Service 类型时,本表 + EServerType 枚举同步加一行即可。
     */
    inline EServerType ParseLocalServerType(const MString& Name) {
        if (Name == "Gateway")
            return EServerType::Gateway;
        if (Name == "Echo")
            return EServerType::Echo;
        // F0→PlayerService 落地(2026-08-20):Player 业务进程类型
        if (Name == "Player")
            return EServerType::Player;
        return EServerType::Unknown;
    }

    /**
     * @brief CreateService - 默认 Service 工厂(不需要 extern "C")。
     * @return 由 TSharedPtr 持有的 Service 实例
     *
     * @detail 定义在 Run 之前,保证 Run 调用点能查到该模板(GCC 要求声明顺序)。
     *   Service 的 .cpp 里可以另提供 `extern "C" TSharedPtr<MObject> Create<ServiceClass>()`
     *   来覆盖本默认实现;默认实现走 NewMObject,实例生命周期始终留在 TSharedPtr 体系内
     *   (见 Object.h)。
     */
    template <typename TService> TSharedPtr<MObject> CreateService() {
        return NewMObject<TService>(/*Outer=*/nullptr, /*Name=*/typeid(TService).name());
    }

    /**
     * @brief Run - 所有 Service 进程的统一入口(每个 Service 都走这同一个)。
     * @param argc main() 的 argc,交给 LoadConfig 解析
     * @param argv main() 的 argv,交给 LoadConfig 解析
     * @return 0 正常退出(收到 SIGINT/SIGTERM);1 = 配置解析失败 / 工厂返回 null / Init 失败
     *
     * @detail Service 需要提供:
     *   1. 一个 MSTRUCT() 标注的配置 struct(如 SEchoServiceConfig),每个 CLI flag 用
     *      MPROPERTY(Meta=(Cli="--xxx")) 标注;
     *   2. 可选:自己 .cpp 里的 `extern "C" TSharedPtr<MObject> Create<ServiceClass>()`
     *      工厂。没有就用上面的默认 CreateService<T>() 模板实例化。无论哪种方式,实例都由
     *      TSharedPtr 持有,绝不用裸 new/delete(见 Object.h "DestroyMObject 已删除")。
     *
     *   流程:LoadConfig(argc, argv) -> MLog 初始化 -> CreateService<TService>() ->
     *   Init(ListenPort) -> Run -> Shutdown -> MLog::Shutdown。
     *   Run() 返回后必须显式调 Shutdown(),原因见该方法体内对应注释。
     *
     *   旧 per-Service 的 BuildConfig / ApplyConfig / ParseCommonArgs 已移除。
     */
    template <typename TService, typename TConfig> int Run(int argc, char** argv) {
        MSignalHandler::Install();

        if (!MService<TConfig>::LoadConfig(argc, argv)) {
            return 1;
        }

        // 初始化新的日志管道:这里始终读 LoadConfig 填好的配置(--log-file /
        // --log-config 等都经反射落到 SLogInitParams)。MLog::Init 幂等,因此那些
        // 已经在 main() 里初始化过日志的 Service 二进制不受影响。
        {
            const TConfig& Cfg = MService<TConfig>::GetConfig();
            SLogInitParams Params;
            Params.GlobalDefaultLevel = ELogLevel::Info;
            Params.bEnableConsole     = true;
            // 给了 --log-config(JSON)时,文件路径与轮转策略一律由 JSON 决定;否则退回
            // per-Service 默认值,即 --log-file / --log-rotate-bytes / --log-archives。
            if (Cfg.LogConfigPath.empty()) {
                if (!Cfg.LogFilePath.empty()) {
                    Params.FilePath = Cfg.LogFilePath;
                } else {
                    Params.FilePath = MString("Logs/service.jsonl");
                }
                Params.bUseColor        = false;
                Params.RotatedFileBytes = Cfg.LogRotateBytes;
                Params.NumArchives      = Cfg.LogArchives;
            } else {
                Params.FilePath = ""; // 留空,交给 JSON 配置覆盖
            }
            Params.ConfigPath = Cfg.LogConfigPath;

            TVector<SLogCategoryConfig> CatCfgs;
            TVector<SLogRouteConfig>    RouteCfgs;
            if (!Params.ConfigPath.empty()) {
                MLogApplyConfigFile(Params.ConfigPath, Params, CatCfgs, RouteCfgs);
            }
            MLog::Init(Params);
            if (!CatCfgs.empty())
                MLog::ApplyCategoryConfig(CatCfgs);
            if (!RouteCfgs.empty())
                MLog::ApplyRouteConfig(RouteCfgs);
        }

        TSharedPtr<MObject> ServiceObj = MServiceMain::CreateService<TService>();
        if (!ServiceObj) {
            LOG_FATAL("MServiceMain::Run: Create<%s>() returned null", typeid(TService).name());
            return 1;
        }

        TService*      Service = static_cast<TService*>(ServiceObj.Get());
        const TConfig& Config  = MService<TConfig>::GetConfig();

        // Init 失败路径:直接 return,ServiceObj 出作用域由 TSharedPtr 释放,
        // 触发 ~MObject 与 IDisposable::Dispose。
        if (!Service->Init(Config.ListenPort)) {
            return 1;
        }
        Service->Run();

        // 正常退出路径:MSignalHandler(SIGINT/SIGTERM)把 bRunning 置 false 后,
        // MNetServerBase::Run() 跳出主循环返回。Run() 自己**不**调 Shutdown()
        // ——它只注销 listener 就退出循环。所以必须在这里显式调 Shutdown(),让子类的
        // ShutdownConnections() 跑起来(关 transport、注销 actor、清 map)。
        // 否则 transport 状态会一直留在静态容器(MActorRouter::ActorRoutes)里直到进程
        // 退出 —— 这正是当初从 ~MObject 里删掉 RemoveFromRoot() 之前那个 double-free
        // 的成因形状。
        Service->Shutdown();

        MLog::Shutdown();

        // ServiceObj 在此出作用域,TSharedPtr 释放一次,~MObject 恰好跑一次
        // IDisposable::Dispose()。
        return 0;
    }

} // namespace MServiceMain