#pragma once
#include "Common/Runtime/MLib.h"
#include <array>

// Note: brief writes MStringView for the parameter type. The codebase renamed
// `namespace MStringView` to `namespace MStringViewUtil` in StringUtils.h to
// avoid the name clash with the MStringView type alias.

class MLogContext {
    public:
    static constexpr size_t kMaxEntries   = 32;
    static constexpr size_t kMaxSnapshots = 4096;

    struct SEntry {
        MString Key;
        MString Value;
    };

    void Set(MStringView Key, MStringView Value);
    void Set(MStringView Key, int64 Value);
    void Unset(MStringView Key);

    uint32 CaptureSnapshot(); // 返回 snapshot id,写入 GSnapshotPool
    void   ReleaseSnapshot(uint32 SnapshotId);

    static MLogContext& GetTLS();

    // Test-only inspection helper: returns the current number of live entries.
    // Production code should read from a captured snapshot instead.
    size_t Size() const {
        return EntryCount;
    }

    private:
    std::array<SEntry, kMaxEntries> Entries;
    size_t                          EntryCount = 0;
};

// Snapshot 池 (全局,静态)
struct SContextSnapshot {
    std::array<MLogContext::SEntry, MLogContext::kMaxEntries> Entries;
    size_t                                                    EntryCount = 0;
    std::atomic<bool>                                         InUse{true};
};

class MLogContextScope {
    public:
    MLogContextScope(MStringView Key, MStringView Value) {
        MLogContext::GetTLS().Set(Key, Value);
        SavedKey = Key;
    }
    ~MLogContextScope() {
        MLogContext::GetTLS().Unset(SavedKey);
    }

    private:
    MStringView SavedKey;
};
