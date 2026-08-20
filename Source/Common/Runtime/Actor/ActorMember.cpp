#include "Common/Runtime/Actor/ActorMember.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/Actor/MActorSystem.h"

#include "Common/Runtime/Reflect/Property.h"

// ============================================================================
// IActorMember — 宿主挂载/摘除
// ============================================================================
void IActorMember::AttachToHost(IMemberHost* InHost) {
    Host = InHost;
    OnAttach();
}

void IActorMember::DetachFromHost() {
    OnDetach();
    Host = nullptr;
}

// ============================================================================
// 框架成员分发器
// ============================================================================
SFutureResult<TByteArray> DispatchFrameworkMemberCall(uint16_t InFunctionId, const TByteArray& InPayload) {
    using TResultType = TResult<TByteArray, FAppError>;

    auto MakeError = [](const char* InCode) {
        return SFutureResult<TByteArray>(TResultType::Err(FAppError::Make(InCode, InCode)));
    };

    // 1. 查 GMemberRpcEntries → (MemberClass, MethodName, RequestType)
    const SMemberRpcEntry* Entry = FindMemberRpcEntryByFunctionId(InFunctionId);
    if (!Entry) {
        return MakeError("member_rpc_entry_not_found");
    }

    // 2. 反序列化请求(继承序列化,含基类 PlayerId)取路由目标
    uint64_t PlayerId = 0;
    if (Entry->RequestType && Entry->RequestType[0] != '\0') {
        MClass* ReqClass = MObject::FindStruct(Entry->RequestType);
        if (!ReqClass) {
            return MakeError("member_request_type_not_found");
        }

        void* ReqInstance = ReqClass->CreateInstance();
        if (!ReqInstance) {
            return MakeError("member_request_alloc_failed");
        }
        ReqClass->ReadSnapshot(ReqInstance, InPayload);

        // FindProperty 递归父类链(Class.cpp)——FPlayerRequestBase::PlayerId 可命中
        if (const MProperty* PlayerIdProp = ReqClass->FindProperty("PlayerId")) {
            if (const uint64_t* Ptr = PlayerIdProp->GetValuePtr<uint64_t>(ReqInstance)) {
                PlayerId = *Ptr;
            }
        }
        ReqClass->DestroyInstance(ReqInstance);
    }

    // 3. 定位宿主:PlayerId → MActorSystem → actor 容器(IActor 实现 IMemberHost)
    MActorHandle Handle = MActorSystem::Get().Find(PlayerId);
    IActor*      Actor  = Handle.GetActor();
    IMemberHost* Host   = Actor ? dynamic_cast<IMemberHost*>(Actor) : nullptr;
    if (!Host) {
        return MakeError("member_host_not_found");
    }

    // 4. 宿主成员表 → 成员实例
    IActorMember* Member = Host->FindMember(Entry->MemberClass);
    if (!Member) {
        return MakeError("member_not_found");
    }

    // 5. 反射调用:复用生成器产出的 ServerCallHandler(反序列化请求 +
    //    调业务方法 + SFutureResult<Resp> → SFutureResult<TByteArray> 桥接)
    MFunction* Func = Member->GetClass()->FindFunction(Entry->MethodName);
    if (!Func || !Func->ServerCallHandler) {
        return MakeError("member_function_not_found");
    }

    return Func->ServerCallHandler(Member, InPayload);
}
