/**
 * @file MRankListActor.h
 * @brief 排行榜 actor —— 第一个业务 actor,验证 IActor 抽象 + 单线程访问契约.
 *
 * 注意:类放在全局命名空间 —— MHeaderTool 生成的 .mgenerated.cpp 假设
 * 反射类在全局(与 MEchoService 一致)。
 */
#pragma once

#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief ERankListMessage - 排行榜 actor 支持的消息类型.
 */
enum class ERankListMessage : int32 {
    UpdateScore   = 1,
    GetTopN       = 2,
    GetPlayerRank = 3,
};

/**
 * @brief MRankListActor - 排行榜 actor.
 *
 * 同时派生 IActor(actor 消息模型)+ MObject(反射身份 /
 * NewMObject 构造 / RootSet 生命周期)。构造必须走
 * NewMObject<MRankListActor>(nullptr, "RankList")。
 *
 * 全服共享的排行榜 state(TMap<uint64, int64> PlayerScores)只被
 * actor 自己的 Sub 线程访问 —— 通过 MActorHandle::Post/Call 发消息,
 * actor 单线程处理,无需锁。
 */
MCLASS(Type = Actor)
class MRankListActor : public IActor, public MObject {
    public:
    MGENERATED_BODY(MRankListActor, MObject, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问(与 MEchoService 一致)
    /** @brief 全服唯一 actor id(PoC 固定;真生产由 MServiceId::Make 派生). */
    static constexpr uint64 RANK_LIST_ACTOR_ID = 9001;

    MRankListActor()           = default;
    ~MRankListActor() override = default;

    // IActor 接口
    uint64 GetActorId() const override {
        return RANK_LIST_ACTOR_ID;
    }

    void OnMessage(const FActorMessage& InMsg) override;

    void OnCreated() override;

    // 持久化:SerializeState/RestoreState 由 MActorSystem 在 actor Sub 线程调,
    // 业务只需重写 2 个方法把 SState 序列化/反序列化（PoC 阶段用简单二进制格式）。
    TByteArray SerializeState() const override;
    bool       RestoreState(const TByteArray& InStateBytes) override;

    private:
    /**
     * @brief 排行榜 state —— actor 内部,只在 actor 自己的 Sub 线程访问.
     */
    struct SState {
        TMap<uint64, int64> PlayerScores; // PlayerId -> Score
        uint64              Version = 0;  // 每次 UpdateScore 递增
    };

    SState State;
};