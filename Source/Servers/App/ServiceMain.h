#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServiceId.h"
#include "Servers/App/MService.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Concurrency/SignalHandler.h"

namespace MServiceMain
{

/**
 * ParseLocalServerType - translate a human-readable server type name
 * ("Echo" / "Gateway" / ...) into the strongly-typed EServerType enum.
 *
 * Used as a post-parse derivation step: SEchoServiceConfig exposes both
 * `LocalServerTypeName` (MString, set from CLI via --local-type) and
 * `LocalServerType` (EServerType). The reflection parser only knows how to
 * fill the MString field; the enum must be filled in code, *after* LoadConfig
 * returns. Service::Init() calls this and writes the result back into the
 * Service-side Config copy.
 *
 * 新增业务 Service 类型时，本表 + EServerType 枚举同步加一行即可。
 */
inline EServerType ParseLocalServerType(const MString& Name)
{
    if (Name == "Gateway") return EServerType::Gateway;
    if (Name == "Echo")    return EServerType::Echo;
    return EServerType::Unknown;
}

/**
 * CreateService - default Service factory (extern "C" not required).
 *
 * Declared before Run so the template is visible at the Run call site below
 * (the lookup happens at instantiation, but GCC needs declaration order).
 *
 * Override per-Service by adding an `extern "C" TSharedPtr<MObject> Create<ServiceClass>()`
 * factory in the Service cpp. The default fallback uses NewMObject so the
 * MObject lifetime stays inside the TSharedPtr system (Object.h).
 */
template<typename TService>
TSharedPtr<MObject> CreateService()
{
    return NewMObject<TService>(/*Outer=*/nullptr, /*Name=*/typeid(TService).name());
}

/**
 * Run - unified Service entry point (every Service goes through the same one).
 *
 * A Service must provide:
 *   1. An SConfig subtype (e.g. SEchoServiceConfig) annotated with MSTRUCT(),
 *      with MPROPERTY(Meta=(Cli="--xxx")) marking each CLI flag.
 *   2. Optionally: an `extern "C" TSharedPtr<MObject> Create<ServiceClass>()`
 *      factory in the Service cpp. If absent, the default CreateService<T>()
 *      template above instantiates the Service via NewMObject<TService>(nullptr, ...).
 *      Either way the Service is owned by TSharedPtr, never by raw new/delete
 *      (see Object.h: "DestroyMObject deleted - MObject lifetime is managed
 *      by TSharedPtr").
 *
 * Flow: LoadConfig(argc, argv) -> CreateService<TService>() -> Init(port) -> Run.
 *
 * Old per-Service BuildConfig / ApplyConfig / ParseCommonArgs are gone.
 */
template<typename TService, typename TConfig>
int Run(int argc, char** argv)
{
    MSignalHandler::Install();

    if (!MService<TConfig>::LoadConfig(argc, argv))
    {
        return 1;
    }

    TSharedPtr<MObject> ServiceObj = MServiceMain::CreateService<TService>();
    if (!ServiceObj)
    {
        LOG_FATAL("MServiceMain::Run: Create<%s>() returned null", typeid(TService).name());
        return 1;
    }

    TService* Service = static_cast<TService*>(ServiceObj.Get());
    const TConfig& Config = MService<TConfig>::GetConfig();

    // Init failure path: ServiceObj goes out of scope on return and is
    // released by TSharedPtr, triggering ~MObject and IDisposable::Dispose.
    if (!Service->Init(Config.ListenPort))
    {
        return 1;
    }
    Service->Run();

    // Normal exit path: Service->Run() returns when MSignalHandler (SIGINT/SIGTERM)
    // flips bRunning inside MNetServerBase::Run. MNetServerBase::Run() itself does
    // NOT call Shutdown() on the way out — it only unregisters the listener and
    // breaks the loop. We must explicitly drive Shutdown() here so the subclass's
    // ShutdownConnections() runs (close transports, unregister actors, clear maps).
    // Without this, transport state lingers in static containers
    // (MActorRouter::ActorRoutes) until process
    // exit, which is the same shape as the double-free we fixed by removing
    // RemoveFromRoot() from ~MObject.
    Service->Shutdown();

    // ServiceObj goes out of scope here, TSharedPtr releases once,
    // ~MObject runs IDisposable::Dispose() exactly once.
    return 0;
}

} // namespace MServiceMain