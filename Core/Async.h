#pragma once

#include "NetCore.h"
#include "ITaskRunner.h"

namespace MAsync
{

/** 将 Next 投递到 Runner 的下一 tick 执行（即“让出”）。Runner 可为任意实现 ITaskRunner 的循环（如 MNetEventLoop）。 */
inline void Yield(ITaskRunner* Runner, TFunction<void()> Next)
{
    if (Runner && Next)
    {
        Runner->PostTask(std::move(Next));
    }
}

/** 多步序列：Do(step1); Do(step2); Run()；步与步之间在下一 tick 自动衔接。 */
class MSequence : public TEnableSharedFromThis<MSequence>
{
public:
    explicit MSequence(ITaskRunner* InRunner) : Runner(InRunner) {}

    static TSharedPtr<MSequence> Create(ITaskRunner* Runner)
    {
        return MakeShared<MSequence>(Runner);
    }

    void Do(TFunction<void()> Step)
    {
        if (Step)
        {
            Steps.push_back(std::move(Step));
        }
    }

    void Run()
    {
        if (Steps.empty())
        {
            return;
        }
        RunStep(0);
    }

private:
    void RunStep(size_t i)
    {
        if (i >= Steps.size())
        {
            return;
        }
        Steps[i]();
        if (i + 1 < Steps.size())
        {
            TSharedPtr<MSequence> Self = shared_from_this();
            const size_t Next = i + 1;
            Runner->PostTask([Self, Next]()
            {
                Self->RunStep(Next);
            });
        }
    }

    ITaskRunner* Runner = nullptr;
    TVector<TFunction<void()>> Steps;
};

}
