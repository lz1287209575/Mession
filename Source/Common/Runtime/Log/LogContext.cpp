#include "Common/Runtime/Log/LogContext.h"
#include <thread>

static thread_local MLogContext GThisThreadContext;
static std::array<SContextSnapshot, MLogContext::kMaxSnapshots> GSnapshotPool;
static std::atomic<uint32> GSnapshotNextSlot{0};
static std::mutex GSnapshotMutex;

MLogContext& MLogContext::GetTLS()
{
    return GThisThreadContext;
}

void MLogContext::Set(MStringView Key, MStringView Value)
{
    for (size_t i = 0; i < EntryCount; ++i)
    {
        if (Entries[i].Key == Key)
        {
            Entries[i].Value = Value;
            return;
        }
    }
    if (EntryCount < kMaxEntries)
    {
        Entries[EntryCount++] = {MString(Key), MString(Value)};
    }
}

void MLogContext::Set(MStringView Key, int64 Value)
{
    Set(Key, std::to_string(Value));
}

void MLogContext::Unset(MStringView Key)
{
    for (size_t i = 0; i < EntryCount; ++i)
    {
        if (Entries[i].Key == Key)
        {
            Entries[i] = Entries[EntryCount - 1];
            --EntryCount;
            return;
        }
    }
}

uint32 MLogContext::CaptureSnapshot()
{
    std::lock_guard<std::mutex> L(GSnapshotMutex);
    uint32 Slot = GSnapshotNextSlot.load() % kMaxSnapshots;
    GSnapshotNextSlot.store(Slot + 1);
    GSnapshotPool[Slot].Entries = Entries;
    GSnapshotPool[Slot].EntryCount = EntryCount;
    GSnapshotPool[Slot].InUse.store(true);
    return Slot;
}

void MLogContext::ReleaseSnapshot(uint32 SnapshotId)
{
    if (SnapshotId < kMaxSnapshots)
    {
        GSnapshotPool[SnapshotId].InUse.store(false);
    }
}

