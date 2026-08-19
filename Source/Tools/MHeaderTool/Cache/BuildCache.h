#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // File Fingerprint - 用于检测文件变化
    // ============================================================================

    struct SFileFingerprint {
        fs::path Path;
        uint64_t FileSize     = 0;
        uint64_t ModifiedTime = 0;
        uint64_t ContentHash  = 0;

        bool operator==(const SFileFingerprint& other) const {
            // 只比较文件大小和内容哈希（跳过时间戳，避免跨平台/大数问题）
            return FileSize == other.FileSize && ContentHash == other.ContentHash;
        }

        bool operator!=(const SFileFingerprint& other) const {
            return !(*this == other);
        }
    };

    // ============================================================================
    // Type Cache Entry - 单个类型的缓存
    // ============================================================================

    struct STypeCacheEntry {
        MString          TypeName;
        fs::path         HeaderPath;
        SFileFingerprint HeaderFingerprint;
        uint64_t         ParsedContentHash = 0;
        fs::path         OutputHeader;
        fs::path         OutputSource;
        TVector<MString> DefinedTypes;
        TVector<MString> IncludedHeaders;

        bool IsValid() const {
            return !TypeName.empty() && !HeaderPath.empty();
        }
    };

    // ============================================================================
    // Build Cache - 整体缓存
    // ============================================================================

    struct SBuildCache {
        static constexpr uint32_t Version = 2;

        uint32_t                              Version_ = Version;
        std::chrono::system_clock::time_point CreatedAt;
        TVector<STypeCacheEntry>              TypeEntries;
        MString                               MHeaderToolVersion;

        // 快速查找
        TMap<MString, size_t>  TypeNameToIndex;
        TMap<fs::path, size_t> HeaderPathToIndex;

        void BuildIndices() {
            TypeNameToIndex.clear();
            HeaderPathToIndex.clear();
            for (size_t i = 0; i < TypeEntries.size(); ++i) {
                const auto& entry = TypeEntries[i];
                if (!entry.TypeName.empty()) {
                    TypeNameToIndex[entry.TypeName] = i;
                }
                // 使用第一个出现的路径（不覆盖）
                if (!entry.HeaderPath.empty() && HeaderPathToIndex.find(entry.HeaderPath) == HeaderPathToIndex.end()) {
                    HeaderPathToIndex[entry.HeaderPath] = i;
                }
            }
        }

        const STypeCacheEntry* FindType(const MString& typeName) const {
            auto it = TypeNameToIndex.find(typeName);
            if (it != TypeNameToIndex.end()) {
                return &TypeEntries[it->second];
            }
            return nullptr;
        }

        const STypeCacheEntry* FindByHeader(const fs::path& headerPath) const {
            auto it = HeaderPathToIndex.find(headerPath);
            if (it != HeaderPathToIndex.end()) {
                return &TypeEntries[it->second];
            }
            return nullptr;
        }
    };

    // ============================================================================
    // Incremental Decision
    // ============================================================================

    enum class EIncrementalAction { Skip, Regenerate, FullRebuild };

    struct SIncrementalDecision {
        EIncrementalAction Action = EIncrementalAction::Skip;
        MString            Reason;
        TVector<MString>   AffectedTypes;
        TVector<fs::path>  AffectedFiles;
        TVector<fs::path>  RemovedFiles;
        size_t             SkippedCount     = 0;
        size_t             RegeneratedCount = 0;
    };

    // ============================================================================
    // Change Detection Result
    // ============================================================================

    struct SChangeDetectionResult {
        TVector<fs::path> ChangedFiles;
        TVector<fs::path> AddedFiles;
        TVector<fs::path> RemovedFiles;
        bool              bRequiresFullRebuild = false;
        MString           RebuildReason;
    };

} // namespace MHeaderTool
