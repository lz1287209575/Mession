/**
 * @file RpcPayload.h
 * @brief RpcPayload - 基于反射的负载序列化/反序列化(消息结构体 ⇄ TByteArray)。
 *
 * @detail 序列化完全走 MClass 反射快照(MReflectArchive),所以参与收发的结构体必须在
 *   反射系统里注册(MSTRUCT/MPROPERTY 或 MHeaderTool 生成),否则 BuildPayload 返回空、
 *   ParsePayload 返回 missing_reflection_metadata。
 *
 *   本文件是 RPC 的公共底座:RpcTransport 的包体、MRpcChannel 的参数、actor 消息的
 *   FActorMessageWire 都经这两个模板转换。
 */

#pragma once

#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/StringUtils.h"

/**
 * @brief BuildPayload - 把消息结构体序列化为字节负载。
 * @param Message 待序列化的消息(只读,内部会做一次拷贝)
 * @return 序列化后的字节;结构体未注册反射元数据时返回空数组
 *
 * @detail 内部先拷贝一份再交给 WriteSnapshot,是因为反射写快照需要非 const 对象。
 *   注意"返回空数组"同时意味着"未注册反射元数据"和"合法的空负载",调用方无法从返回值
 *   区分二者 —— 若需要区分,请先用 MObject::FindStruct 自行探测。
 */
template <typename TMessage> inline TByteArray BuildPayload(const TMessage& Message) {
    if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TMessage)))) {
        MReflectArchive Archive;
        TMessage        MutableCopy = Message;
        StructMeta->WriteSnapshot(&MutableCopy, Archive);
        return std::move(Archive.Data);
    }

    return {};
}

/**
 * @brief ParsePayload - 把字节负载反序列化到消息结构体,并做完整性校验。
 * @param Data       待解析字节
 * @param OutMessage 输出消息;校验失败时内容未定义
 * @param Context    可选前缀,会拼在错误信息前(通常传函数名,便于定位)
 * @return Ok 解析成功;Err 时错误文本形如 "<Context>: payload_size=N, <原因>"
 *
 * @detail 校验两条不变量,任一不成立即失败:
 *   - 不得读越界(Archive.bReadOverflow)→ read_overflow
 *   - 必须恰好读完,不允许尾随多余字节(ReadPos == Data.size())→ trailing_bytes=N
 *   另:结构体未注册反射元数据时报 missing_reflection_metadata。
 *   严格"读满且不剩"是为了让协议不兼容/版本漂移立刻暴露,而不是被静默截断。
 */
template <typename TMessage> inline TResult<void, MString> ParsePayload(const TByteArray& Data, TMessage& OutMessage, const char* Context = nullptr) {
    const size_t PayloadSize = Data.size();
    if (MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TMessage)))) {
        MReflectArchive Archive(Data);
        StructMeta->WriteSnapshot(&OutMessage, Archive);
        if (Archive.bReadOverflow) {
            MString Error = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Error += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", read_overflow";
            return TResult<void, MString>::Err(std::move(Error));
        }
        if (Archive.ReadPos != PayloadSize) {
            const size_t Trailing = PayloadSize - Archive.ReadPos;
            MString      Error    = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
            Error += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", trailing_bytes=" + MStringUtil::ToString(static_cast<uint64>(Trailing));
            return TResult<void, MString>::Err(std::move(Error));
        }
        return TResult<void, MString>::Ok();
    }

    MString Error = (Context && Context[0]) ? (MString(Context) + ": ") : MString();
    Error += "payload_size=" + MStringUtil::ToString(static_cast<uint64>(PayloadSize)) + ", missing_reflection_metadata";
    return TResult<void, MString>::Err(std::move(Error));
}
