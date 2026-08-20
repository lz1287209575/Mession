#pragma once

// ActorMember 框架(F0,见 Docs/superpowers/specs/2026-08-14-actor-member-framework.md)
//
// 目标:业务协议以 MCLASS(Type = ActorMember) 类声明,协议下沉到成员;
// Service 类零业务协议,进程接收由框架内部成员分发器完成。
//
// 命名空间说明:设计文档草案用 `namespace mession::actor`,但本仓库现有
// actor 基础设施(MActorSystem/MActorRouter/IActor/MRankListActor)均为
// 全局命名空间,这里跟随代码现状(全局),避免割裂。

#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Actor/MActorSystem.h"
#include "Common/Runtime/Actor/MemberRpcManifest.generated.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <cstdint>

// ============================================================================
// 成员 RPC 注册表条目(GMemberRpcEntries)
//
// 条目由 MHeaderTool 生成(Build/Generated/MemberRpcManifest.mgenerated.cpp):
// 扫描 Type=ActorMember 类的 MFUNCTION(ServerCall),FunctionId 用
// ComputeStableReflectId(member 类名, 方法名)(与 ServerCall 同域,不同
// member 类同名方法不撞)。请求寻址:请求反序列化后取 PlayerId(继承序列化
// 修复后基类字段落盘)。
//
// SMemberRpcEntry 结构与查找函数声明见 MemberRpcManifest.generated.h。
// ============================================================================

// ============================================================================
// IMemberHost — 成员宿主抽象
//
// 双宿主:per-player 成员挂 actor 容器(IActor + MObject 实现 IMemberHost);
// 单例成员(认证等)挂服务实例。成员表键 = 成员类名。
// ============================================================================
class IActorMember;

class IMemberHost {
    public:
    virtual ~IMemberHost() = default;

    /** 成员表查询,按类型名(类名)查找已挂载成员。 */
    virtual IActorMember* FindMember(const MString& InTypeName) = 0;

    /** 注册成员(入表 + 挂宿主);重复注册覆盖。 */
    virtual void AddMember(const MString& InTypeName, TSharedPtr<IActorMember> InMember) = 0;
};

// ============================================================================
// IActorMember — 业务成员基类(含 MObject 反射身份)
//
// 用户代码:`class MPlayerItemContainer : public IActorMember`,成员是
// Component 默认子对象(CreateDefaultSubObject<T>(Owner)),生命周期跟随
// Owner(actor 容器)。
// ============================================================================
class IActorMember : public MObject {
    public:
    virtual ~IActorMember() = default;

    /** 挂到宿主:设置 Host + 调 OnAttach(子类钩子,成员初始化)。 */
    void AttachToHost(IMemberHost* InHost);

    /** 从宿主摘除:调 OnDetach + 清 Host。 */
    void DetachFromHost();

    IMemberHost* GetHost() const {
        return Host;
    }

    /** 成员间查找(同宿主):经宿主的成员表按类型名取实例。 */
    template <typename T> T* GetActorMember() {
        if (!Host) {
            return nullptr;
        }
        return static_cast<T*>(Host->FindMember(T::StaticClass()->GetName()));
    }

    /** 挂载钩子:子类覆盖做成员初始化(如加载数据)。 */
    virtual void OnAttach() {
    }
    /** 摘除钩子:子类覆盖做清理。 */
    virtual void OnDetach() {
    }

    protected:
    IMemberHost* Host = nullptr;
};

// ============================================================================
// TMemberHostImpl — 成员表默认实现(TMap 类型名 → TSharedPtr)
//
// 宿主(actor 容器 / 服务实例)继承或组合本类即获得成员表能力。
// AddMember 会把成员 AttachToHost(this);成员挂载顺序 = AddMember 调用序。
// ============================================================================
class TMemberHostImpl : public IMemberHost {
    public:
    IActorMember* FindMember(const MString& InTypeName) override {
        auto It = Members.find(InTypeName);
        return (It != Members.end()) ? It->second.Get() : nullptr;
    }

    void AddMember(const MString& InTypeName, TSharedPtr<IActorMember> InMember) override {
        Members[InTypeName] = InMember;
        if (InMember) {
            InMember->AttachToHost(this);
        }
    }

    /** 摘除成员(调 DetachFromHost + 出表)。 */
    void RemoveMember(const MString& InTypeName) {
        auto It = Members.find(InTypeName);
        if (It != Members.end()) {
            if (It->second) {
                It->second->DetachFromHost();
            }
            Members.erase(It);
        }
    }

    const TMap<MString, TSharedPtr<IActorMember>>& GetMembers() const {
        return Members;
    }

    protected:
    TMap<MString, TSharedPtr<IActorMember>> Members;
};

// ============================================================================
// 框架成员分发器 — 进程内 FunctionId → 成员反射调用
//
// 处理流程(2026-08-14-actor-member-framework.md §4.3):
//   1. 查 GMemberRpcEntries → (MemberClass, MethodName, RequestType)
//   2. 反序列化请求(继承序列化,含基类 PlayerId)→ FindProperty("PlayerId")
//   3. MActorSystem::Find(PlayerId) → actor 宿主(IActor 实现 IMemberHost)
//   4. 宿主成员表 → 成员实例
//   5. 反射调用:MemberClass::FindFunction(MethodName) → ServerCallHandler
//      (复用生成器产出的 handler:反序列化请求 + 调业务方法 + 回包桥接)
//
// PoC 简化:请求反序列化两次(分发器提取 PlayerId 一次,ServerCallHandler
// 内部再反序列化一次);成员方法在调用线程执行(跨 Sub 线程投递为演进项)。
// ============================================================================
SFutureResult<TByteArray> DispatchFrameworkMemberCall(uint16_t InFunctionId, const TByteArray& InPayload);

// ============================================================================
// FindActorMember<T>(PlayerId) — 非成员上下文(服务类/自由函数)获取 target
// 的显式路径:MActorSystem::Find(PlayerId) → actor 宿主 → 成员表。
// ============================================================================
template <typename T> T* FindActorMember(uint64_t InPlayerId) {
    MActorHandle Handle = MActorSystem::Get().Find(InPlayerId);
    IActor*      Actor  = Handle.GetActor();
    IMemberHost* Host   = Actor ? dynamic_cast<IMemberHost*>(Actor) : nullptr;
    if (!Host) {
        return nullptr;
    }
    return static_cast<T*>(Host->FindMember(T::StaticClass()->GetName()));
}
