/**
 * @file MRankListActor.cpp
 * @brief MRankListActor 实现.
 */
#include "Servers/EchoService/MRankListActor.h"

#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Log/Log.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <algorithm>
#include <cstring>
#include <vector>

    // 简化序列化(单测/样例级):uint64 + int64 小端拼字节.
    // 真生产用 MSTRUCT + MPROPERTY 反射序列化(CodingStyle §9.3)。
    namespace {
        void AppendU64(TByteArray& InOut, uint64 Value) {
            for (int Shift = 0; Shift < 64; Shift += 8) {
                InOut.push_back(static_cast<uint8>((Value >> Shift) & 0xFFu));
            }
        }

        uint64 ReadU64(const TByteArray& In, size_t Offset) {
            uint64 Value = 0;
            for (int Shift = 0; Shift < 64; Shift += 8) {
                Value |= static_cast<uint64>(In[Offset + Shift / 8]) << Shift;
            }
            return Value;
        }

        void AppendI64(TByteArray& InOut, int64 Value) {
            AppendU64(InOut, static_cast<uint64>(Value));
        }

        int64 ReadI64(const TByteArray& In, size_t Offset) {
            return static_cast<int64>(ReadU64(In, Offset));
        }
    } // namespace

    void MRankListActor::OnCreated() {
        LOG_INFO("MRankListActor created (ActorId=%llu)", static_cast<unsigned long long>(GetActorId()));
    }

    void MRankListActor::OnMessage(const FActorMessage& InMsg) {
        // 这里在 actor 自己的 Sub 线程,State 单线程访问,无需锁。
        const ERankListMessage Type = static_cast<ERankListMessage>(InMsg.Header.MsgType);
        switch (Type) {
        case ERankListMessage::UpdateScore: {
            // Payload: [PlayerId:u64][NewScore:i64]
            if (InMsg.Payload.size() < 16) {
                LOG_WARN("MRankListActor: UpdateScore payload too short (%zu)", InMsg.Payload.size());
                break;
            }
            const uint64 PlayerId = ReadU64(InMsg.Payload, 0);
            const int64  NewScore = ReadI64(InMsg.Payload, 8);
            State.PlayerScores[PlayerId] = NewScore;
            State.Version++;
            LOG_DEBUG("MRankListActor: UpdateScore player=%llu score=%lld (v%llu)",
                      static_cast<unsigned long long>(PlayerId),
                      static_cast<long long>(NewScore),
                      static_cast<unsigned long long>(State.Version));
            break;
        }
        case ERankListMessage::GetTopN: {
            // Payload: [N:i64](简单起见 N 也走 i64)
            int32 N = 10;
            if (InMsg.Payload.size() >= 8) {
                N = static_cast<int32>(ReadI64(InMsg.Payload, 0));
            }

            // 收集 + 降序排序 + 取 TopN
            TVector<TPair<uint64, int64>> All;
            All.reserve(State.PlayerScores.size());
            for (const auto& Pair : State.PlayerScores) {
                All.push_back(TPair<uint64, int64>(Pair.first, Pair.second));
            }
            std::sort(All.begin(), All.end(),
                      [](const TPair<uint64, int64>& A, const TPair<uint64, int64>& B) { return A.second > B.second; });

            TByteArray Resp;
            AppendU64(Resp, static_cast<uint64>(std::min<size_t>(All.size(), static_cast<size_t>(N))));
            for (size_t Index = 0; Index < All.size() && Index < static_cast<size_t>(N); ++Index) {
                AppendU64(Resp, All[Index].first);
                AppendI64(Resp, All[Index].second);
            }

            // 回包(仅 Call 时 ReplyPromise 非空)
            if (InMsg.ReplyPromise != nullptr) {
                InMsg.ReplyPromise->SetValue(TResult<TByteArray, FAppError>::Ok(std::move(Resp)));
            }
            break;
        }
        case ERankListMessage::GetPlayerRank: {
            // Payload: [PlayerId:u64]
            int64 Rank = -1; // -1 = 不在榜
            if (InMsg.Payload.size() >= 8) {
                const uint64 PlayerId = ReadU64(InMsg.Payload, 0);
                auto         It       = State.PlayerScores.find(PlayerId);
                if (It != State.PlayerScores.end()) {
                    // 排名 = 分数比它高的人数 + 1
                    int64 Higher = 0;
                    for (const auto& Pair : State.PlayerScores) {
                        if (Pair.second > It->second) {
                            Higher++;
                        }
                    }
                    Rank = Higher + 1;
                }
            }
            TByteArray Resp;
            AppendI64(Resp, Rank);
            if (InMsg.ReplyPromise != nullptr) {
                InMsg.ReplyPromise->SetValue(TResult<TByteArray, FAppError>::Ok(std::move(Resp)));
            }
            break;
        }
        default: {
            LOG_WARN("MRankListActor: unknown message type=%d", static_cast<int>(Type));
            break;
        }
        }
    }
